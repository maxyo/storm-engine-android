#include "storm_ui/ui.h"

#include "storm_ui/render_backend.h"

#include "components.h"
#include "touch_motion.h"
#include "elements.h"
#include "frame_renderer.h"
#include "hud/hud.h"
#include "screens/screen.h"
#include "storm_ui/interface_model.h"

#include <RmlUi/Core.h>
#include <plutovg.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <unordered_map>

namespace storm::ui
{

namespace
{

class DirectorySource final : public FileSource
{
  public:
    explicit DirectorySource(std::string root) : root_(std::move(root))
    {
    }
    bool Read(const std::string &path, std::vector<uint8_t> &out) override
    {
        if (path.find("..") != std::string::npos)
            return false;
        std::ifstream file(root_ + "/" + path, std::ios::binary);
        if (!file)
            return false;
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

  private:
    std::string root_;
};

class PackFiles final : public Rml::FileInterface
{
  public:
    explicit PackFiles(FileSource &source) : source_(source)
    {
    }

    Rml::FileHandle Open(const Rml::String &path) override
    {
        auto file = std::make_unique<Opened>();
        if (!source_.Read(Normalise(path), file->bytes))
            return 0;
        return reinterpret_cast<Rml::FileHandle>(file.release());
    }
    void Close(Rml::FileHandle handle) override
    {
        delete reinterpret_cast<Opened *>(handle);
    }
    size_t Read(void *buffer, size_t size, Rml::FileHandle handle) override
    {
        auto *file = reinterpret_cast<Opened *>(handle);
        const size_t count = std::min(size, file->bytes.size() - file->position);
        std::memcpy(buffer, file->bytes.data() + file->position, count);
        file->position += count;
        return count;
    }
    bool Seek(Rml::FileHandle handle, long offset, int origin) override
    {
        auto *file = reinterpret_cast<Opened *>(handle);
        const long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? static_cast<long>(file->position) : static_cast<long>(file->bytes.size());
        const long target = base + offset;
        if (target < 0 || static_cast<size_t>(target) > file->bytes.size())
            return false;
        file->position = static_cast<size_t>(target);
        return true;
    }
    size_t Tell(Rml::FileHandle handle) override
    {
        return reinterpret_cast<Opened *>(handle)->position;
    }

    // Collapses "a/../b" so that documents can link styles relative to themselves.
    static std::string Normalise(const std::string &path)
    {
        std::vector<std::string> parts;
        size_t at = 0;
        while (at <= path.size())
        {
            size_t end = path.find('/', at);
            if (end == std::string::npos)
                end = path.size();
            const std::string part = path.substr(at, end - at);
            if (part == "..")
            {
                if (!parts.empty())
                    parts.pop_back();
            }
            else if (!part.empty() && part != ".")
                parts.push_back(part);
            at = end + 1;
        }
        std::string out;
        for (const std::string &part : parts)
            out += (out.empty() ? "" : "/") + part;
        return out;
    }

  private:
    struct Opened
    {
        std::vector<uint8_t> bytes;
        size_t position = 0;
    };
    FileSource &source_;
};

class System final : public Rml::SystemInterface
{
  public:
    std::function<double()> clock;

    double GetElapsedTime() override
    {
        if (clock)
            return clock();
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        return duration<double>(steady_clock::now() - start).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
#if defined(__ANDROID__)
        const int priority = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? ANDROID_LOG_ERROR
                             : type == Rml::Log::LT_WARNING                            ? ANDROID_LOG_WARN
                                                                                       : ANDROID_LOG_INFO;
        __android_log_print(priority, "StormUi", "%s", message.c_str());
#else
        std::fprintf(stderr, "[ui %d] %s\n", static_cast<int>(type), message.c_str());
#endif
        return true;
    }
};

bool ReadJson(FileSource &files, const std::string &path, Json &out, std::string *error)
{
    std::vector<uint8_t> bytes;
    if (!files.Read(path, bytes))
    {
        if (error)
            *error = "UI pack lacks " + path;
        return false;
    }
    out = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (out.is_discarded())
    {
        if (error)
            *error = "UI pack: malformed " + path;
        return false;
    }
    return true;
}

// Every document of the context: counts its layout passes (FrameStats::laid_out), so that a
// re-layout of plain elements (not only of components) makes Prepare draw a frame.
class CountedDocument final : public Rml::ElementDocument
{
  public:
    CountedDocument(const Rml::String &tag, FrameStats &stats) : Rml::ElementDocument(tag), stats_(stats)
    {
    }

  protected:
    void OnLayoutStart() override // patches/rmlui-document-layout-start.patch
    {
        layout_started_ = std::chrono::steady_clock::now();
    }
    void OnLayout() override
    {
        Rml::ElementDocument::OnLayout();
        ++stats_.laid_out;
        ++stats_.layout_passes;
        stats_.layout_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - layout_started_).count();
    }

  private:
    FrameStats &stats_;
    std::chrono::steady_clock::time_point layout_started_;
};

class CountedDocumentInstancer final : public Rml::ElementInstancer
{
  public:
    explicit CountedDocumentInstancer(FrameStats &stats) : stats_(stats)
    {
    }
    Rml::ElementPtr InstanceElement(Rml::Element *, const Rml::String &tag, const Rml::XMLAttributes &) override
    {
        return Rml::ElementPtr(new CountedDocument(tag, stats_));
    }
    void ReleaseElement(Rml::Element *element) override
    {
        delete element;
    }

