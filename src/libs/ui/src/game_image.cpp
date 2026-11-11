// <ui-game-image texture="INTERFACES/items6" cell="8">: a picture of the player's own game
// files (RESOURCE/Textures/<texture>.tga.tx), supplied by the renderer's texture module through
// the render backend (RenderBackend::AcquireGamePicture). Nothing is drawn until the renderer
// has it, so the placeholder under the element stays visible; the parent then gets the class
// "has-image". A picture that cannot be had (file missing, bad atlas cell) is logged once with
// texture, cell and reason and the parent gets "image-missing", drawn unlike the neutral loading
// placeholder. A backend without a picture source (a capture without game files) keeps the
// neutral placeholder; one line is logged.
// texture="atlas:GOODS/Balls" names a picture the way interface models do: a group and a
// picture of the game's own RESOURCE/INI/interfaces/pictures.ini, read at run time from
// the game root (picture_atlas.h). It resolves to a texture and a rectangle; `cell` is
// then ignored. uv="left,top,right,bottom" (0…1) shows that rectangle of a texture file
// instead of a cell (pictures the engine names by file: maps, interlude pictures).
// fit="contain|cover" keeps the picture's proportions (see OnRender).
#include "elements.h"
#include "picture_atlas.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>

namespace storm::ui
{

namespace
{

enum class State
{
    Loading, // the renderer reports it pending
    Ready,   // draw GamePictureSource(texture)
    Missing,
    Unavailable // the renderer has no picture source (a capture without game files): neutral placeholder, logged once
};

struct Entry
{
    State state = State::Loading;
    std::string reason; // why it is Missing
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

// Main thread only: the pictures the renderer was asked for, by texture name.
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
        if (root == root_)
            return;
        root_ = root;
        atlas_.reset();
        atlas_missing_.clear();
    }

