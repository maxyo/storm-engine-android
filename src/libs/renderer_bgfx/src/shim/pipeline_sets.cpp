#include "shim/pipeline_sets.hpp"

#include "shim/device.hpp"
#include "shim/shim.hpp"
#include "storm/fx/programs.hpp"
#include "storm/render/pipelines.hpp"
#include "texture/core/cache.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <fstream>
#include <system_error>

// patches/bgfx-vulkan-pipeline-cache.patch (src/renderer_vk.cpp) defines these.
namespace bgfx
{
enum StormPipelineFlags
{
    kStormPipelineOffThread = 1,
    kStormPipelinePrewarm = 2,
    kStormPipelineDrawWaited = 4,
    kStormPipelineUnusable = 8,
};
typedef void (*VkPipelineCreatedFn)(const char *_vs, const char *_fs, uint64_t _state, double _ms, uint32_t _flags,
                                    uint32_t _hash, const void *_recipe, uint32_t _recipeSize);
void setVkPipelineCreated(VkPipelineCreatedFn _created);
void setVkPipelineThreads(uint32_t _num, int32_t _nice);
bool prewarmVkPipeline(ProgramHandle _program, const void *_recipe, uint32_t _size, uint32_t _frame);
uint32_t getVkPipelinesPending();
uint32_t getVkDrawsLeftOut();
uint32_t getVkLastWaitingFrame();
bool getVkPipelineCacheControl();
} // namespace bgfx