  private:
    FrameStats &stats_;
};

double Milliseconds(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// The one live host of this process (Host::Create).
std::atomic<bool> g_host_alive{false};

class HostImpl final : public Host, public Services, public screens::Surface, public hud::Mount
{
  public:
    bool owns_process_slot = false;

    ~HostImpl() override
    {
        captures_.clear();
        ticking_.clear();
        router_.reset();
        hud_.reset();
        if (context_)
        {
            context_->UnloadAllDocuments();
            Rml::RemoveContext(context_->GetName());
        }
        ShutdownGameImages();
        SetGameImageRenderer(nullptr);
        ReleaseIconTextures();
        ReleaseDrawTextures();
        if (initialised_)
            Rml::Shutdown();
        registry_.reset();
        if (owns_process_slot)
            g_host_alive = false;
    }

    bool Start(const Config &config, std::string *error)
    {
        config_ = config;
        system_.clock = config.clock;
        if (!config.files)
        {
            if (error)
                *error = "no UI pack";
            return false;
        }
        backend_ = config.backend;
        if (!backend_)
        {
            if (error)
                *error = "no render backend given";
            return false;
        }
        files_ = std::make_unique<PackFiles>(*config.files);
        Rml::SetSystemInterface(&system_);
        Rml::SetFileInterface(files_.get());
        renderer_ = std::make_unique<FrameRenderer>(backend_->Interface(), counters_);
        renderer_->Record(config.verify_unchanged);
        Rml::SetRenderInterface(renderer_.get());
        if (!Rml::Initialise())
        {
            if (error)
                *error = "RmlUi failed to start";
            return false;
        }
        initialised_ = true;
        document_instancer_ = std::make_unique<CountedDocumentInstancer>(counters_);
        Rml::Factory::RegisterElementInstancer("body", document_instancer_.get());

        Json pack, components, icons;
        if (!ReadJson(*config.files, "pack.json", pack, error) ||
            !ReadJson(*config.files, pack.value("components", std::string("components.json")), components, error) ||
            !ReadJson(*config.files, pack.value("icons", std::string("icons.json")), icons, error))
            return false;
        settings_ = pack.value("settings", Json::object());
        RegisterBuiltinBehaviors();
        RegisterDrawElements();
        RegisterIconElement();
        RegisterGameImageElement();
        LoadIconPack(icons);
        registry_ = std::make_unique<ComponentRegistry>(*this);
        registry_->Load(components);
        SetGameImageRenderer(backend_);
        SetGameImageRoot(config.game_root);

        bool fallback = true;
        const Json fonts = pack.value("fonts", Json::array());
        for (const Json &font : fonts)
        {
            if (!Rml::LoadFontFace(font.get<std::string>(), fallback))
                Rml::Log::Message(Rml::Log::LT_ERROR, "Font %s failed to load", font.get<std::string>().c_str());
            fallback = false;
        }

        backend_->SetViewport(config.width, config.height);
        context_ = Rml::CreateContext("storm-ui", {config.width, config.height});
        if (!context_)
        {
            if (error)
                *error = "RmlUi context failed";
            return false;
        }
        context_->SetDensityIndependentPixelRatio(config.density);
        // UI strings: one table per shipped language, chosen by the game language.
        const Json string_files = pack.value("strings", Json::object());
        for (const auto &entry : string_files.items())
        {
            Json table;
            if (!ReadJson(*config.files, entry.value().get<std::string>(), table, error))
                return false;
            strings_[entry.key()] = std::move(table);
        }
        SetTemplateStrings([this](const std::string &key) { return Text(key); });
        screen_path_ = pack.value("screen", std::string());
        // The HUD: its document and where the elements of every scene sit. A pack that
        // names a layout must hold a readable one; a pack without a HUD (none shipped
        // today) answers every HUD state with an error.
        hud_path_ = pack.value("hud", std::string());
        const std::string layout_path = pack.value("hudLayout", std::string());
        if (!layout_path.empty())
        {
            Json layout;
            if (!ReadJson(*config.files, layout_path, layout, error) || !hud::ParseLayout(layout, layout_, error))
                return false;
        }
        hud_ = std::make_unique<hud::Hud>(*this);
        router_ = std::make_unique<screens::Router>(*this);
        router_->AttachHud(hud_.get());
        overlay_path_ = pack.value("overlay", std::string());
        if (!overlay_path_.empty())
        {
            overlay_ = context_->LoadDocument(overlay_path_);
            if (overlay_)
                overlay_->Show();
        }
        return true;
    }

    // --- Host ---------------------------------------------------------------------

    bool LoadDocument(const std::string &path) override
    {
        game_interface_ = false;
        router_->CloseInterface();
        const bool opened = OpenDocument(path);
        PublishTouchMap();
        return opened;
    }

    bool OpenDocument(const std::string &path)
    {
        changed_ = true;
        captures_.clear();
        screen_root_ = nullptr;
        if (document_)
        {
            document_->Close();
            document_ = nullptr;
        }
        document_ = context_->LoadDocument(path);
        if (!document_)
            return false;
        ReportUnknownComponents(document_);
        document_->Show();
        if (overlay_)
            overlay_->PullToFront();
        return true;
    }

    void Resize(int width, int height, float density) override
    {
        changed_ = true;
        config_.width = width;
        config_.height = height;
        config_.density = density;
        backend_->SetViewport(width, height);
        context_->SetDimensions({width, height});
        context_->SetDensityIndependentPixelRatio(density);
    }

    void SetCutouts(const std::vector<Cutout> &cutouts) override
    {
        if (cutouts == cutouts_)
            return;
        cutouts_ = cutouts;
        changed_ = true;
        cutouts_changed_ = true;
        PublishTouchMap();
    }

    void SetSafeArea(float left, float top, float right, float bottom) override
    {
        const Json safe = {{"left", left}, {"top", top}, {"right", right}, {"bottom", bottom}};
        if (safe == safe_)
            return;
        safe_ = safe;
        changed_ = true;
        // Rebuild every component: the insets reach the templates through their scope.
        std::vector<Rml::Element *> pending = {document_, overlay_, hud_document_};
        while (!pending.empty())
        {
            Rml::Element *element = pending.back();
            pending.pop_back();
            if (!element)
                continue;
            if (auto *component = dynamic_cast<ComponentElement *>(element))
                component->SetProps(Json::object());
            for (int i = 0; i < element->GetNumChildren(); ++i)
                pending.push_back(element->GetChild(i));
        }
        // The host's toast stack keeps clear of the cutout too (8 dp: its offset in base.rcss).
        if (overlay_)
            if (Rml::Element *stack = overlay_->GetElementById("toasts"))
            {
                stack->SetProperty(Rml::PropertyId::Left, Rml::Property(left, Rml::Unit::DP));
                stack->SetProperty(Rml::PropertyId::Right, Rml::Property(right, Rml::Unit::DP));
                stack->SetProperty(Rml::PropertyId::Top, Rml::Property(8.0f + top, Rml::Unit::DP));
            }
    }

    void SetGameRoot(const std::string &game_root) override
    {
        changed_ = true;
        config_.game_root = game_root;
        SetGameImageRoot(game_root);
    }

    using Host::Touch;

    void Touch(TouchPhase phase, int pointer, float x, float y, double time) override
    {
        changed_ = true;
        // A pointer that went down on what is not laid out yet (an interface whose model is
        // still to come, a sheet opened since the last frame) waits for that layout, with the
        // rest of its gesture (ReleaseHeld).
        if (phase == TouchPhase::Down ? Holding() : held_pointers_.count(pointer) > 0)
        {
            if (phase == TouchPhase::Down)
            {
                held_pointers_.insert(pointer);
                Log("touch held for " + std::string(opening_ != Opening::None ? "an interface being opened" : between_ ? "what comes after the transition" : "the next layout") +
                    ": " + RoutingFacts());
            }
            held_.push_back({phase, pointer, x, y, time});
            return;
        }
        Apply(phase, pointer, x, y, time);
    }

    void Apply(TouchPhase phase, int pointer, float x, float y, double time)
    {
        const Rml::Vector2f at = {x, y};
        // A tap that travels this far is a drag (a scroll), no longer a press.
        const float slop = 10.0f * config_.density;
        if (phase == TouchPhase::Down)
        {
            Capture capture;
            capture.pointer.id = pointer;
            capture.pointer.position = capture.pointer.start = at;
            capture.pointer.time = time;
            capture.pointer.target = context_->GetElementAtPoint(at);
            // The HUD takes input only while the player can act (Presentation::live).
            if (capture.pointer.target && capture.pointer.target->GetOwnerDocument() == hud_document_ && !hud_->Interactive())
                return;
            ComponentElement *component = InteractiveComponentAt(capture.pointer.target, capture.pointer);
            // Claimed for something that is gone when it arrives (a load ended in gameplay in
            // between): nothing takes it, said once per gesture.
            if (!component && !InterfaceActive() && !hud_->Interactive())
                Log("touch reached nothing of the UI: " + RoutingFacts());
            if (component && component->PointerExclusive(capture.pointer))
            {
                capture.exclusive = true;
                capture.component = component->GetObserverPtr();
                component->PointerDown(capture.pointer);
            }
            else
            {
                if (scroll_pointer_ < 0)
                {
                    scroll_pointer_ = pointer;
                    scroll_.Down(capture.pointer.target, at, time, config_.density, slop);
                }
                // A finger that stops a list in motion drags it: it presses nothing.
                const bool caught = pointer == scroll_pointer_ && scroll_.Caught();
                if (component && !caught && component->PointerDown(capture.pointer))
                    capture.component = component->GetObserverPtr();
            }
            captures_[pointer] = std::move(capture);
            return;
        }

        if (phase == TouchPhase::Up) // the end of a tap: what a screen switch answers (ReportSwitch)
            last_tap_ = Clock::now() - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(std::max(0.0, Now() - time)));
        const auto found = captures_.find(pointer);
        if (found == captures_.end())
            return;
        Capture &capture = found->second;
        capture.pointer.position = at;
        capture.pointer.time = time;
        auto *component = static_cast<ComponentElement *>(capture.component.get());
        const bool ended = phase == TouchPhase::Up || phase == TouchPhase::Cancel;

        if (capture.exclusive)
        {
            if (component)
            {
                if (ended)
                    component->PointerUp(capture.pointer, phase == TouchPhase::Cancel);
                else
                    component->PointerMove(capture.pointer);
            }
        }
        else
        {
            const bool scrolling = pointer == scroll_pointer_ && phase == TouchPhase::Move && scroll_.Move(at, time);
            if (component)
            {
                if (ended)
                    component->PointerUp(capture.pointer, phase == TouchPhase::Cancel);
                else if (scrolling || (at - capture.pointer.start).SquaredMagnitude() > slop * slop)
                {
                    component->PointerUp(capture.pointer, true); // the press is withdrawn
                    capture.component = {};
                }
                else
                    component->PointerMove(capture.pointer);
            }
            if (pointer == scroll_pointer_ && ended)
            {
                scroll_.Up(time, phase == TouchPhase::Cancel);
                scroll_pointer_ = -1;
            }
        }
        if (ended)
            captures_.erase(found);
    }

    bool Prepare(double &delay) override
    {
        const auto started = std::chrono::steady_clock::now();
        const double now = system_.GetElapsedTime();
        // A list in motion after the finger left: placed where it is at this frame's time.
        if (scroll_.Update(now))
            changed_ = true;
        const std::vector<Rml::ObserverPtr<Rml::Element>> ticking = ticking_;
        for (const auto &entry : ticking)
            if (auto *component = static_cast<ComponentElement *>(entry.get()))
                component->Tick(now);
        ExpireToasts(now);
        const bool images = GameImagesChanged();
        const double routing = std::min(router_->Tick(), hud_->Tick());

        UpdateContext();
        // Scroll edges move only with a layout, a touch or a scroll in motion: the walk over
        // every element is skipped on frames without any of them.
        const bool moved = changed_ || counters_.laid_out > 0 || context_->GetNextUpdateDelay() <= 0.0;
        const bool edges = moved && ScrollEdgesChanged(context_->GetRootElement());
        const bool clamps = TextClampsChanged() || edges;
        // What fell under a cutout with this layout moves out of it (SetCutouts).
        const bool cutouts = moved && AvoidCutouts();
        if (clamps || cutouts) // clamped text was re-cut for a new width, a scrolling edge changed, content moved off a cutout
            UpdateContext();
        delay = context_->GetNextUpdateDelay();
        // What makes the shown frame out of date: input and host calls (changed_), rebuilt
        // or re-laid-out components, ticking behaviours, animations and transitions, game
        // pictures that arrived, re-cut text.
        if (images || clamps || cutouts || !ticking_.empty() || delay <= 0.0 || counters_.rebuilds > 0 || counters_.laid_out > 0)
            changed_ = true;
        if (changed_)
            delay = 0.0;
        else if (GameImagesPending())
            delay = std::min(delay, 0.05); // a decoded picture is shown on the next frame
        delay = std::min(delay, routing);
        for (const Toast_ &toast : toasts_)
            if (toast.held < 0.0)
                delay = std::min(delay, std::max(0.0, (toast.leaving >= 0.0 ? toast.leaving + ToastLeaveSeconds() : toast.until) - now));
        TakeGameImageCost(counters_.picture_ms, counters_.pictures);
        counters_.update_ms += Milliseconds(started, std::chrono::steady_clock::now());
        const bool draw = changed_ || !drawn_;
        if (!draw)
        {
            if (switch_.drawn)
                ReportSwitch(true); // nothing more to draw: the screen has settled
            // The frame on the screen still shows everything: acknowledgements that wait
            // for a drawn frame are given for it.
            hud_->Drawn();
            PublishTouchMap(true);
            ReleaseHeld();
        }
        return draw;
    }

    // Context::Update, its time split for FrameStats: layout (the documents' own hooks), component
    // rebuilds and game pictures (theirs); the rest is styles.
    void UpdateContext()
    {
        TakeGameImageCost(counters_.picture_ms, counters_.pictures);
        const double rebuilt = counters_.rebuild_ms, acquired = counters_.picture_ms, laid_out = counters_.layout_ms;
        const auto started = std::chrono::steady_clock::now();
        context_->Update();
        const double ms = Milliseconds(started, std::chrono::steady_clock::now());
        TakeGameImageCost(counters_.picture_ms, counters_.pictures); // acquired in their elements' update
        counters_.style_ms +=
            std::max(0.0, ms - (counters_.rebuild_ms - rebuilt) - (counters_.picture_ms - acquired) - (counters_.layout_ms - laid_out));
    }

    double Frame() override
    {
        double delay = 0.0;
        const bool predicted = Prepare(delay);
        const auto updated = std::chrono::steady_clock::now();
        renderer_->BeginFrame();
        backend_->BeginFrame(true);
        context_->Render();
        backend_->EndFrame();
        renderer_->EndFrame();
        counters_.mispredicted = !predicted && config_.verify_unchanged && renderer_->FrameHash() != shown_hash_;
        if (counters_.mispredicted)
            Error("UI frame predicted unchanged drew something else: a change that does not ask for a frame; " + renderer_->Difference());
        shown_hash_ = renderer_->FrameHash();
        // Textures made or handed out while drawing (a font atlas that grew for new glyphs, a
        // box shadow made again) make RmlUi re-encode meshes and re-apply its clip state in
        // the next frame: that frame is drawn too.
        changed_ = counters_.textures > 0 || counters_.textures_shared > 0;
        drawn_ = true;
        counters_.render_ms = Milliseconds(updated, std::chrono::steady_clock::now());
        counters_.number = last_frame_.number + 1;
        ReportSwitch(false);
        last_frame_ = counters_;
        counters_ = FrameStats(); // what happens until the next frame counts for it
        hud_->Drawn();
        PublishTouchMap(true);
        ReleaseHeld();
        return delay;
    }

    FrameStats LastFrameStats() const override
    {
        return last_frame_;
    }

    void Toast(const std::string &text, const std::string &tone) override
    {
        if (hud_->Notice(text, tone)) // over the game, events go to the HUD's top-centre log
            return;
        OverlayToast(text, tone, hud::ToastSeconds(text));
    }

    // On the overlay whatever is shown: over the game, over a screen, and through the change
    // from one to the other (a failed load: the HUD's log would vanish with the menu that comes back).
    void Alert(const std::string &text) override
    {
        OverlayToast(text, "danger", Setting("alertMs", 6000.0) / 1000.0);
    }

    void GameMessage(const std::string &text, const std::string &target) override
    {
        if (hud_->Notice(text, "info", target)) // over the game: the HUD's log, a tap opens the target
            return;
        OverlayToast(text, "info", hud::ToastSeconds(text)); // over a screen: only shown
    }

    void AlertString(const std::string &key) override
    {
        Alert(Text(key));
    }

    // `seconds`: how long it stays (hud::ToastSeconds for a toast, longer for an alert); a repeat
    // starts it again, a finger on it stops it (behaviour toast: hold / release).
    void OverlayToast(const std::string &text, const std::string &tone, double seconds)
    {
        if (!overlay_ || text.empty())
            return;
        // One place for every toast: the top edge (the HUD's event lines are there too).
        Rml::Element *stack = overlay_->GetElementById("toasts");
        if (!stack)
        {
            Rml::Log::Message(Rml::Log::LT_ERROR, "overlay.rml has no #toasts stack: toast dropped: %s", text.c_str());
            return;
        }
        const double until = system_.GetElapsedTime() + seconds;
        for (Toast_ &shown : toasts_)
            if (shown.text == text && shown.element && shown.leaving < 0.0)
            {
                shown.element->SetAttribute("text", hud::RepeatedText(text, ++shown.count));
                if (shown.held >= 0.0)
                    shown.held = seconds;
                else
                    shown.until = until;
                changed_ = true;
                return;
            }
        // At most three shown: the oldest shown one starts to leave.
        if (std::count_if(toasts_.begin(), toasts_.end(), [](const Toast_ &toast) { return toast.leaving < 0.0; }) >= 3)
            for (Toast_ &shown : toasts_)
                if (shown.leaving < 0.0)
                {
                    Leave(shown, system_.GetElapsedTime());
                    break;
                }
        Rml::ElementPtr notice = overlay_->CreateElement("c-notice");
        notice->SetAttribute("variant", "toast");
        notice->SetAttribute("place", "none");
        notice->SetAttribute("tone", tone.empty() ? "info" : tone);
        notice->SetAttribute("text", text);
        notice->SetAttribute("compact", "");
        changed_ = true;
        Toast_ toast;
        toast.element = stack->AppendChild(std::move(notice))->GetObserverPtr();
        toast.until = until;
        toast.text = text;
        toasts_.push_back(std::move(toast));
    }

    // A finger on an overlay toast (behaviour toast): its timer stops while held.
    void ToastTouched(ComponentElement &source, const std::string &verb)
    {
        const double now = system_.GetElapsedTime();
        for (Toast_ &toast : toasts_)
            if (toast.element.get() == &source && toast.leaving < 0.0)
            {
                if (verb == "hold" && toast.held < 0.0)
                    toast.held = std::max(0.0, toast.until - now);
                else if (verb == "release" && toast.held >= 0.0)
                {
                    toast.until = now + toast.held;
                    toast.held = -1.0;
                    changed_ = true;
                }
            }
    }

    bool SetProps(const std::string &element_id, const std::string &json) override
    {
        changed_ = true;
        Rml::Element *element = document_ ? document_->GetElementById(element_id) : nullptr;
        auto *component = dynamic_cast<ComponentElement *>(element);
        const Json props = Json::parse(json, nullptr, false);
        if (!component || !props.is_object())
            return false;
        component->SetProps(props);
        return true;
    }

    void ScrollTo(const std::string &element_id) override
    {
        changed_ = true;
        if (Rml::Element *element = document_ ? document_->GetElementById(element_id) : nullptr)
            element->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Start));
    }

