// <ui-game-image texture="INTERFACES/items6" cell="8">: a picture read from the player's
// own game files (RESOURCE/Textures/<texture>.tga.tx). Nothing is drawn until the file
// is decoded on a worker thread, so the placeholder under the element stays visible;
// the parent then gets the class "has-image". A picture that cannot be had (file missing,
// loader refused, decode error, bad atlas cell) is logged once with texture, cell and reason
// and the parent gets "image-missing", drawn unlike the neutral loading placeholder.
// texture="atlas:GOODS/Balls" names a picture the way interface models do: a group and a
// picture of the game's own RESOURCE/INI/interfaces/pictures.ini, read at run time from
// the game root (picture_atlas.h). It resolves to a texture and a rectangle; `cell` is
// then ignored.
// Exactly one source is used: the configured loader if there is one, else the .tx files
// under the game root. With neither (gallery on the build host, the design canvas) the
// pictures are "unavailable": the neutral placeholder stays and one line is logged.
#include "elements.h"
#include "picture_atlas.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace storm::ui
{

namespace
{

enum class State
{
    Loading,
    Ready,
    Missing,
    Unavailable // no loader and no game root: documented fallback, logged once
};

struct Entry
{
    std::atomic<State> state{State::Loading};
    TxImage image; // written by the worker before `state` becomes Ready
    std::unique_ptr<Rml::CallbackTextureSource> source;
    std::string reason; // why it is Missing; written by the worker before `state`
};

bool SameName(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// Game data comes from Windows: resolve each path part without regard to case.
bool ResolvePath(const std::filesystem::path &root, const std::string &relative, std::filesystem::path &out)
{
    std::error_code error;
    std::filesystem::path current = root;
    size_t at = 0;
    while (at < relative.size())
    {
        size_t end = relative.find_first_of("/\\", at);
        if (end == std::string::npos)
            end = relative.size();
        const std::string part = relative.substr(at, end - at);
        at = end + 1;
        if (part.empty() || part == ".")
            continue;
        if (part == "..")
            return false;
        std::filesystem::path next = current / part;
        if (!std::filesystem::exists(next, error))
        {
            bool found = false;
            for (std::filesystem::directory_iterator it(current, error), last; !error && it != last; it.increment(error))
                if (SameName(it->path().filename().string(), part))
                {
                    next = it->path();
                    found = true;
                    break;
                }
            if (!found)
                return false;
        }
        current = next;
    }
    out = current;
    return true;
}

class Store
{
  public:
    static Store &Get()
    {
        static Store store;
        return store;
    }

    void SetRoot(const std::string &root)
    {
        std::lock_guard lock(mutex_);
        if (root == root_)
            return;
        root_ = root;
        ++generation_;
        entries_.clear();
        pending_ -= static_cast<int>(queue_.size());
        queue_.clear();
        atlas_.reset();
    }

    // Resolves "GROUP/Picture" through the game's pictures.ini. False with a reason when
    // the file or the picture is not there.
    bool Atlas(const std::string &reference, AtlasPicture &out, std::string &reason)
    {
        std::lock_guard lock(mutex_);
        if (!atlas_)
        {
            atlas_ = std::make_unique<PictureAtlas>();
            std::filesystem::path path;
            if (!root_.empty() && ResolvePath(std::filesystem::path(root_), "RESOURCE/INI/interfaces/pictures.ini", path))
            {
                std::ifstream file(path, std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                atlas_->Load(text);
            }
            else
                atlas_missing_ = root_.empty() ? "no game root to read pictures.ini from" : "no RESOURCE/INI/interfaces/pictures.ini under " + root_;
        }
        const size_t slash = reference.find('/');
        const AtlasPicture *found = slash == std::string::npos ? nullptr : atlas_->Find(reference.substr(0, slash), reference.substr(slash + 1));
        if (!found)
        {
            reason = !atlas_missing_.empty() ? atlas_missing_ : "pictures.ini has no picture " + reference;
            return false;
        }
        out = *found;
        return true;
    }

    bool HasSource()
    {
        std::lock_guard lock(mutex_);
        return !root_.empty() || loader_;
    }

    void SetLoader(std::function<bool(const std::string &, Picture &)> loader)
    {
        std::lock_guard lock(mutex_);
        loader_ = std::move(loader);
    }

    std::shared_ptr<Entry> Request(const std::string &texture)
    {
        std::lock_guard lock(mutex_);
        auto &entry = entries_[texture];
        if (!entry)
        {
            entry = std::make_shared<Entry>();
            all_.push_back(entry);
            if (root_.empty() && !loader_)
                entry->state = State::Unavailable;
            else
            {
                queue_.push_back({texture, entry, generation_});
                ++pending_;
                if (!worker_.joinable())
                    worker_ = std::thread([this] { Work(); });
                wake_.notify_one();
            }
        }
        return entry;
    }

    bool TakeChanged()
    {
        return changed_.exchange(false);
    }

    // Pictures queued or decoding, or decoded since the last TakeChanged (a decode that ends
    // while a frame is being drawn must still get a frame of its own).
    bool Pending() const
    {
        return pending_ > 0 || changed_;
    }

    void Shutdown()
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
            pending_ -= static_cast<int>(queue_.size());
            queue_.clear();
            entries_.clear();
        }
        wake_.notify_all();
        if (worker_.joinable())
            worker_.join();
        stop_ = false;
        // Elements may still hold entries: give their textures back before RmlUi shuts down.
        for (const std::weak_ptr<Entry> &weak : all_)
            if (const std::shared_ptr<Entry> entry = weak.lock())
                entry->source.reset();
        all_.clear();
    }

  private:
    struct Job
    {
        std::string texture;
        std::shared_ptr<Entry> entry;
        int generation = 0;
    };

    void Work()
    {
        while (true)
        {
            Job job;
            std::string root;
            std::function<bool(const std::string &, Picture &)> loader;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_)
                    return;
                job = std::move(queue_.front());
                queue_.pop_front();
                root = root_;
                loader = loader_;
            }
            bool ok = false;
            std::filesystem::path path;
            if (loader) // the one configured source; its failure is not retried elsewhere
            {
                ok = loader(job.texture, job.entry->image);
                if (!ok)
                    job.entry->reason = "the game picture loader refused it";
            }
            else if (ResolvePath(std::filesystem::path(root), "RESOURCE/Textures/" + job.texture + ".tga.tx", path)) // any case
            {
                std::ifstream file(path, std::ios::binary);
                const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::string error;
                ok = DecodeTx(bytes.data(), bytes.size(), job.entry->image, &error);
                if (!ok)
                    job.entry->reason = "cannot decode " + path.string() + ": " + error;
            }
            else
                job.entry->reason = "no file RESOURCE/Textures/" + job.texture + ".tga.tx under " + root;
            job.entry->state = ok ? State::Ready : State::Missing;
            changed_ = true;
            --pending_;
        }
    }

    std::mutex mutex_;
    std::unique_ptr<PictureAtlas> atlas_; // of the current game root, read on first use
    std::string atlas_missing_;
    std::condition_variable wake_;
    std::deque<Job> queue_;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
    std::vector<std::weak_ptr<Entry>> all_;
    std::string root_;
    std::function<bool(const std::string &, Picture &)> loader_;
    std::thread worker_;
    std::atomic<bool> changed_{false};
    std::atomic<int> pending_{0}; // pictures queued or being decoded
    bool stop_ = false;
    int generation_ = 0;
};

