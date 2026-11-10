// The runtime's layer between RmlUi and the backend's renderer (RenderBackend::Interface).
// Backend-independent, so every backend (GL3 now, bgfx next) gets the same savings:
//
//   * counts what a frame costs (FrameStats: meshes, draws, clip masks, layers, uploads);
//   * folds every drawing command into a hash of the frame, so the host can tell whether
//     a frame it predicted to be unchanged really drew the same (Config::verify_unchanged);
//   * caches filter results: an element with `filter:` (drop-shadow, grayscale, …) costs
//     the backend an offscreen layer, a filter pass and a composite every frame. Here the
//     layer's commands are buffered; when they are the same as in an earlier frame (same
//     meshes, textures, positions, scissor, transform and filters), the filtered result is
//     drawn from a texture saved the second time it was seen (content seen once, such as
//     an animation, is composited as it came), as one textured quad. Positions count relative
//     to the composite region, so content that only moved (a scrolled list) stays cached. Anything else (a
//     mask image, a saved texture, another blend mode) is passed through as it
//     came; layers inside the layer are part of its content. Clip masks inside the layer are replayed on a hit (the stencil is shared by
//     every layer) and, with the stencil the layer is drawn against, part of the key.
//     A cache entry not used in a frame is released at its end.
//   * shares textures saved from a layer (RmlUi's box shadows): the same drawing relative
//     to its region gives the texture made before, held by reference until RmlUi releases
//     it everywhere and a few seconds after.
#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include "storm_ui/ui.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::ui
{

class FrameRenderer final : public Rml::RenderInterface
{
  public:
    FrameRenderer(Rml::RenderInterface &target, FrameStats &stats) : target_(target), stats_(stats)
    {
    }
    ~FrameRenderer() override;

    // Around Context::Render. EndFrame releases the cache entries this frame did not use.
    void BeginFrame();
    void EndFrame();
    // Hash of the drawing commands of the last finished frame.
    uint64_t FrameHash() const
    {
        return last_hash_;
    }
    // With recording on (Config::verify_unchanged), Difference() names the first command
    // in which the last frame differs from the one before it.
    void Record(bool on)
    {
        record_ = on;
    }
    std::string Difference() const;
    // Releases every cached filter result (context loss, shutdown).
    void ReleaseCache();

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;
    void SetTransform(const Rml::Matrix4f *transform) override;
    Rml::LayerHandle PushLayer() override;
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override;
    void PopLayer() override;
    Rml::TextureHandle SaveLayerAsTexture() override;
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
    Rml::CompiledFilterHandle CompileFilter(const Rml::String &name, const Rml::Dictionary &parameters) override;
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override;
    Rml::CompiledShaderHandle CompileShader(const Rml::String &name, const Rml::Dictionary &parameters) override;
    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
    void ReleaseShader(Rml::CompiledShaderHandle shader) override;

  private:
    // A buffered command of a layer that may be cached.
    struct Command
    {
        enum class Kind
        {
            Geometry,
            Shader,
            Scissor,
            ScissorRegion,
            Transform,
            ClipEnable,
            ClipMask,
            Push,      // a: the layer handle given to RmlUi
            Composite, // a: source, b: destination, c: blend mode, filters
            Pop,
        } kind;
        uintptr_t a = 0, b = 0, c = 0; // handles
        Rml::ClipMaskOperation operation = Rml::ClipMaskOperation::Set;
        Rml::Vector2f translation;
        Rml::Rectanglei region;
        bool flag = false;
        bool identity = true;
        Rml::Matrix4f matrix;
        std::vector<Rml::CompiledFilterHandle> filters;
    };
    struct Entry
    {
        std::vector<uint64_t> key;
        Rml::TextureHandle texture = 0;
        Rml::CompiledGeometryHandle quad = 0;
        uint64_t used = 0; // frame number
        int origin_x = 0, origin_y = 0; // where the region was when the result was saved
    };

    void Fold(uint64_t value);
    void UpdateStateHash();
    void FoldFloat(float value);
    uint64_t Serial(const std::unordered_map<uintptr_t, uint64_t> &serials, uintptr_t handle) const;
    void Forward(const Command &command);
    Rml::LayerHandle Real(Rml::LayerHandle layer) const;
    static bool StateOnly(const Command &command);
    // Drops the buffered layer: only the state its commands leave behind is set.
    void SkipBuffer();
    // Gives the buffered layer to the backend as it came (a real layer, its commands).
    void Flush();
    std::vector<uint64_t> Key(Rml::Span<const Rml::CompiledFilterHandle> filters) const;
    void DrawEntry(const Entry &entry);
    void ApplyTransform(bool identity, const Rml::Matrix4f &matrix);

    Rml::RenderInterface &target_;
    FrameStats &stats_;
    std::unordered_map<uintptr_t, uint64_t> geometry_serials_, texture_serials_, filter_serials_, shader_serials_;
    uint64_t next_serial_ = 1;
    uint64_t hash_ = 0, last_hash_ = 0;
    uint64_t frame_ = 0;
    struct HashMark
    {
        uint64_t hash = 0; // the frame hash before the layer was pushed
        bool saved = false; // the layer was saved as a texture
        size_t folded = 0;  // with record_: values folded before it
    };
    std::vector<HashMark> hash_marks_;
    bool record_ = false;
    std::vector<uint64_t> folded_, folded_last_, folded_shown_; // with record_: what was folded, per frame

    // Render state as RmlUi set it.
    bool scissor_enabled_ = false;
    Rml::Rectanglei scissor_;
    bool transform_identity_ = true;
    Rml::Matrix4f transform_;
    bool clip_enabled_ = false;
    uint64_t clip_hash_ = 0; // what the stencil holds (the clip-mask draws since the last Set)
    uint64_t state_hash_ = 0;

    // The layer being buffered, with the layers inside it.
    bool buffering_ = false;
    int buffer_depth_ = 0;              // layers open inside the buffered one
    Rml::LayerHandle outer_layer_ = 0;  // the handle RmlUi got for the buffered layer
    Rml::LayerHandle next_layer_ = static_cast<Rml::LayerHandle>(0x5747000000000000ull); // handles given out for buffered layers: never a backend's
    std::unordered_map<Rml::LayerHandle, Rml::LayerHandle> layer_map_; // buffered handle → the backend's, once drawn
    bool swallow_pop_ = false;
    std::vector<Command> buffer_;
    bool buffer_scissor_enabled_ = false; // state when the layer was pushed
    Rml::Rectanglei buffer_scissor_;
    bool buffer_identity_ = true;
    Rml::Matrix4f buffer_transform_;
    uint64_t buffer_clip_ = 0;
    int depth_ = 0; // layers pushed through to the backend while not buffering

    std::unordered_map<uint64_t, std::vector<Entry>> cache_; // key hash → entries
    std::unordered_map<uint64_t, uint64_t> candidates_;       // key hash → the last frame it was composited uncached
    struct Saved // a texture saved from a layer (box shadow), shared by the elements that drew the same
    {
        std::vector<uint64_t> key;
        Rml::TextureHandle texture = 0;
        int refs = 0;       // RmlUi's holders
        uint64_t used = 0;  // frame of the last hand-out or release
    };
    std::unordered_map<uint64_t, Saved> saved_;                    // key hash → texture
    std::unordered_map<Rml::TextureHandle, uint64_t> saved_textures_; // texture → key hash
    std::vector<Rml::CompiledGeometryHandle> deferred_geometry_; // released after the frame (a buffered layer may still need them)
    std::vector<Rml::TextureHandle> deferred_textures_;
    std::vector<Rml::CompiledFilterHandle> deferred_filters_;
    std::vector<Rml::CompiledShaderHandle> deferred_shaders_;
};

} // namespace storm::ui