    // --- game interfaces -----------------------------------------------------------

    bool SetInterface(const std::string &json) override
    {
        InterfaceModel model;
        std::string error;
        if (!ParseInterfaceModel(json, model, &error))
        {
            Error(error);
            return false;
        }
        SetInterface(model);
        return true;
    }

    void SetInterface(const InterfaceModel &model) override
    {
        const auto started = std::chrono::steady_clock::now();
        if (model.opening != switch_.opening)
        {
            if (!switch_.open)
                switch_.Begin(started, false);
            switch_.opening = model.opening;
            switch_.screen = model.screen;
            switch_.modelled = started;
        }
        game_interface_ = true;
        router_->SetInterface(model);
        counters_.apply_ms += Milliseconds(started, std::chrono::steady_clock::now());
        if (opening_ == Opening::AwaitModel)
            opening_ = Opening::AwaitFrame;
        PublishTouchMap();
    }

    void CloseInterface() override
    {
        switch_.open = false;
        game_interface_ = false;
        router_->CloseInterface();
        if (opening_ != Opening::None)
        {
            opening_ = Opening::None;
            if (!held_.empty())
                Log(std::to_string(held_.size()) + " touch events dropped: the interface closed before it was shown; " + RoutingFacts());
            held_.clear();
            held_pointers_.clear();
        }
        PublishTouchMap();
    }