class GameImageElement final : public Rml::Element
{
  public:
    explicit GameImageElement(const Rml::String &tag) : Rml::Element(tag)
    {
    }

  protected:
    void OnAttributeChange(const Rml::ElementAttributes &changed) override
    {
        Rml::Element::OnAttributeChange(changed);
        if (changed.count("texture"))
        {
            entry_.reset();
            atlas_reason_.clear();
            atlas_ = AtlasPicture();
        }
        built_size_ = {-1, -1};
    }

    void OnUpdate() override
    {
        Rml::Element::OnUpdate();
        std::string texture = GetAttribute<Rml::String>("texture", "");
        const bool by_atlas = texture.rfind("atlas:", 0) == 0;
        if (!entry_ && by_atlas && atlas_reason_.empty() && Store::Get().HasSource())
        {
            if (Store::Get().Atlas(texture.substr(6), atlas_, atlas_reason_))
                entry_ = Store::Get().Request(atlas_.texture);
        }
        else if (!entry_ && !by_atlas && !texture.empty())
            entry_ = Store::Get().Request(texture);
        const State state = entry_ ? entry_->state.load() : by_atlas && !Store::Get().HasSource() ? State::Unavailable : State::Loading;
        std::string reason;
        if (!atlas_reason_.empty())
            reason = atlas_reason_;
        else if (by_atlas)
            reason = state == State::Missing ? entry_->reason : std::string();
        else if (state == State::Missing)
            reason = entry_->reason;
        else if (state == State::Ready && !AtlasCell(GetAttribute<int>("cell", 0), GetAttribute<int>("grid", 4), nullptr))
            reason = "cell outside the " + std::to_string(GetAttribute<int>("grid", 4)) + "x" + std::to_string(GetAttribute<int>("grid", 4)) + " atlas";
        else if (state == State::Unavailable && !unavailable_logged_)
        {
            unavailable_logged_ = true; // once per process: the host gave no source at all
            Rml::Log::Message(Rml::Log::LT_INFO, "Game pictures unavailable (no game root, no loader): placeholders stay");
        }
        const bool ready = state == State::Ready && reason.empty();
        const bool missing = !reason.empty();
        if (missing && !logged_)
        {
            logged_ = true;
            Rml::Log::Message(Rml::Log::LT_ERROR, "Game picture %s#%d missing: %s", GetAttribute<Rml::String>("texture", "").c_str(),
                              GetAttribute<int>("cell", 0), reason.c_str());
        }
        if (ready != shown_ || missing != missing_shown_)
        {
            shown_ = ready;
            missing_shown_ = missing;
            if (Rml::Element *parent = GetParentNode())
            {
                parent->SetClass("has-image", ready);
                parent->SetClass("image-missing", missing);
            }
            // The parent was updated before this child in this pass: its new class takes
            // effect in the next update, which an idle host would otherwise never run.
            if (Rml::Context *context = GetContext())
                context->RequestNextUpdate(0.0);
        }
    }