    // Resolves "GROUP/Picture" through the game's pictures.ini. False with a reason when
    // the file or the picture is not there.
    bool Atlas(const std::string &reference, AtlasPicture &out, std::string &reason)
    {
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

    bool HasRoot() const
    {
        return !root_.empty();
    }

    void SetRenderer(RenderBackend *renderer)
    {
        renderer_ = renderer;
    }

    // Each update while the renderer reports the picture as pending.
    void Poll(const std::string &texture, Entry &entry)
    {
        if (entry.state != State::Loading || !renderer_)
            return;
        switch (renderer_->AcquireGamePicture(texture))
        {
        case RenderBackend::PictureState::Pending:
            changed_ = true; // keep frames coming
            return;
        case RenderBackend::PictureState::Ready:
            entry.state = State::Ready;
            break;
        case RenderBackend::PictureState::Failed:
            entry.reason = "the renderer could not load " + texture;
            entry.state = State::Missing;
            break;
        case RenderBackend::PictureState::Unavailable:
            entry.state = State::Unavailable;
            break;
        }
        changed_ = true;
    }

    std::shared_ptr<Entry> Request(const std::string &texture)
    {
        auto &entry = entries_[texture];
        if (!entry)
            entry = std::make_shared<Entry>();
        return entry;
    }

    bool TakeChanged()
    {
        const bool changed = changed_;
        changed_ = false;
        return changed;
    }

    // A picture changed state since the last TakeChanged (it must get a frame of its own).
    bool Pending() const
    {
        return changed_;
    }

    void Shutdown()
    {
        entries_.clear();
    }

  private:
    std::unique_ptr<PictureAtlas> atlas_; // of the current game root, read on first use
    std::string atlas_missing_;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
    std::string root_;
    RenderBackend *renderer_ = nullptr;
    bool changed_ = false;
};

// "left,top,right,bottom" in 0…1 with a non-empty rectangle; false for anything else.
bool ParseUv(const std::string &text, float uv[4])
{
    if (text.empty())
        return false;
    float parsed[4];
    if (std::sscanf(text.c_str(), "%f,%f,%f,%f", &parsed[0], &parsed[1], &parsed[2], &parsed[3]) != 4)
        return false;
    for (const float v : parsed)
        if (!(v >= 0.0f && v <= 1.0f))
            return false;
    if (parsed[0] == parsed[2] || parsed[1] == parsed[3])
        return false;
    std::copy(std::begin(parsed), std::end(parsed), uv);
    return true;
}

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
            renderer_texture_ = {};
        }
        built_size_ = {-1, -1};
    }

    void OnUpdate() override
    {
        Rml::Element::OnUpdate();
        std::string texture = GetAttribute<Rml::String>("texture", "");
        const bool by_atlas = texture.rfind("atlas:", 0) == 0;
        if (!entry_ && by_atlas && atlas_reason_.empty() && Store::Get().HasRoot())
        {
            if (Store::Get().Atlas(texture.substr(6), atlas_, atlas_reason_))
                entry_ = Store::Get().Request(atlas_.texture);
        }
        else if (!entry_ && !by_atlas && !texture.empty())
            entry_ = Store::Get().Request(texture);
        if (entry_)
            Store::Get().Poll(by_atlas ? atlas_.texture : texture, *entry_);
        const State state = entry_ ? entry_->state : by_atlas && !Store::Get().HasRoot() ? State::Unavailable : State::Loading;
        std::string reason;
        float scratch_uv[4];
        if (!atlas_reason_.empty())
            reason = atlas_reason_;
        else if (by_atlas)
            reason = state == State::Missing ? entry_->reason : std::string();
        else if (state == State::Missing)
            reason = entry_->reason;
        else if (state == State::Ready && !GetAttribute<Rml::String>("uv", "").empty() && !ParseUv(GetAttribute<Rml::String>("uv", ""), scratch_uv))
            reason = "uv \"" + GetAttribute<Rml::String>("uv", "") + "\" is not left,top,right,bottom in 0..1";
        else if (state == State::Ready && !AtlasCell(GetAttribute<int>("cell", 0), GetAttribute<int>("grid", 4), nullptr))
            reason = "cell outside the " + std::to_string(GetAttribute<int>("grid", 4)) + "x" + std::to_string(GetAttribute<int>("grid", 4)) + " atlas";
        else if (state == State::Unavailable && !unavailable_logged_)
        {
            unavailable_logged_ = true; // once per process: the host gave no source at all
            Rml::Log::Message(Rml::Log::LT_INFO, "Game pictures unavailable (no picture source or game root): placeholders stay");
        }
        const bool ready = state == State::Ready && reason.empty();
        const bool missing = !reason.empty();
        if (missing && !logged_)
        {
            logged_ = true;
            Rml::Log::Message(Rml::Log::LT_ERROR, "Game picture %s#%d missing: %s", GetAttribute<Rml::String>("texture", "").c_str(),
                              GetAttribute<int>("cell", 0), reason.c_str());
        }
        shown_ = ready;
        // Checked on the parent itself every update: a template patch of the parent (a new
        // model revision) rewrites its class attribute and drops these classes.
        if (Rml::Element *parent = GetParentNode())
        {
            const bool change = parent->IsClassSet("has-image") != ready || parent->IsClassSet("image-missing") != missing;
            parent->SetClass("has-image", ready);
            parent->SetClass("image-missing", missing);
            // The parent was updated before this child in this pass: its new class takes
            // effect in the next update, which an idle host would otherwise never run.
            if (change)
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
        if (!renderer_texture_)
            renderer_texture_ = manager->LoadTexture(GamePictureSource(atlas_.texture.empty() ? GetAttribute<Rml::String>("texture", "") : atlas_.texture));
        const float opacity = GetComputedValues().opacity();
        if (size != built_size_ || opacity != built_opacity_)
        {
            float uv[4];
            if (!atlas_.texture.empty())
                std::copy(std::begin(atlas_.uv), std::end(atlas_.uv), uv);
            else if (!ParseUv(GetAttribute<Rml::String>("uv", ""), uv))
                AtlasCell(GetAttribute<int>("cell", 0), GetAttribute<int>("grid", 4), uv);
            Rml::Vector2f uv0 = {uv[0], uv[1]}, uv1 = {uv[2], uv[3]};
            Rml::Vector2f origin = {0, 0}, extent = size;
            // fit="contain" keeps the picture's proportions inside the box, "cover" fills the
            // box and crops the picture's middle; without it the picture is stretched.
            const std::string fit = GetAttribute<Rml::String>("fit", "");
            const Rml::Vector2i pixels = renderer_texture_.GetDimensions();
            const float picture = pixels.y && uv1.y != uv0.y ? (pixels.x * std::abs(uv1.x - uv0.x)) / (pixels.y * std::abs(uv1.y - uv0.y)) : 0.0f;
            const float box = size.x / size.y;
            if (fit == "contain" && picture > 0.0f)
            {
                extent = picture > box ? Rml::Vector2f{size.x, size.x / picture} : Rml::Vector2f{size.y * picture, size.y};
                origin = (size - extent) * 0.5f;
            }
            else if (fit == "cover" && picture > 0.0f)
            {
                const Rml::Vector2f span = uv1 - uv0;
                const Rml::Vector2f keep = picture > box ? Rml::Vector2f{box / picture, 1.0f} : Rml::Vector2f{1.0f, picture / box};
                uv0 = uv0 + span * (Rml::Vector2f{1.0f, 1.0f} - keep) * 0.5f;
                uv1 = uv0 + span * keep;
            }
            Rml::Mesh mesh = geometry_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            Rml::MeshUtilities::GenerateQuad(mesh, origin, extent, Rml::Colourb(255, 255, 255, 255).ToPremultiplied(opacity), uv0, uv1);
            geometry_ = manager->MakeGeometry(std::move(mesh));
            built_size_ = size;
            built_opacity_ = opacity;
        }
        geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content), renderer_texture_);
    }

  private:
    std::shared_ptr<Entry> entry_;
    AtlasPicture atlas_;       // set when the texture attribute is an atlas reference
    std::string atlas_reason_; // why the reference could not be resolved
    Rml::Texture renderer_texture_;
    Rml::Geometry geometry_;
    Rml::Vector2f built_size_ = {-1, -1};
    float built_opacity_ = -1.0f;
    bool shown_ = false;
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

void SetGameImageRenderer(RenderBackend *renderer)
{
    Store::Get().SetRenderer(renderer);
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