    void ExpectInterface() override
    {
        if (!switch_.open)
            switch_.Begin(std::chrono::steady_clock::now(), true);
        changed_ = true;
        opening_ = Opening::AwaitModel;
        PublishTouchMap();
    }

    bool SetState(const std::string &channel, const std::string &json) override
    {
        const Json state = Json::parse(json, nullptr, false);
        if (state.is_discarded())
        {
            Error("state channel " + channel + ": malformed JSON");
            return false;
        }
        if (channel == "video")
            return SetVideo(state);
        const auto started = std::chrono::steady_clock::now();
        try
        {
            router_->SetState(channel, state);
            counters_.apply_ms += Milliseconds(started, std::chrono::steady_clock::now());
        }
        catch (const Json::exception &error)
        {
            Error("state channel " + channel + ": " + error.what());
            return false;
        }
        PublishTouchMap(); // a sheet the state opened owns the touches from now
        return true;
    }

    void SubmitText(const std::string &token, const std::string &text, bool accepted) override
    {
        changed_ = true;
        router_->SubmitText(token, text, accepted);
    }

    bool InterfaceActive() const override
    {
        // hud::Mount too: a plain document covers the game as well; a full-screen video covers both
        return !video_ && (router_->Active() || document_ != nullptr);
    }

    // Channel "video" ({"fullscreen": true} or null, engine aviplayer.cpp): while a game video
    // plays over the whole screen the interface screen is hidden and takes no touch, so the
    // video is seen and a tap reaches the game (the player's tap-to-skip). The screen itself
    // stays mounted: the script that started the video may leave its interface open (the main
    // menu's «Создатели» exits without a release) and it returns as it was.
    bool SetVideo(const Json &state)
    {
        if (!state.is_null() && !(state.is_object() && state.contains("fullscreen") && state["fullscreen"].is_boolean()))
        {
            Error("state channel video: expected {fullscreen: bool} or null, got " + state.dump());
            return false;
        }
        const bool video = state.is_object() && state["fullscreen"].get<bool>();
        if (video == video_)
            return true;
        video_ = video;
        if (video_)
            CancelInput();
        ShowDocument();
        PublishTouchMap();
        return true;
    }

    void ShowDocument()
    {
        changed_ = true;
        if (document_)
            video_ ? document_->Hide() : document_->Show();
    }

    bool WantsTouch(float x, float y) const override
    {
        const std::lock_guard<std::mutex> lock(touch_mutex_);
        if (touch_map_.everywhere)
            return true;
        for (const Rml::Rectanglef &region : touch_map_.regions)
            if (region.Contains({x, y}))
                return true;
        return false;
    }

    void CancelInput() override
    {
        changed_ = true;
        if (!held_.empty())
            Log(std::to_string(held_.size()) + " touch events dropped: the platform cancelled the input while they waited; " + RoutingFacts());
        held_.clear();
        held_pointers_.clear();
        std::vector<std::pair<int, Rml::Vector2f>> held;
        for (const auto &capture : captures_)
            held.emplace_back(capture.first, capture.second.pointer.position);
        for (const auto &pointer : held)
            Touch(TouchPhase::Cancel, pointer.first, pointer.second.x, pointer.second.y);
        hud_->Released();
    }

    bool WantsBack() const override
    {
        const std::lock_guard<std::mutex> lock(touch_mutex_);
        return touch_map_.back;
    }

    void Back() override
    {
        changed_ = true;
        bool hud_sheet = false;
        {
            const std::lock_guard<std::mutex> lock(touch_mutex_);
            hud_sheet = touch_map_.back;
        }
        if (router_->WantsBack()) // a screen's own modal (the dialogue's answer editor)
            router_->Back();
        else if (hud_sheet || !router_->Active())
            hud_->Back();
        else
            router_->Back();
    }

    // --- hud::Mount ---------------------------------------------------------------

    bool ShowHud(const hud::View &view) override
    {
        const std::string problem = hud::CheckView(layout_, view);
        if (!problem.empty())
        {
            Error(problem);
            return false;
        }
        if (!hud_document_)
        {
            hud_document_ = hud_path_.empty() ? nullptr : context_->LoadDocument(hud_path_);
            if (!hud_document_)
            {
                Error("UI pack has no HUD document");
                return false;
            }
            if (overlay_)
                overlay_->PullToFront();
        }
        auto *root = dynamic_cast<ComponentElement *>(hud_document_->GetElementById("hud"));
        if (root && !ComponentSlot(*root))
            root->RebuildNow();
        Rml::Element *layer = root ? ComponentSlot(*root) : nullptr;
        if (!layer)
        {
            Error("UI pack: the HUD document has no <s-hud id=\"hud\"> layer");
            return false;
        }
        if (!hud_document_->IsVisible())
        {
            changed_ = true;
            hud_document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        }
        // Elements are patched in place: one that both the old and the new view have stays
        // mounted (a held stick survives explore ↔ combat), only its place and props follow.
        const std::vector<hud::LayoutElement> &elements = layout_.at(view.layout);
        std::unordered_map<std::string, std::string> wanted; // id → what it must have been created as
        for (const hud::LayoutElement &element : elements)
            if (view.elements.contains(element.id))
            {
                std::string made = element.component + "\n" + element.behavior + "\n" + element.data.dump();
                for (const hud::LayoutElement &child : element.children)
                    if (view.elements.contains(child.id))
                        made += "\n" + child.id + " " + child.component;
                wanted[element.id] = std::move(made);
            }
        for (int i = layer->GetNumChildren(); i-- > 0;)
        {
            Rml::Element *mounted = layer->GetChild(i);
            const auto keep = wanted.find(mounted->GetId());
            if (mounted->GetId() == "sheet" || mounted->GetId() == "overlay" || (keep != wanted.end() && hud_made_[mounted->GetId()] == keep->second))
                continue;
            RemoveHudElement(layer, mounted);
        }
        bool complete = true;
        Rml::Element *before = layer->GetElementById("overlay"); // layout order: later elements are drawn on top
        if (!before)
            before = layer->GetElementById("sheet");
        for (size_t i = elements.size(); i-- > 0;)
        {
            const hud::LayoutElement &element = elements[i];
            if (!wanted.count(element.id))
                continue;
            Rml::Element *mounted = hud_document_->GetElementById(element.id);
            if (!mounted)
            {
                Rml::ElementPtr created = CreateHudElement(element);
                for (const hud::LayoutElement &child : element.children)
                    if (view.elements.contains(child.id))
                    {
                        Rml::ElementPtr inner = CreateHudElement(child);
                        inner->SetAttribute("style", "width: 100%;");
                        created->AppendChild(std::move(inner));
                    }
                mounted = before ? layer->InsertBefore(std::move(created), before) : layer->AppendChild(std::move(created));
                hud_made_[element.id] = wanted[element.id];
            }
            before = mounted;
            if (mounted->GetAttribute<Rml::String>("style", "") != element.rcss)
            {
                changed_ = true;
                mounted->SetAttribute("style", element.rcss);
            }
            complete = SetHudProps(element, view) && complete;
            for (const hud::LayoutElement &child : element.children)
                if (view.elements.contains(child.id))
                    complete = SetHudProps(child, view) && complete;
            const bool dim = std::find(view.dimmed.begin(), view.dimmed.end(), element.id) != view.dimmed.end();
            if (mounted->IsClassSet("dim") != dim)
            {
                changed_ = true;
                mounted->SetClass("dim", dim);
            }
        }
        // The overlay (the spyglass card) lies over the scene's elements and under the sheet.
        Rml::Element *overlay = hud_document_->GetElementById("overlay");
        if (overlay && overlay->GetTagName() != view.overlay)
        {
            RemoveHudElement(layer, overlay);
            overlay = nullptr;
        }
        if (!view.overlay.empty())
        {
            if (!overlay)
            {
                Rml::ElementPtr created = hud_document_->CreateElement(view.overlay);
                created->SetId("overlay");
                Rml::Element *above = layer->GetElementById("sheet");
                overlay = above ? layer->InsertBefore(std::move(created), above) : layer->AppendChild(std::move(created));
            }
            if (auto *component = dynamic_cast<ComponentElement *>(overlay))
                component->SetProps(view.overlay_props);
            else
            {
                Error("UI pack has no HUD overlay " + view.overlay);
                complete = false;
            }
        }
        // The sheet lies over everything else of the scene.
        Rml::Element *sheet = hud_document_->GetElementById("sheet");
        if (sheet && sheet->GetTagName() != view.sheet)
        {
            RemoveHudElement(layer, sheet);
            sheet = nullptr;
        }
        if (!view.sheet.empty())
        {
            if (!sheet)
            {
                changed_ = true;
                Rml::ElementPtr created = hud_document_->CreateElement(view.sheet);
                created->SetId("sheet");
                sheet = layer->AppendChild(std::move(created));
            }
            if (auto *component = dynamic_cast<ComponentElement *>(sheet))
            {
                Json &shown = hud_props_["sheet"]; // forgotten with the sheet (RemoveHudElement)
                if (shown != view.sheet_props)
                {
                    component->SetProps(view.sheet_props);
                    shown = view.sheet_props;
                }
            }
            else
            {
                Error("UI pack has no HUD sheet " + view.sheet);
                complete = false;
            }
        }
        return complete;
    }