namespace storm::shim
{

namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// A pipeline creation this long holds the render thread over a 60 Hz frame: logged with its program and state.
constexpr double kSlowPipelineMs = 16.0;
// The longest a fade-in waits for the scene's pipelines; the rest are created while it is shown, their draws
// left out until then (each logged).
constexpr auto kRevealBound = std::chrono::seconds(5);
// Presented scene frames without a fade-in asking: the scene was shown without a fader, nothing to hold.
constexpr uint32_t kNoFaderFrames = 300;
// Measured after a reveal.
constexpr auto kMeasureWindow = std::chrono::seconds(30);
// Bounds of what is kept (a recipe is a few hundred bytes).
constexpr size_t kMaxRecipes = 8192;
// Pipeline threads below the game and render threads (setpriority).
constexpr int32_t kPipelineThreadNice = 5;

constexpr char kMagic[8] = {'S', 'T', 'P', 'I', 'P', 'E', 'S', 'T'};
constexpr uint32_t kVersion = 1;

uint32_t pipeline_threads()
{
    // A third of the cores, so that the game, render and loading threads keep theirs: 2 on an 8-core phone.
#ifdef __ANDROID__
    constexpr uint32_t kMax = 3;
#else
    constexpr uint32_t kMax = 4;
#endif
    const uint32_t cores = std::max(1u, std::thread::hardware_concurrency());
    return std::clamp(cores / 3, 1u, kMax);
}

uint64_t checksum(const void *data, size_t size)
{
    return texture::hash64(data, size, 0x5354504950ull);
}

// Little helpers of the file format: native byte order (the file belongs to this device).
struct Writer
{
    std::vector<uint8_t> bytes;
    template <typename T> void put(const T &value)
    {
        const auto *p = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    void put_string(const std::string &s)
    {
        put(static_cast<uint16_t>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
};

struct Reader
{
    const uint8_t *at, *end;
    bool ok = true;
    template <typename T> T get()
    {
        T value{};
        if (end - at < static_cast<ptrdiff_t>(sizeof(T)))
            ok = false;
        else
            std::memcpy(&value, at, sizeof(T)), at += sizeof(T);
        return value;
    }
    std::string get_string()
    {
        const uint16_t n = get<uint16_t>();
        if (!ok || end - at < n)
        {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char *>(at), n);
        at += n;
        return s;
    }
};

double percentile(const std::vector<float> &sorted, double p)
{
    if (sorted.empty())
        return 0.0;
    const size_t i = std::min(sorted.size() - 1, static_cast<size_t>(p * static_cast<double>(sorted.size())));
    return sorted[i];
}
} // namespace

PipelineSets &PipelineSets::instance()
{
    static PipelineSets sets;
    return sets;
}

void PipelineSets::attach(const fs::path &directory, bool vulkan)
{
    std::lock_guard lock(mutex_);
    if (writer_.joinable())
        writer_.join();
    vulkan_ = vulkan;
    file_ = directory.empty() ? fs::path{} : directory / "pipelines.bin";
    recipes_.clear();
    scenes_.clear();
    created_.clear();
    to_prewarm_.clear();
    waiting_.clear();
    programs_.clear(); // of the device before
    dirty_ = false;
    reveal_ = {};
    measure_ = {};
    caps_logged_ = false;
    pipelines_ = render_thread_ = slow_ = off_thread_ = prewarms_ = unpredicted_ = 0;
    render_thread_ms_ = off_thread_ms_ = 0.0;
    scene_ = "start";
    if (!vulkan)
    {
        bgfx::setVkPipelineCreated(nullptr);
        return;
    }
    const uint32_t threads = pipeline_threads();
    bgfx::setVkPipelineThreads(threads, kPipelineThreadNice);
    bgfx::setVkPipelineCreated(&PipelineSets::created);
    logf("pipelines: %u pipeline threads (of %u cores, nice %d) create the scene's pipelines off the render thread",
         threads, std::thread::hardware_concurrency(), kPipelineThreadNice);
    if (file_.empty())
        logf("pipelines: no program cache directory: nothing recorded is kept between runs, nothing is prewarmed");
    else
        load();
    // The menus' own pipelines, ahead of the first menu frame.
    if (const auto it = scenes_.find(scene_); it != scenes_.end())
        to_prewarm_.assign(it->second.begin(), it->second.end());
}

void PipelineSets::load()
{
    std::error_code error;
    if (!fs::exists(file_, error))
    {
        logf("pipelines: no recorded pipelines yet (%s): every scene compiles its pipelines when it is first shown",
             file_.string().c_str());
        return;
    }
    std::ifstream in(file_, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    const auto refuse = [&](const char *why) {
        logf("ERROR: pipelines: %s %s: removed, the scenes record their pipelines again", file_.string().c_str(), why);
        recipes_.clear();
        scenes_.clear();
        fs::remove(file_, error);
    };
    if (bytes.size() < sizeof(kMagic) + 4 + 8 || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
        return refuse("has another format");
    uint64_t sum = 0;
    std::memcpy(&sum, bytes.data() + bytes.size() - 8, 8);
    if (checksum(bytes.data(), bytes.size() - 8) != sum)
        return refuse("is truncated or corrupt (checksum)");
    Reader r{bytes.data() + sizeof(kMagic), bytes.data() + bytes.size() - 8};
    if (r.get<uint32_t>() != kVersion)
        return refuse("has another version");
    const uint32_t recipes = r.get<uint32_t>();
    for (uint32_t i = 0; r.ok && i < recipes; ++i)
    {
        const uint32_t hash = r.get<uint32_t>();
        Recipe recipe;
        recipe.vs = r.get_string();
        recipe.fs = r.get_string();
        const uint32_t size = r.get<uint32_t>();
        if (!r.ok || r.end - r.at < size)
        {
            r.ok = false;
            break;
        }
        recipe.bytes.assign(r.at, r.at + size);
        r.at += size;
        recipes_.emplace(hash, std::move(recipe));
    }
    const uint32_t scenes = r.get<uint32_t>();
    for (uint32_t i = 0; r.ok && i < scenes; ++i)
    {
        std::set<uint32_t> &set = scenes_[r.get_string()];
        const uint32_t n = r.get<uint32_t>();
        for (uint32_t j = 0; r.ok && j < n; ++j)
            if (const uint32_t hash = r.get<uint32_t>(); recipes_.contains(hash))
                set.insert(hash);
    }
    if (!r.ok || r.at != r.end)
        return refuse("does not parse");
    logf("pipelines: %zu recorded pipelines of %zu scenes (%s, %.0f KB)", recipes_.size(), scenes_.size(),
         file_.string().c_str(), bytes.size() / 1024.0);
}

void PipelineSets::write_file()
{
    if (file_.empty() || !dirty_)
        return;
    Writer w;
    w.bytes.insert(w.bytes.end(), kMagic, kMagic + sizeof(kMagic));
    w.put(kVersion);
    w.put(static_cast<uint32_t>(recipes_.size()));
    for (const auto &[hash, recipe] : recipes_)
    {
        w.put(hash);
        w.put_string(recipe.vs);
        w.put_string(recipe.fs);
        w.put(static_cast<uint32_t>(recipe.bytes.size()));
        w.bytes.insert(w.bytes.end(), recipe.bytes.begin(), recipe.bytes.end());
    }
    w.put(static_cast<uint32_t>(scenes_.size()));
    for (const auto &[name, set] : scenes_)
    {
        w.put_string(name);
        w.put(static_cast<uint32_t>(set.size()));
        for (const uint32_t hash : set)
            w.put(hash);
    }
    w.put(checksum(w.bytes.data(), w.bytes.size()));
    dirty_ = false;
    if (writer_.joinable())
        writer_.join();
    writer_ = std::thread([file = file_, bytes = std::move(w.bytes)] {
        std::error_code error;
        fs::create_directories(file.parent_path(), error); // the system may have cleared the cache meanwhile
        fs::path temporary = file;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out)
            {
                logf("ERROR: pipelines: cannot write %s: the recorded pipelines are not kept", temporary.string().c_str());
                fs::remove(temporary, error);
                return;
            }
        }
        fs::rename(temporary, file, error);
        if (error)
        {
            logf("ERROR: pipelines: cannot store %s (%s)", file.string().c_str(), error.message().c_str());
            fs::remove(temporary, error);
        }
    });
}

void PipelineSets::save()
{
    std::lock_guard lock(mutex_);
    write_file();
}

void PipelineSets::detach()
{
    std::lock_guard lock(mutex_);
    if (!vulkan_)
        return;
    if (measure_.active)
        end_measure("the device went");
    if (pipelines_)
        logf("pipelines: %u created by this device: %u on the render thread (%u of them over %.0f ms, %.0f ms in all), "
             "%u off it (%.0f ms in all); %u prewarmed, %u for scene draws nobody predicted",
             pipelines_, render_thread_, slow_, kSlowPipelineMs, render_thread_ms_, off_thread_, off_thread_ms_,
             prewarms_, unpredicted_);
    bgfx::setVkPipelineCreated(nullptr);
    if (!waiting_.empty())
    {
        logf("pipelines: %zu recorded pipelines never got their program in this run (no program of those shaders was "
             "made): forgotten",
             waiting_.size());
        for (const uint32_t hash : std::vector<uint32_t>(waiting_.begin(), waiting_.end()))
            forget(hash);
    }
    write_file();
    if (writer_.joinable())
        writer_.join();
    vulkan_ = false;
}

void PipelineSets::created(const char *vs, const char *fs, uint64_t state, double ms, uint32_t flags, uint32_t hash,
                           const void *recipe, uint32_t size)
{
    PipelineSets &sets = instance();
    std::lock_guard lock(sets.mutex_);
    if (flags & bgfx::kStormPipelineUnusable)
    {
        // Recorded with other shaders than the program has now (the game or the engine changed them).
        if (sets.recipes_.erase(hash))
        {
            for (auto &[name, set] : sets.scenes_)
                set.erase(hash);
            sets.dirty_ = true;
        }
        ++sets.reveal_.unusable;
        logf("pipelines: a recorded pipeline of %s + %s, state 0x%016" PRIx64 ", no longer matches its shaders: "
             "forgotten",
             vs, fs, state);
        return;
    }
    ++sets.pipelines_;
    sets.created_.insert(hash);
    if (size == 0)
        logf("ERROR: pipelines: the pipeline of %s + %s, state 0x%016" PRIx64 ", has no recipe (its render pass was "
             "not recorded): it cannot be prewarmed",
             vs, fs, state);
    else if (!sets.recipes_.contains(hash) && sets.recipes_.size() >= kMaxRecipes)
        logf("ERROR: pipelines: %zu recorded pipelines, the most kept: %s + %s is not recorded", kMaxRecipes, vs, fs);
    else
    {
        if (!sets.recipes_.contains(hash))
        {
            const auto *bytes = static_cast<const uint8_t *>(recipe);
            sets.recipes_.emplace(hash, Recipe{vs, fs, std::vector<uint8_t>(bytes, bytes + size)});
            sets.dirty_ = true;
        }
        sets.dirty_ |= sets.scenes_[sets.scene_].insert(hash).second;
    }
    if (flags & bgfx::kStormPipelinePrewarm)
    {
        ++sets.prewarms_;
        if (sets.reveal_.armed)
            ++sets.reveal_.prewarmed, sets.reveal_.prewarm_ms += ms;
        if (flags & bgfx::kStormPipelineDrawWaited)
            logf("pipeline: prewarmed too late, a scene draw waited for it: %.1f ms for %s + %s, state 0x%016" PRIx64,
                 ms, vs, fs, state);
    }
    else if (flags & bgfx::kStormPipelineDrawWaited)
    {
        // The last resort: a scene draw left out for a pipeline nobody predicted. Recorded above for the next time.
        ++sets.unpredicted_;
        if (sets.reveal_.armed)
            ++sets.reveal_.waited;
        if (sets.measure_.active)
            ++sets.measure_.unpredicted, sets.measure_.unpredicted_ms += ms;
        logf("pipeline: unpredicted, a scene draw waited %.1f ms for %s + %s, state 0x%016" PRIx64 " (recorded for %s)",
             ms, vs, fs, state, sets.scene_.c_str());
    }
    if (flags & bgfx::kStormPipelineOffThread)
    {
        ++sets.off_thread_;
        sets.off_thread_ms_ += ms;
        return;
    }
    ++sets.render_thread_;
    sets.render_thread_ms_ += ms;
    if (ms < kSlowPipelineMs)
        return;
    ++sets.slow_;
    logf("pipeline: %.1f ms on the render thread for %s + %s, state 0x%016" PRIx64 " (pipeline %u of this start, %u of "
         "them slow on the render thread, %.0f ms there in all)",
         ms, vs, fs, state, sets.pipelines_, sets.slow_, sets.render_thread_ms_);
}

void PipelineSets::begin_scene(const char *scene)
{
    std::lock_guard lock(mutex_);
    if (!vulkan_)
        return;
    if (measure_.active)
        end_measure("the next scene began");
    write_file(); // what the scene before learned
    scene_ = scene;
    reveal_ = {};
    reveal_.armed = true;
    reveal_.begun = Clock::now();
    to_prewarm_.clear();
    size_t recorded = 0;
    if (const auto it = scenes_.find(scene_); it != scenes_.end())
    {
        recorded = it->second.size();
        for (const uint32_t hash : it->second)
            if (!created_.contains(hash))
                to_prewarm_.push_back(hash);
    }
    logf("pipelines: scene %s begins: %zu recorded pipelines, %zu of them to prewarm (the others exist)", scene_.c_str(),
         recorded, to_prewarm_.size());
}

bool PipelineSets::hold_reveal()
{
    std::lock_guard lock(mutex_);
    if (!vulkan_ || !reveal_.armed)
        return false;
    const auto now = Clock::now();
    if (!reveal_.holding)
    {
        reveal_.holding = true;
        reveal_.hold_started = now;
        reveal_.hold_frame = presented_frame_;
    }
    reveal_.query_frame = presented_frame_; // frame(): a fader that stopped asking
    const uint32_t pending = bgfx::getVkPipelinesPending() + static_cast<uint32_t>(to_prewarm_.size());
    // bgfx::frame() returned presented_frame_ after the render thread finished the frame before: that one drew the
    // scene under the closed fader and left nothing out.
    const bool complete = bgfx::getVkLastWaitingFrame() < presented_frame_;
    const bool ready = pending == 0 && complete && presented_frame_ >= reveal_.hold_frame + 2;
    const bool bound = now - reveal_.hold_started >= kRevealBound;
    if (!ready && !bound)
        return true;
    const auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    logf("pipelines: scene %s revealed: the fade-in waited %.0f ms (%s), %.0f ms after the scene began; %u prewarms "
         "asked for (%u refused), %u created (%.0f ms of compiles, %u no longer matching), %u created for scene draws "
         "that waited",
         scene_.c_str(), ms(now - reveal_.hold_started),
         ready ? "every pipeline ready" : "bound reached, the rest are created while it is shown",
         ms(now - reveal_.begun), reveal_.asked, reveal_.refused, reveal_.prewarmed, reveal_.prewarm_ms,
         reveal_.unusable, reveal_.waited);
    if (!ready)
        logf("pipelines: scene %s shown with %u pipelines still pending: their draws appear when they exist",
             scene_.c_str(), pending);
    reveal_.armed = reveal_.holding = false;
    start_measure(now);
    return false;
}

void PipelineSets::start_measure(Clock::time_point now)
{
    measure_ = {};
    measure_.active = true;
    measure_.scene = scene_;
    measure_.started = measure_.last = now;
    measure_.left_out_start = bgfx::getVkDrawsLeftOut();
    measure_.last_waiting = bgfx::getVkLastWaitingFrame();
}

void PipelineSets::end_measure(const char *why)
{
    measure_.active = false;
    std::vector<float> sorted = measure_.frame_ms;
    std::sort(sorted.begin(), sorted.end());
    const double seconds = std::chrono::duration<double>(measure_.last - measure_.started).count();
    logf("pipelines: first %.0f s of %s (%s): %zu frames, frame time p50 %.1f, p95 %.1f, p99 %.1f, max %.1f ms; %u "
         "frames incomplete (%.0f ms, %u draws left out), %u pipelines unpredicted (%.0f ms of compiles)",
         seconds, measure_.scene.c_str(), why, sorted.size(), percentile(sorted, 0.50), percentile(sorted, 0.95),
         percentile(sorted, 0.99), sorted.empty() ? 0.0 : sorted.back(), measure_.incomplete_frames,
         measure_.incomplete_ms, bgfx::getVkDrawsLeftOut() - measure_.left_out_start, measure_.unpredicted,
         measure_.unpredicted_ms);
}

void PipelineSets::forget(uint32_t hash)
{
    for (auto &[name, set] : scenes_)
        set.erase(hash);
    recipes_.erase(hash);
    waiting_.erase(hash);
    dirty_ = true;
}

void PipelineSets::register_program(const char *vs, const char *fs, bgfx::ProgramHandle program)
{
    std::lock_guard lock(mutex_);
    if (!vulkan_)
        return;
    const std::string key = std::string(vs) + '+' + fs;
    programs_[key] = program;
    size_t woken = 0;
    for (auto it = waiting_.begin(); it != waiting_.end();)
    {
        const auto recipe = recipes_.find(*it);
        if (recipe != recipes_.end() && recipe->second.vs + '+' + recipe->second.fs == key)
        {
            to_prewarm_.push_back(*it);
            it = waiting_.erase(it);
            ++woken;
        }
        else
            ++it;
    }
    if (woken)
        logf("pipelines: program %s registered: %zu recorded pipelines waiting for it are prewarmed", key.c_str(), woken);
}

void PipelineSets::unregister_program(bgfx::ProgramHandle program)
{
    std::lock_guard lock(mutex_);
    for (auto it = programs_.begin(); it != programs_.end();)
        it = it->second.idx == program.idx ? programs_.erase(it) : std::next(it);
}

bgfx::ProgramHandle PipelineSets::program_of(Device &device, const Recipe &recipe)
{
    // The registered programs (the interface's) and the fx package's passes by their names, else the shim's own.
    const std::string key = recipe.vs + '+' + recipe.fs;
    if (const auto it = programs_.find(key); it != programs_.end())
        return it->second;
    if (const bgfx::ProgramHandle fx = fx::FindProgram(recipe.vs.c_str(), recipe.fs.c_str()); bgfx::isValid(fx))
        return programs_[key] = fx; // kept with the device (bgfx shares it with the pass's own)
    const uint32_t vs = find_shader_by_name(recipe.vs.c_str());
    const uint32_t fs = recipe.fs.empty() ? UINT32_MAX : find_shader_by_name(recipe.fs.c_str());
    return vs == UINT32_MAX || fs == UINT32_MAX ? bgfx::ProgramHandle BGFX_INVALID_HANDLE : device.program(vs, fs);
}

void PipelineSets::frame(Device &device, uint32_t presented_frame, bool scene_frame)
{
    std::lock_guard lock(mutex_);
    if (!vulkan_)
        return;
    presented_frame_ = presented_frame;
    if (!caps_logged_)
    {
        caps_logged_ = true;
        logf(bgfx::getVkPipelineCacheControl()
                 ? "pipelines: pipeline creation cache control (Vulkan 1.3): a scene draw whose pipeline is in the "
                   "pipeline cache gets it at once, only a compile leaves it out"
                 : "pipelines: no pipeline creation cache control (Vulkan 1.3) on this device: every new scene pipeline "
                   "leaves its draw out for a frame or more, also when the pipeline cache holds it");
    }
    // The prewarms: their programs are made now, so they exist for the render thread from the next frame on.
    for (const uint32_t hash : to_prewarm_)
    {
        const auto it = recipes_.find(hash);
        if (it == recipes_.end())
            continue;
        const Recipe &recipe = it->second;
        const bgfx::ProgramHandle program = program_of(device, recipe);
        if (!bgfx::isValid(program))
        {
            // Not made yet (the interface's programs exist from its init on): kept, prewarmed when it registers.
            waiting_.insert(hash);
            continue;
        }
        if (!bgfx::prewarmVkPipeline(program, recipe.bytes.data(), static_cast<uint32_t>(recipe.bytes.size()),
                                     presented_frame + 1))
        {
            logf("pipelines: a recorded pipeline of %s + %s cannot be prewarmed (a recipe of another format): "
                 "forgotten",
                 recipe.vs.c_str(), recipe.fs.c_str());
            forget(hash);
            ++reveal_.refused;
            continue;
        }
        created_.insert(hash);
        ++reveal_.asked;
    }
    to_prewarm_.clear();
    const auto now = Clock::now();
    if (reveal_.armed && reveal_.holding && presented_frame > reveal_.query_frame + 5)
        reveal_.holding = false; // the fader that asked went without asking again: the next one starts anew
    if (reveal_.armed && !reveal_.holding && scene_frame && ++reveal_.scene_frames > kNoFaderFrames)
    {
        logf("pipelines: scene %s shown without a fade-in asking for it: nothing held", scene_.c_str());
        reveal_.armed = false;
        start_measure(now);
    }
    if (measure_.active)
    {
        measure_.frame_ms.push_back(std::chrono::duration<float, std::milli>(now - measure_.last).count());
        const uint32_t waiting = bgfx::getVkLastWaitingFrame();
        if (waiting != measure_.last_waiting)
            ++measure_.incomplete_frames, measure_.incomplete_ms += measure_.frame_ms.back();
        measure_.last_waiting = waiting;
        measure_.last = now;
        if (now - measure_.started >= kMeasureWindow)
            end_measure("measured");
    }
}

void Context::RegisterPipelineProgram(const char *vertex_shader, const char *fragment_shader, uint16_t program)
{
    PipelineSets::instance().register_program(vertex_shader, fragment_shader, bgfx::ProgramHandle{program});
}

void Context::UnregisterPipelineProgram(uint16_t program)
{
    PipelineSets::instance().unregister_program(bgfx::ProgramHandle{program});
}

} // namespace storm::shim

namespace storm::render
{
void BeginScenePipelines(const char *scene)
{
    shim::PipelineSets::instance().begin_scene(scene);
}

void RegisterPipelineProgram(const char *vertex_shader, const char *fragment_shader, bgfx::ProgramHandle program)
{
    shim::PipelineSets::instance().register_program(vertex_shader, fragment_shader, program);
}

void UnregisterPipelineProgram(bgfx::ProgramHandle program)
{
    shim::PipelineSets::instance().unregister_program(program);
}

bool HoldSceneReveal()
{
    return shim::PipelineSets::instance().hold_reveal();
}
} // namespace storm::render