    void OnRender() override
    {
        Rml::RenderManager *manager = GetRenderManager();
        if (!manager || !entry_ || !shown_)
            return;
        const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
        if (size.x < 1.0f || size.y < 1.0f)
            return;
        if (!entry_->source)
        {
            std::shared_ptr<Entry> entry = entry_;
            entry_->source = std::make_unique<Rml::CallbackTextureSource>([entry](const Rml::CallbackTextureInterface &texture) {
                return texture.GenerateTexture({entry->image.rgba.data(), entry->image.rgba.size()}, {entry->image.width, entry->image.height});
            });
        }
        const float opacity = GetComputedValues().opacity();
        if (size != built_size_ || opacity != built_opacity_)
        {
            float uv[4];
            if (atlas_.texture.empty())
                AtlasCell(GetAttribute<int>("cell", 0), GetAttribute<int>("grid", 4), uv);
            else
                std::copy(std::begin(atlas_.uv), std::end(atlas_.uv), uv);
            const Rml::Vector2f uv0 = {uv[0], uv[1]}, uv1 = {uv[2], uv[3]};
            Rml::Mesh mesh = geometry_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            Rml::MeshUtilities::GenerateQuad(mesh, {0, 0}, size, Rml::Colourb(255, 255, 255, 255).ToPremultiplied(opacity), uv0, uv1);
            geometry_ = manager->MakeGeometry(std::move(mesh));
            built_size_ = size;
            built_opacity_ = opacity;
        }
        geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content), entry_->source->GetTexture(*manager));
    }

  private:
    std::shared_ptr<Entry> entry_;
    AtlasPicture atlas_;       // set when the texture attribute is an atlas reference
    std::string atlas_reason_; // why the reference could not be resolved
    Rml::Geometry geometry_;
    Rml::Vector2f built_size_ = {-1, -1};
    float built_opacity_ = -1.0f;
    bool shown_ = false;
    bool missing_shown_ = false;
    bool logged_ = false;
    static inline bool unavailable_logged_ = false;
};

} // namespace

bool AtlasCell(int cell, int grid, float uv[4])
{
    // Cells are numbered 1…grid² by rows; 0 (no cell) means the whole texture.
    if (grid < 1 || cell < 0 || cell > grid * grid)
        return false;
    if (uv)
    {
        uv[0] = uv[1] = 0.0f, uv[2] = uv[3] = 1.0f;
        if (cell > 0)
        {
            const float step = 1.0f / static_cast<float>(grid);
            uv[0] = static_cast<float>((cell - 1) % grid) * step, uv[1] = static_cast<float>((cell - 1) / grid) * step;
            uv[2] = uv[0] + step, uv[3] = uv[1] + step;
        }
    }
    return true;
}

void RegisterGameImageElement()
{
    static Rml::ElementInstancerGeneric<GameImageElement> instancer;
    Rml::Factory::RegisterElementInstancer("ui-game-image", &instancer);
}

void SetGameImageRoot(const std::string &game_root)
{
    Store::Get().SetRoot(game_root);
}

void SetGameImageLoader(std::function<bool(const std::string &, Picture &)> loader)
{
    Store::Get().SetLoader(std::move(loader));
}

bool GameImagesChanged()
{
    return Store::Get().TakeChanged();
}

bool GameImagesPending()
{
    return Store::Get().Pending();
}

void ShutdownGameImages()
{
    Store::Get().Shutdown();
}

} // namespace storm::ui