    bool Measure(const std::string &id, const std::string &selector, hud::Placement &out) const override
    {
        Rml::Element *element = hud_document_ && hud_document_->IsVisible() ? hud_document_->GetElementById(id) : nullptr;
        if (element && !selector.empty())
            element = element->QuerySelector(selector);
        if (!element || !element->IsVisible(true))
            return false;
        const Rml::Vector2f origin = element->GetAbsoluteOffset(Rml::BoxArea::Border), size = element->GetBox().GetSize(Rml::BoxArea::Border);
        if (size.x <= 0.0f || size.y <= 0.0f)
            return false;
        auto *root = dynamic_cast<ComponentElement *>(hud_document_->GetElementById("hud"));
        Rml::Element *layer = root ? ComponentSlot(*root) : nullptr;
        const Rml::Vector2f corner = layer ? layer->GetAbsoluteOffset(Rml::BoxArea::Border) : Rml::Vector2f(0.0f, 0.0f);
        const Rml::Vector2f extent = layer ? layer->GetBox().GetSize(Rml::BoxArea::Border)
                                           : Rml::Vector2f(static_cast<float>(config_.width), static_cast<float>(config_.height));
        out = {origin.x, origin.y, size.x, size.y, corner.x, corner.y, static_cast<float>(config_.width), static_cast<float>(config_.height), config_.density,
               extent.x, extent.y};
        return true;
    }

    void HideHud() override
    {
        if (!hud_document_)
            return;
        if (hud_document_->IsVisible())
            changed_ = true;
        auto *root = dynamic_cast<ComponentElement *>(hud_document_->GetElementById("hud"));
        if (Rml::Element *layer = root ? ComponentSlot(*root) : nullptr)
            while (layer->GetNumChildren() > 0)
                RemoveHudElement(layer, layer->GetLastChild());
        hud_document_->Hide();
    }

    // --- screens::Surface ---------------------------------------------------------

    void Show(const std::string &tag, const Json &props) override
    {
        changed_ = true;
        if (!screen_root_ && (screen_path_.empty() || !OpenDocument(screen_path_)))
        {
            Error("UI pack has no screen document");
            return;
        }
        screen_root_ = document_;
        Rml::Element *mounted = document_->GetElementById("screen");
        if (!mounted || mounted->GetTagName() != tag)
        {
            if (mounted)
                document_->RemoveChild(mounted);
            Rml::ElementPtr created = document_->CreateElement(tag);
            created->SetId("screen");
            mounted = document_->AppendChild(std::move(created));
        }
        if (auto *component = dynamic_cast<ComponentElement *>(mounted))
            component->SetProps(props);
        else
            Error("UI pack has no screen " + tag);
        if (video_)
            ShowDocument();
        PublishTouchMap();
    }

    void Hide() override
    {
        changed_ = true;
        captures_.clear();
        if (screen_root_ && document_)
        {
            document_->Close();
            document_ = nullptr;
        }
        screen_root_ = nullptr;
        PublishTouchMap();
    }

    void Send(const Action &action) override
    {
        if (config_.on_action)
            config_.on_action(action);
    }

    void RequestText(TextRequest request, const std::string &anchor) override
    {
        if (Rml::Element *element = document_ ? document_->GetElementById(anchor) : nullptr)
        {
            // A field marks the box the text sits in (c-field: data-action="focus"); the
            // editor goes exactly over it, not over the label and error lines.
            if (Rml::Element *box = element->QuerySelector("[data-action=focus]"))
                element = box;
            const Rml::Vector2f origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
            request.x = origin.x;
            request.y = origin.y;
            request.width = size.x;
            request.height = size.y;
        }
        if (config_.on_text_request)
            config_.on_text_request(request);
        else
            router_->SubmitText(request.token, {}, false);
    }

    void Toast(const std::string &text) override
    {
        Toast(text, "info");
    }

    void SetLanguage(const std::string &game_language) override
    {
        static const char *const codes[][2] = {{"russian", "ru"}, {"english", "en"}, {"german", "de"},
                                               {"french", "fr"},  {"spanish", "es"}, {"italian", "it"}};
        std::string code;
        for (const auto &pair : codes)
            if (SameNode(game_language, pair[0]))
                code = pair[1];
        if (!strings_.count(code))
        {
            // A game language the pack has no strings for (a mod, an empty name): English,
            // and said so once per language.
            if (unknown_languages_.insert(game_language).second)
                Error("no UI strings for the game language '" + game_language + "'; using en");
            code = "en";
        }
        language_ = code;
    }

    std::string Text(const std::string &key) override
    {
        const auto table = strings_.find(language_);
        if (table != strings_.end())
        {
            const auto found = table->second.find(key);
            if (found != table->second.end() && found->is_string())
                return found->get<std::string>();
        }
        if (missing_strings_.insert(language_ + ":" + key).second)
            Error("UI string '" + key + "' is missing for language '" + language_ + "'");
        return key;
    }

    void Log(const std::string &line) override
    {
        Rml::Log::Message(Rml::Log::LT_INFO, "%s", line.c_str());
    }

    void Error(const std::string &line) override
    {
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s", line.c_str());
        if (config_.show_errors)
            Toast(line, "danger");
    }

    // --- Services -----------------------------------------------------------------

    double Now() const override
    {
        return const_cast<System &>(system_).GetElapsedTime();
    }
    float Density() const override
    {
        return config_.density;
    }
    Json SafeArea() const override
    {
        return safe_;
    }
    double Setting(const std::string &name, double fallback) const override
    {
        const auto found = settings_.find(name);
        return found != settings_.end() && found->is_number() ? found->get<double>() : fallback;
    }
    void Emit(ComponentElement &source, const std::string &verb, const std::string &value) override
    {
        std::string id;
        for (Rml::Element *element = &source; element && id.empty(); element = element->GetParentNode())
            id = element->GetId();
        if (overlay_ && source.GetOwnerDocument() == overlay_)
        {
            ToastTouched(source, verb); // the overlay holds only the toasts and the tooltip
            return;
        }
        if (hud_document_ && source.GetOwnerDocument() == hud_document_)
        {
            // The HUD: its scene maps the event to engine input.
            hud_->OnEvent({id, verb, value});
            return;
        }
        if (screen_root_)
        {
            // A game interface: its screen maps the event to engine actions.
            router_->OnEvent({id, verb, value});
            return;
        }
        if (!config_.on_action)
            return;
        Action action;
        action.node = id;
        action.verb = verb;
        action.value = value;
        config_.on_action(action);
    }
    void Tooltip(Rml::Element *anchor, const std::string &text, bool below) override
    {
        changed_ = true;
        Rml::Element *tip = overlay_ ? overlay_->GetElementById("tooltip") : nullptr;
        if (!tip)
            return;
        if (!anchor || text.empty())
        {
            tip->SetClass("open", false);
            return;
        }
        const Rml::Vector2f origin = anchor->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = anchor->GetBox().GetSize(Rml::BoxArea::Border);
        const float gap = 6.0f * config_.density;
        const float width = std::min(static_cast<float>(config_.width) - 2.0f * gap, std::max(size.x, 280.0f * config_.density));
        const float left = std::clamp(origin.x, gap, static_cast<float>(config_.width) - gap - width);
        tip->SetInnerRML(EscapeRml(text));
        tip->SetProperty(Rml::PropertyId::Left, Rml::Property(left, Rml::Unit::PX));
        tip->SetProperty(Rml::PropertyId::Width, Rml::Property(width, Rml::Unit::PX));
        if (below)
        {
            tip->SetProperty(Rml::PropertyId::Top, Rml::Property(origin.y + size.y + gap, Rml::Unit::PX));
            tip->RemoveProperty(Rml::PropertyId::Bottom);
        }
        else
        {
            tip->SetProperty(Rml::PropertyId::Bottom, Rml::Property(static_cast<float>(config_.height) - origin.y + gap, Rml::Unit::PX));
            tip->RemoveProperty(Rml::PropertyId::Top);
        }
        tip->SetClass("open", true);
    }
    void RequestTicks(ComponentElement &component, bool enable) override
    {
        const auto found = std::find_if(ticking_.begin(), ticking_.end(), [&](const auto &entry) { return entry.get() == &component; });
        if (enable && found == ticking_.end())
            ticking_.push_back(component.GetObserverPtr());
        else if (!enable && found != ticking_.end())
            ticking_.erase(found);
    }
    void PressStarted(ComponentElement &component) override
    {
        // Game interfaces only (their scripts' click); the HUD and the application screens (the
        // launcher and the downloads, state channels of their own process) have none.
        if (!game_interface_ || !screen_root_ || component.GetOwnerDocument() != screen_root_)
            return;
        Action action;
        action.screen = "sound";
        action.verb = "press";
        Send(action);
    }

    FrameStats &Counters() override
    {
        return counters_;
    }
    void Forget(ComponentElement &component) override
    {
        ticking_.erase(std::remove_if(ticking_.begin(), ticking_.end(), [&](const auto &entry) { return !entry || entry.get() == &component; }),
                       ticking_.end());
    }

  private:
    struct Capture
    {
        Pointer pointer;
        Rml::ObserverPtr<Rml::Element> component;
        bool exclusive = false;
    };
    // Where a pointer going down belongs to the UI; read by the thread that gets touches.
    struct TouchMap
    {
        bool everywhere = false;
        bool back = false;
        std::vector<Rml::Rectanglef> regions; // physical pixels
    };

    // --- HUD mount ---

    Rml::ElementPtr CreateHudElement(const hud::LayoutElement &element)
    {
        changed_ = true;
        Rml::ElementPtr created = hud_document_->CreateElement(element.component);
        created->SetId(element.id);
        if (!element.behavior.empty())
            created->SetAttribute("data-behavior", element.behavior);
        for (const auto &item : element.data.items())
            created->SetAttribute("data-" + item.key(), ToText(item.value()));
        return created;
    }

    // Layout props with the scene's live ones on top; a rebuild only when they changed.
    bool SetHudProps(const hud::LayoutElement &element, const hud::View &view)
    {
        auto *component = dynamic_cast<ComponentElement *>(hud_document_->GetElementById(element.id));
        if (!component)
        {
            Error("UI pack has no component <" + element.component + "> for HUD element " + element.id);
            return false;
        }
        Json props = element.props;
        props.update(view.elements[element.id]);
        Json &shown = hud_props_[element.id];
        if (shown != props)
        {
            Json change = props;
            if (shown.is_object()) // a prop the scene no longer gives goes back to its default
                for (const auto &item : shown.items())
                    if (!props.contains(item.key()))
                        change[item.key()] = nullptr;
            component->SetProps(change);
            shown = std::move(props);
        }
        return true;
    }

    // Takes a HUD element off the screen. A pointer held on it ends first, so that its
    // control reports the release (a stick stops, a held button lets go).
    void RemoveHudElement(Rml::Element *layer, Rml::Element *mounted)
    {
        changed_ = true;
        std::vector<std::pair<int, Rml::Vector2f>> held;
        for (const auto &capture : captures_)
            for (Rml::Element *at = capture.second.component.get(); at; at = at->GetParentNode())
                if (at == mounted)
                {
                    held.emplace_back(capture.first, capture.second.pointer.position);
                    break;
                }
        for (const auto &pointer : held)
            Touch(TouchPhase::Cancel, pointer.first, pointer.second.x, pointer.second.y);
        std::vector<Rml::Element *> inside = {mounted};
        while (!inside.empty())
        {
            Rml::Element *element = inside.back();
            inside.pop_back();
            hud_props_.erase(element->GetId());
            for (int i = 0; i < element->GetNumChildren(); ++i)
                inside.push_back(element->GetChild(i));
        }
        hud_made_.erase(mounted->GetId());
        layer->RemoveChild(mounted);
    }

    // --- display cutouts ------------------------------------------------------------------
    // The layout runs edge to edge. After it, what must stay readable or touchable and falls
    // under a cutout is moved out by the cutout's depth from its screen edge (plus a hair), with
    // a translate: text, icons, pictures and small controls (a component no larger than a
    // quarter of the screen each way, moved whole). Larger components (panels, rows, lists) stay
    // under the cutout; their content is looked at one by one. A transform the pack set itself is
    // not overwritten: that element counts as a container. What scrolls is never moved: the content
    // of a scrolling element passes under the cutout as it goes (a row that stepped aside while it
    // crossed the cutout read as a dent travelling through the list). True when a shift changed.
    bool AvoidCutouts()
    {
        std::unordered_map<Rml::Element *, Rml::Vector2f> next;
        if (!cutouts_.empty())
            for (Rml::ElementDocument *document : {document_, hud_document_, overlay_})
                if (document && document->IsVisible())
                    PlaceOffCutouts(document, next);
        bool changed = cutouts_changed_;
        cutouts_changed_ = false;
        for (auto it = cutout_shifts_.begin(); it != cutout_shifts_.end();)
        {
            Rml::Element *element = it->second.element.get();
            const auto found = element ? next.find(element) : next.end();
            if (!element || found == next.end())
            {
                if (element)
                    element->RemoveProperty(Rml::PropertyId::Transform);
                it = cutout_shifts_.erase(it);
                changed = true;
                continue;
            }
            ++it;
        }
        for (const auto &[element, shift] : next)
        {
            auto &entry = cutout_shifts_[element];
            if (entry.element.get() == element && entry.shift == shift)
                continue;
            entry = {element->GetObserverPtr(), shift};
            element->SetProperty("transform", "translate(" + std::to_string(shift.x) + "px, " + std::to_string(shift.y) + "px)");
            changed = true;
        }
        return changed;
    }

    // How far `box` must move to clear every cutout it overlaps: along the axis of the cutout's
    // nearest screen edge, away from that edge.
    Rml::Vector2f CutoutShift(const Rml::Rectanglef &box) const
    {
        const Rml::Vector2f screen(static_cast<float>(context_->GetDimensions().x), static_cast<float>(context_->GetDimensions().y));
        const float hair = 4.0f * config_.density;
        Rml::Vector2f shift(0.0f, 0.0f);
        for (const Cutout &cut : cutouts_)
        {
            const float right = cut.x + cut.width, bottom = cut.y + cut.height;
            if (box.Right() <= cut.x || box.Left() >= right || box.Bottom() <= cut.y || box.Top() >= bottom)
                continue;
            const float to_left = cut.x, to_right = screen.x - right, to_top = cut.y, to_bottom = screen.y - bottom;
            const float nearest = std::min({to_left, to_right, to_top, to_bottom});
            if (nearest == to_left)
                shift.x = std::max(shift.x, right + hair - box.Left());
            else if (nearest == to_right)
                shift.x = std::min(shift.x, cut.x - hair - box.Right());
            else if (nearest == to_top)
                shift.y = std::max(shift.y, bottom + hair - box.Top());
            else
                shift.y = std::min(shift.y, cut.y - hair - box.Bottom());
        }
        return shift;
    }

    void PlaceOffCutouts(Rml::Element *element, std::unordered_map<Rml::Element *, Rml::Vector2f> &out)
    {
        if (!element->IsVisible(true))
            return;
        const auto scrolls = [](Rml::Style::Overflow overflow) { return overflow == Rml::Style::Overflow::Auto || overflow == Rml::Style::Overflow::Scroll; };
        if (scrolls(element->GetComputedValues().overflow_x()) || scrolls(element->GetComputedValues().overflow_y()))
            return; // scrolled content ignores the cutout
        const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
        const Rml::Rectanglef box = Rml::Rectanglef::FromPositionSize(element->GetAbsoluteOffset(Rml::BoxArea::Border), size);
        const auto ours = cutout_shifts_.find(element);
        const bool own_transform = element->GetLocalProperty(Rml::PropertyId::Transform) && (ours == cutout_shifts_.end());
        const Rml::Vector2f screen(static_cast<float>(context_->GetDimensions().x), static_cast<float>(context_->GetDimensions().y));
        bool content = false;
        if (!own_transform && size.x > 0.0f && size.y > 0.0f)
        {
            if (dynamic_cast<ComponentElement *>(element))
                content = size.x <= screen.x * 0.25f && size.y <= screen.y * 0.25f; // a control, moved whole
            else
            {
                const Rml::String &tag = element->GetTagName();
                bool text = false, children = false;
                for (int i = 0; i < element->GetNumChildren(); ++i)
                {
                    auto *child = element->GetChild(i);
                    if (auto *run = dynamic_cast<Rml::ElementText *>(child))
                        text = text || !run->GetText().empty();
                    else
                        children = true;
                }
                content = (text && !children) || tag == "ui-icon" || tag == "img" || tag == "ui-ring";
            }
        }
        if (content)
        {
            const Rml::Vector2f shift = CutoutShift(box);
            if (shift.x != 0.0f || shift.y != 0.0f)
                out[element] = shift;
            return; // a control or a run of text moves as one
        }
        for (int i = 0; i < element->GetNumChildren(); ++i)
            PlaceOffCutouts(element->GetChild(i), out);
    }

    // The cutout shift applied to `element` or to one of its ancestors (touch regions follow it).
    Rml::Vector2f CutoutShiftOf(Rml::Element *element) const
    {
        for (Rml::Element *at = element; at; at = at->GetParentNode())
            if (const auto found = cutout_shifts_.find(at); found != cutout_shifts_.end() && found->second.element.get() == at)
                return found->second.shift;
        return {0.0f, 0.0f};
    }

    void CollectRegions(Rml::Element *element, std::vector<Rml::Rectanglef> &out) const
    {
        if (!element->IsVisible(true))
            return;
        if (auto *component = dynamic_cast<ComponentElement *>(element))
        {
            std::vector<Rml::Element *> parts;
            component->TouchRegions(parts);
            for (Rml::Element *part : parts)
            {
                const Rml::Vector2f size = part->GetBox().GetSize(Rml::BoxArea::Border);
                if (size.x > 0.0f && size.y > 0.0f && part->IsVisible(true))
                    out.push_back(Rml::Rectanglef::FromPositionSize(part->GetAbsoluteOffset(Rml::BoxArea::Border) + CutoutShiftOf(part), size));
            }
        }
        for (int i = 0; i < element->GetNumChildren(); ++i)
            CollectRegions(element->GetChild(i), out);
    }

    // Called after everything that changes what a touch would hit (a frame, a document,
    // an interface, a state): the answer of WantsTouch never waits for this thread. Outside a
    // frame (`laid_out` false) what now covers the game is not laid out yet: pointers that go
    // down on it are held until the next frame (Touch, ReleaseHeld).
    void PublishTouchMap(bool laid_out = false)
    {
        TouchMap map;
        const bool hud = hud_->Interactive();
        map.back = !video_ && ((hud && hud_->Shown().modal) || router_->WantsBack()); // a modal HUD sheet or a screen's own modal
        const bool was_between = between_;
        between_ = Between();
        // a document or an interface screen (also one the game opened whose model is still to
        // come), a modal HUD sheet, or nothing the player can use (Between)
        map.everywhere = !video_ && (document_ != nullptr || map.back || opening_ != Opening::None || between_);
        if (hud && !map.everywhere && hud_document_)
            CollectRegions(hud_document_, map.regions);
        const std::lock_guard<std::mutex> lock(touch_mutex_);
        // What comes after a transition is not laid out before the next frame either.
        if (!laid_out && map.everywhere && (!touch_map_.everywhere || (was_between && !between_)))
            unlaid_ = true;
        touch_map_ = std::move(map);
    }

    // The game is between scenes and shows nothing the player can use: no interface (nor one
    // being opened), no full-screen video, no telescope, no gameplay mode a HUD
    // scene holds, and the engine says why there is no mode: it is starting, loading, has no
    // scene yet, or launched an interface that is not shown yet (Gameplay::blocked). A touch the
    // game got then would land in whatever comes next unseen, so the UI owns the touches and holds
    // them for that: the next interface gets them, gameplay drops them (ReleaseHeld). Every
    // other reason (a cutscene, a dialogue, a command menu, a control group no mode knows, no
    // native player) leaves the touches to the game as before. States decide it, not a timer.
    // One log line per screen switch ("UI switch"), also in the player's shared log: from the
    // engine action that led to it (when there was one since the last switch) through the game
    // opening its interface and the model arriving to the screen's first frame, and on until
    // its frames stop rebuilding or laying out (pictures that arrived, re-cut text): the CPU
    // of those frames by stage (FrameStats). GPU time is the renderer's.
    void ReportSwitch(bool idle)
    {
        if (!switch_.open || switch_.modelled == Clock::time_point{})
            return;
        const auto now = Clock::now();
        if (!idle)
        {
            const FrameStats &frame = counters_;
            if (!switch_.drawn)
            {
                switch_.drawn = true;
                switch_.first = now;
                switch_.first_frame = frame;
            }
            else
            {
                FrameStats &sum = switch_.settle;
                sum.update_ms += frame.update_ms, sum.render_ms += frame.render_ms, sum.apply_ms += frame.apply_ms;
                sum.rebuild_ms += frame.rebuild_ms, sum.style_ms += frame.style_ms, sum.layout_ms += frame.layout_ms;
                sum.picture_ms += frame.picture_ms, sum.texture_ms += frame.texture_ms, sum.pictures += frame.pictures;
                sum.textures += frame.textures, sum.texture_bytes += frame.texture_bytes, sum.rebuilds += frame.rebuilds;
                sum.laid_out += frame.laid_out, sum.layout_passes += frame.layout_passes;
            }
            ++switch_.frames;
            const bool busy = frame.rebuilds > 0 || frame.laid_out > 0 || frame.pictures > 0 || frame.textures > 0;
            if ((busy || switch_.frames == 1) && switch_.frames < 30)
                return;
        }
        const auto ms = [](Clock::time_point from, Clock::time_point to) { return Milliseconds(from, to); };
        const bool acted = last_tap_ > reported_ && last_tap_ <= switch_.begun;
        const Clock::time_point origin = acted ? last_tap_ : switch_.begun;
        const FrameStats &f = switch_.first_frame, &s = switch_.settle;
        char line[1024];
        std::snprintf(line, sizeof line,
                      "UI switch %s: first frame %.0f ms after %s%s (game: %s%.0f ms to the model), settled %.0f ms after it in %d more frames | "
                      "first frame CPU: apply %.1f, rebuild %.1f (%d), style %.1f, layout %.1f (%d boxes, %d passes), pictures %d in %.1f, "
                      "render %.1f incl. textures %d (%zu KB) in %.1f | settling CPU: update %.1f, render %.1f, rebuild %d, layout %.1f (%d passes), "
                      "pictures %d in %.1f",
                      switch_.screen.c_str(), ms(origin, switch_.first), acted ? "the tap" : "the interface opened", "", switch_.expected ? ("interface opened +" + std::to_string(int(ms(origin, switch_.begun))) + " ms, ").c_str() : "",
                      ms(origin, switch_.modelled), ms(switch_.first, now), switch_.frames - 1, f.apply_ms, f.rebuild_ms, f.rebuilds, f.style_ms,
                      f.layout_ms, f.laid_out, f.layout_passes, f.pictures, f.picture_ms, f.render_ms, f.textures, f.texture_bytes / 1024, f.texture_ms,
                      s.update_ms, s.render_ms, s.rebuilds, s.layout_ms, s.layout_passes, s.pictures, s.picture_ms);
        Log(line);
        switch_.open = false;
        reported_ = now;
    }

    bool Between() const
    {
        if (video_ || opening_ != Opening::None || InterfaceActive())
            return false;
        const hud::States &states = hud_->Current();
        // Engine states only (gameplay, video, spyglass): they arrive in the engine's own frame.
        // The platform's presentation (its cinematic flag) comes a thread hop and a loop later and
        // stays stale through a load that follows a video (the title).
        if ((states.spyglass.known && states.spyglass.on) || hud_->InGameplay())
            return false;
        const std::string &why = states.gameplay.blocked;
        return why == "starting" || why == "loading" || why == "no-scene" || why == "interface";
    }

    // What decided a held or dropped touch, for the log (the player's shared log shows a state
    // that was missing).
    std::string RoutingFacts() const
    {
        const hud::States &states = hud_->Current();
        const hud::Presentation &shown = states.presentation;
        return "gameplay " + states.gameplay.mode + (states.gameplay.blocked.empty() ? "" : " (" + states.gameplay.blocked + ")") +
               ", presentation " + (shown.known ? "screen '" + shown.screen + "' loading " + std::to_string(shown.loading) + " cinematic " +
                                                      std::to_string(shown.cinematic) + " dialogue " + std::to_string(shown.dialogue) + " live " +
                                                      std::to_string(shown.live)
                                                : std::string("unknown")) +
               ", interface " + std::to_string(InterfaceActive()) + ", video " + std::to_string(video_);
    }

    bool Holding() const
    {
        return opening_ != Opening::None || unlaid_ || between_;
    }

    // After a frame (its layout): an opened interface that got its model is shown, a new sheet
    // is laid out, and the touches held for them are applied in order. Touches held through a
    // transition that ends in gameplay are dropped instead: the player aimed at a screen, not
    // at the scene that appeared.
    void ReleaseHeld()
    {
        if (opening_ == Opening::AwaitFrame)
            opening_ = Opening::None;
        unlaid_ = false;
        if (opening_ != Opening::None || between_ || held_.empty())
            return;
        const std::vector<HeldTouch> held = std::move(held_);
        held_.clear();
        held_pointers_.clear();
        if (!InterfaceActive())
        {
            Log(std::to_string(held.size()) + " touch events dropped: made while the game showed nothing to use, and gameplay came next; " + RoutingFacts());
            return;
        }
        for (const HeldTouch &touch : held)
            Apply(touch.phase, touch.pointer, touch.x, touch.y, touch.time);
    }

    struct Toast_
    {
        Rml::ObserverPtr<Rml::Element> element;
        double until = 0.0;
        std::string text; // the message; the element shows it with its repeat count
        int count = 1;
        double leaving = -1.0; // when it started to animate out (c-notice leaving), -1 = shown
        double held = -1.0;    // the seconds it had left when a finger stopped it (-1 = running)
    };

    // How long a toast animates out (pack time.toastLeave); the stack closes up meanwhile.
    double ToastLeaveSeconds() const
    {
        return Setting("toastLeave", 0.22);
    }

    void Leave(Toast_ &toast, double now)
    {
        changed_ = true;
        toast.leaving = now;
        if (Rml::Element *element = toast.element.get())
            element->SetAttribute("leaving", "1");
    }

    void RemoveToast(size_t index)
    {
        changed_ = true;
        if (Rml::Element *element = toasts_[index].element.get())
            if (Rml::Element *parent = element->GetParentNode())
                parent->RemoveChild(element);
        toasts_.erase(toasts_.begin() + static_cast<long>(index));
    }

    // An expired toast animates out first; it goes once that has run.
    void ExpireToasts(double now)
    {
        for (size_t i = toasts_.size(); i-- > 0;)
            if (toasts_[i].leaving < 0.0 && toasts_[i].held < 0.0 && now >= toasts_[i].until)
                Leave(toasts_[i], now);
            else if (toasts_[i].leaving >= 0.0 && now >= toasts_[i].leaving + ToastLeaveSeconds())
                RemoveToast(i);
    }

    Config config_;
    System system_;
    std::unique_ptr<PackFiles> files_;
    RenderBackend *backend_ = nullptr;
    std::unique_ptr<FrameRenderer> renderer_;
    std::unique_ptr<CountedDocumentInstancer> document_instancer_; // registered for "body" until Rml::Shutdown
    FrameStats counters_;   // the frame being made, including the work done since the last one
    FrameStats last_frame_; // what the last Frame did
    bool video_ = false;    // a full-screen game video plays (channel "video")
    bool changed_ = true;   // something since the last drawn frame may look different (Prepare)
    bool drawn_ = false;
    uint64_t shown_hash_ = 0; // drawing commands of the frame on the screen
    std::unique_ptr<ComponentRegistry> registry_;
    Rml::Context *context_ = nullptr;
    Rml::ElementDocument *document_ = nullptr;
    Rml::ElementDocument *overlay_ = nullptr;
    std::string overlay_path_;
    std::string screen_path_;
    std::unordered_map<std::string, Json> strings_; // language code → {key: text}
    std::string language_ = "en";
    std::set<std::string> unknown_languages_, missing_strings_;
    std::unique_ptr<screens::Router> router_;
    Rml::ElementDocument *screen_root_ = nullptr; // the screen document while a game interface is mounted
    // HUD
    std::unique_ptr<hud::Hud> hud_;
    hud::Layout layout_;
    std::string hud_path_;
    Rml::ElementDocument *hud_document_ = nullptr;
    std::unordered_map<std::string, Json> hud_props_;       // props last given to each mounted element
    std::unordered_map<std::string, std::string> hud_made_; // what each mounted element was created as
    mutable std::mutex touch_mutex_;
    TouchMap touch_map_;
    std::vector<Cutout> cutouts_; // physical pixels (SetCutouts)
    bool cutouts_changed_ = false;
    struct CutoutShift_
    {
        Rml::ObserverPtr<Rml::Element> element;
        Rml::Vector2f shift;
    };
    std::unordered_map<Rml::Element *, CutoutShift_> cutout_shifts_; // what AvoidCutouts moved, and by how much
    // An interface the game opened (ExpectInterface): its model is still to come, then its
    // first frame.
    enum class Opening
    {
        None,
        AwaitModel,
        AwaitFrame
    };
    Opening opening_ = Opening::None;
    bool game_interface_ = false; // a game interface model is shown (SetInterface until CloseInterface)
    // A screen switch, timed from the engine action before it to its first frames (ReportSwitch).
    using Clock = std::chrono::steady_clock;
    struct Switch
    {
        bool open = false;
        bool expected = false;  // the engine announced it (ExpectInterface) before its model
        uint64_t opening = 0;   // InterfaceModel::opening of the screen being timed
        std::string screen;
        Clock::time_point begun, modelled, first;
        bool drawn = false; // its first frame with the model is drawn
        FrameStats first_frame, settle;
        int frames = 0;
        void Begin(Clock::time_point now, bool by_expect)
        {
            *this = Switch{false, by_expect, opening};
            open = true;
            begun = now;
            modelled = {};
        }
    } switch_;
    Clock::time_point last_tap_{}, reported_{}; // the newest touch end (its own time); the end of the last switch reported
    bool unlaid_ = false; // something covers the game since the last frame (PublishTouchMap)
    bool between_ = false; // nothing the player can use is shown (Between)
    struct HeldTouch
    {
        TouchPhase phase;
        int pointer;
        float x, y;
        double time;
    };
    std::vector<HeldTouch> held_;
    std::set<int> held_pointers_;
    Json settings_ = Json::object();
    Json safe_ = {{"left", 0}, {"top", 0}, {"right", 0}, {"bottom", 0}};
    std::unordered_map<int, Capture> captures_;
    std::vector<Rml::ObserverPtr<Rml::Element>> ticking_;
    std::vector<Toast_> toasts_;
    int scroll_pointer_ = -1; // the pointer that drags lists (one at a time)
    TouchScroll scroll_;
    bool initialised_ = false;
};

} // namespace

bool DecodePackImage(const std::string &source, int &width, int &height, std::vector<unsigned char> &rgba)
{
    Rml::FileInterface *files = Rml::GetFileInterface();
    const Rml::FileHandle handle = files->Open(source);
    if (!handle)
        return false;
    std::vector<uint8_t> bytes(files->Length(handle));
    files->Read(bytes.data(), bytes.size(), handle);
    files->Close(handle);
    plutovg_surface_t *surface = plutovg_surface_load_from_image_data(bytes.data(), static_cast<int>(bytes.size()));
    if (!surface)
        return false;
    width = plutovg_surface_get_width(surface);
    height = plutovg_surface_get_height(surface);
    const int stride = plutovg_surface_get_stride(surface);
    const unsigned char *data = plutovg_surface_get_data(surface);
    rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y)
    {
        const auto *row = reinterpret_cast<const uint32_t *>(data + static_cast<size_t>(y) * static_cast<size_t>(stride));
        for (int x = 0; x < width; ++x)
        {
            const uint32_t argb = row[x]; // premultiplied
            unsigned char *pixel = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            pixel[0] = static_cast<unsigned char>(argb >> 16);
            pixel[1] = static_cast<unsigned char>(argb >> 8);
            pixel[2] = static_cast<unsigned char>(argb);
            pixel[3] = static_cast<unsigned char>(argb >> 24);
        }
    }
    plutovg_surface_destroy(surface);
    return true;
}

std::unique_ptr<FileSource> DirectoryFiles(std::string root)
{
    return std::make_unique<DirectorySource>(std::move(root));
}

std::unique_ptr<Host> Host::Create(const Config &config, std::string *error)
{
    if (g_host_alive.exchange(true))
    {
        const std::string why = "a UI host already lives in this process: one host per process (RmlUi state is process-global)";
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s", why.c_str());
        if (error)
            *error = why;
        return nullptr;
    }
    auto host = std::make_unique<HostImpl>();
    host->owns_process_slot = true;
    if (!host->Start(config, error))
        return nullptr;
    return host;
}

} // namespace storm::ui
