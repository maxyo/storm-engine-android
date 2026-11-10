#include "frame_renderer.h"

#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>

#include <cstdio>
#include <cstring>

namespace storm::ui
{

namespace
{


uint64_t Mix(uint64_t hash, uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

uint64_t FloatBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

FrameRenderer::~FrameRenderer()
{
    ReleaseCache();
}

void FrameRenderer::Fold(uint64_t value)
{
    hash_ = Mix(hash_, value);
    if (record_)
        folded_.push_back(value);
}

std::string FrameRenderer::Difference() const
{
    size_t at = 0;
    while (at < folded_last_.size() && at < folded_shown_.size() && folded_last_[at] == folded_shown_[at])
        ++at;
    std::string text = "first difference at value " + std::to_string(at) + " of " + std::to_string(folded_last_.size()) + " (shown frame " +
                       std::to_string(folded_shown_.size()) + "):";
    for (size_t i = at > 6 ? at - 6 : 0; i < at + 6; ++i)
    {
        char part[64];
        std::snprintf(part, sizeof(part), " %llx|%llx", i < folded_last_.size() ? static_cast<unsigned long long>(folded_last_[i]) : 0ull,
                      i < folded_shown_.size() ? static_cast<unsigned long long>(folded_shown_[i]) : 0ull);
        text += part;
    }
    return text;
}

void FrameRenderer::FoldFloat(float value)
{
    Fold(FloatBits(value));
}

uint64_t FrameRenderer::Serial(const std::unordered_map<uintptr_t, uint64_t> &serials, uintptr_t handle) const
{
    if (!handle)
        return 0;
    const auto found = serials.find(handle);
    return found == serials.end() ? ~0ull : found->second;
}

// Draws are folded with the state they are made in, so that a frame that sets the same
// state with other (redundant) calls hashes the same.
void FrameRenderer::UpdateStateHash()
{
    uint64_t hash = Mix(Mix(0, scissor_enabled_), clip_enabled_);
    if (clip_enabled_)
        hash = Mix(hash, clip_hash_);
    if (scissor_enabled_)
        hash = Mix(Mix(Mix(Mix(hash, static_cast<uint32_t>(scissor_.Left())), static_cast<uint32_t>(scissor_.Top())),
                       static_cast<uint32_t>(scissor_.Width())),
                   static_cast<uint32_t>(scissor_.Height()));
    hash = Mix(hash, transform_identity_);
    if (!transform_identity_)
        for (int i = 0; i < 16; ++i)
            hash = Mix(hash, FloatBits(transform_.data()[i]));
    state_hash_ = hash;
}

void FrameRenderer::BeginFrame()
{
    ++frame_;
    hash_ = 0;
    // A backend starts every frame without scissor, clip mask or transform.
    scissor_enabled_ = false;
    clip_enabled_ = false;
    clip_hash_ = 0;
    transform_identity_ = true;
    UpdateStateHash();
    hash_marks_.clear();
}

void FrameRenderer::EndFrame()
{
    buffer_.clear(); // RmlUi pairs every layer it pushes; nothing is left open here
    buffering_ = false;
    layer_map_.clear();
    last_hash_ = hash_;
    if (record_)
    {
        folded_shown_.swap(folded_last_);
        folded_last_.swap(folded_);
        folded_.clear();
    }
    for (auto bucket = cache_.begin(); bucket != cache_.end();)
    {
        auto &entries = bucket->second;
        for (size_t i = entries.size(); i-- > 0;)
            if (entries[i].used != frame_)
            {
                target_.ReleaseTexture(entries[i].texture);
                target_.ReleaseGeometry(entries[i].quad);
                entries.erase(entries.begin() + static_cast<long>(i));
            }
        bucket = entries.empty() ? cache_.erase(bucket) : std::next(bucket);
    }
    for (Rml::CompiledGeometryHandle geometry : deferred_geometry_)
        target_.ReleaseGeometry(geometry);
    for (Rml::TextureHandle texture : deferred_textures_)
        target_.ReleaseTexture(texture);
    for (Rml::CompiledFilterHandle filter : deferred_filters_)
        target_.ReleaseFilter(filter);
    for (Rml::CompiledShaderHandle shader : deferred_shaders_)
        target_.ReleaseShader(shader);
    deferred_geometry_.clear();
    deferred_textures_.clear();
    deferred_filters_.clear();
    deferred_shaders_.clear();
}

void FrameRenderer::ReleaseCache()
{
    for (auto &bucket : cache_)
        for (Entry &entry : bucket.second)
        {
            target_.ReleaseTexture(entry.texture);
            target_.ReleaseGeometry(entry.quad);
        }
    cache_.clear();
}

// --- resources: always straight to the backend ------------------------------------------

Rml::CompiledGeometryHandle FrameRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    ++stats_.geometry;
    const Rml::CompiledGeometryHandle geometry = target_.CompileGeometry(vertices, indices);
    // Identified by content: RmlUi recompiles the same mesh at times (text after a font
    // texture grew), which draws the same.
    uint64_t content = Mix(vertices.size(), indices.size());
    const auto bytes = [&content](const void *data, size_t size) {
        const auto *words = static_cast<const unsigned char *>(data);
        uint64_t hash = 1469598103934665603ull; // FNV-1a
        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ words[i]) * 1099511628211ull;
        content = Mix(content, hash);
    };
    bytes(vertices.data(), vertices.size() * sizeof(Rml::Vertex));
    bytes(indices.data(), indices.size() * sizeof(int));
    geometry_serials_[geometry] = content;
    return geometry;
}

void FrameRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    geometry_serials_.erase(geometry);
    if (buffering_)
        deferred_geometry_.push_back(geometry);
    else
        target_.ReleaseGeometry(geometry);
}

Rml::TextureHandle FrameRenderer::LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source)
{
    const Rml::TextureHandle texture = target_.LoadTexture(dimensions, source);
    ++stats_.textures;
    stats_.texture_bytes += static_cast<size_t>(dimensions.x) * static_cast<size_t>(dimensions.y) * 4;
    texture_serials_[texture] = next_serial_++;
    return texture;
}

Rml::TextureHandle FrameRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
{
    ++stats_.textures;
    stats_.texture_bytes += source.size();
    const Rml::TextureHandle texture = target_.GenerateTexture(source, dimensions);
    texture_serials_[texture] = next_serial_++;
    return texture;
}

void FrameRenderer::ReleaseTexture(Rml::TextureHandle texture)
{
    texture_serials_.erase(texture);
    if (buffering_)
        deferred_textures_.push_back(texture);
    else
        target_.ReleaseTexture(texture);
}

Rml::CompiledFilterHandle FrameRenderer::CompileFilter(const Rml::String &name, const Rml::Dictionary &parameters)
{
    const Rml::CompiledFilterHandle filter = target_.CompileFilter(name, parameters);
    filter_serials_[filter] = next_serial_++;
    return filter;
}

void FrameRenderer::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
    filter_serials_.erase(filter);
    if (buffering_) // a buffered composite may still use it
        deferred_filters_.push_back(filter);
    else
        target_.ReleaseFilter(filter);
}

Rml::CompiledShaderHandle FrameRenderer::CompileShader(const Rml::String &name, const Rml::Dictionary &parameters)
{
    const Rml::CompiledShaderHandle shader = target_.CompileShader(name, parameters);
    shader_serials_[shader] = next_serial_++;
    return shader;
}

void FrameRenderer::ReleaseShader(Rml::CompiledShaderHandle shader)
{
    shader_serials_.erase(shader);
    if (buffering_)
        deferred_shaders_.push_back(shader);
    else
        target_.ReleaseShader(shader);
}

// --- drawing and state -------------------------------------------------------------------

void FrameRenderer::Forward(const Command &command)
{
    switch (command.kind)
    {
    case Command::Kind::Geometry:
        target_.RenderGeometry(command.a, command.translation, command.b);
        break;
    case Command::Kind::Shader:
        target_.RenderShader(command.a, command.b, command.translation, command.c);
        break;
    case Command::Kind::Scissor:
        target_.EnableScissorRegion(command.flag);
        break;
    case Command::Kind::ScissorRegion:
        target_.SetScissorRegion(command.region);
        break;
    case Command::Kind::Transform:
        target_.SetTransform(command.identity ? nullptr : &command.matrix);
        break;
    case Command::Kind::ClipEnable:
        target_.EnableClipMask(command.flag);
        break;
    case Command::Kind::ClipMask:
        target_.RenderToClipMask(command.operation, command.a, command.translation);
        break;
    case Command::Kind::Push:
        layer_map_[command.a] = target_.PushLayer();
        ++depth_;
        break;
    case Command::Kind::Composite:
        target_.CompositeLayers(Real(command.a), Real(command.b), static_cast<Rml::BlendMode>(command.c), command.filters);
        break;
    case Command::Kind::Pop:
        target_.PopLayer();
        --depth_;
        break;
    }
}

void FrameRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
    ++stats_.draws;
    Fold(1), Fold(state_hash_), Fold(Serial(geometry_serials_, geometry)), FoldFloat(translation.x), FoldFloat(translation.y), Fold(Serial(texture_serials_, texture));
    Command command{Command::Kind::Geometry, geometry, texture};
    command.translation = translation;
    if (buffering_)
        buffer_.push_back(command);
    else
        Forward(command);
}

void FrameRenderer::RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                 Rml::TextureHandle texture)
{
    ++stats_.draws;
    Fold(2), Fold(state_hash_), Fold(Serial(shader_serials_, shader)), Fold(Serial(geometry_serials_, geometry)), FoldFloat(translation.x), FoldFloat(translation.y),
        Fold(Serial(texture_serials_, texture));
    Command command{Command::Kind::Shader, shader, geometry, texture};
    command.translation = translation;
    if (buffering_)
        buffer_.push_back(command);
    else
        Forward(command);
}

void FrameRenderer::EnableScissorRegion(bool enable)
{
    scissor_enabled_ = enable;
    UpdateStateHash();
    Command command{Command::Kind::Scissor};
    command.flag = enable;
    if (buffering_)
        buffer_.push_back(command);
    else
        Forward(command);
}

void FrameRenderer::SetScissorRegion(Rml::Rectanglei region)
{
    scissor_ = region;
    UpdateStateHash();
    Command command{Command::Kind::ScissorRegion};
    command.region = region;
    if (buffering_)
        buffer_.push_back(command);
    else
        Forward(command);
}

void FrameRenderer::SetTransform(const Rml::Matrix4f *transform)
{
    transform_identity_ = transform == nullptr;
    if (transform)
        transform_ = *transform;
    UpdateStateHash();
    Command command{Command::Kind::Transform};
    command.identity = transform == nullptr;
    if (transform)
        command.matrix = *transform;
    if (buffering_)
        buffer_.push_back(command);
    else
        Forward(command);
}

void FrameRenderer::EnableClipMask(bool enable)
{
    clip_enabled_ = enable;
    UpdateStateHash();
    Command command{Command::Kind::ClipEnable};
    command.flag = enable;
    if (buffering_)
        buffer_.push_back(command);
    else
        Forward(command);
}

void FrameRenderer::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation)
{
    ++stats_.masks;
    const uint64_t shape = Mix(Mix(Mix(Serial(geometry_serials_, geometry), FloatBits(translation.x)), FloatBits(translation.y)), static_cast<uint64_t>(operation));
    Fold(7), Fold(state_hash_), Fold(shape);
    // What the stencil holds: a Set starts it over, an Intersect adds to it.
    clip_hash_ = operation == Rml::ClipMaskOperation::Intersect ? Mix(clip_hash_, shape) : shape;
    UpdateStateHash();
    Command command{Command::Kind::ClipMask, geometry};
    command.translation = translation;
    command.operation = operation;
    if (buffering_)
        buffer_.push_back(command);
    else
        Forward(command);
}

// --- layers --------------------------------------------------------------------------

Rml::LayerHandle FrameRenderer::Real(Rml::LayerHandle layer) const
{
    const auto found = layer_map_.find(layer);
    return found == layer_map_.end() ? layer : found->second;
}

bool FrameRenderer::StateOnly(const Command &command)
{
    switch (command.kind)
    {
    case Command::Kind::Scissor:
    case Command::Kind::ScissorRegion:
    case Command::Kind::Transform:
    case Command::Kind::ClipEnable:
    case Command::Kind::ClipMask:
        return true;
    default:
        return false;
    }
}

void FrameRenderer::Flush()
{
    buffering_ = false;
    // The layer starts from the state it was pushed in, as it would have on the backend.
    if (buffer_scissor_enabled_) // a backend may switch the scissor on with the region: the region only when on
        target_.SetScissorRegion(buffer_scissor_);
    target_.EnableScissorRegion(buffer_scissor_enabled_);
    ApplyTransform(buffer_identity_, buffer_transform_);
    layer_map_[outer_layer_] = target_.PushLayer();
    ++depth_;
    for (const Command &command : buffer_)
        Forward(command);
    buffer_.clear();
}

Rml::LayerHandle FrameRenderer::PushLayer()
{
    hash_marks_.push_back({hash_, false, folded_.size()});
    Fold(8), Fold(state_hash_);
    if (buffering_) // a layer inside the buffered one is part of its content
    {
        Command command{Command::Kind::Push, next_layer_++};
        buffer_.push_back(command);
        ++buffer_depth_;
        return command.a;
    }
    if (depth_ > 0) // inside a layer given to the backend: everything goes there as it comes
    {
        ++depth_;
        return target_.PushLayer();
    }
    buffering_ = true;
    buffer_depth_ = 0;
    outer_layer_ = next_layer_++;
    buffer_scissor_enabled_ = scissor_enabled_;
    buffer_scissor_ = scissor_;
    buffer_identity_ = transform_identity_;
    buffer_transform_ = transform_;
    buffer_clip_ = clip_enabled_ ? Mix(1, clip_hash_) : 0;
    return outer_layer_;
}

std::vector<uint64_t> FrameRenderer::Key(Rml::Span<const Rml::CompiledFilterHandle> filters) const
{
    std::vector<uint64_t> key;
    key.reserve(buffer_.size() * 6 + filters.size() + 24);
    const auto matrix = [&key](bool identity, const Rml::Matrix4f &matrix) {
        key.push_back(identity);
        if (!identity)
            for (int i = 0; i < 16; ++i)
                key.push_back(FloatBits(matrix.data()[i]));
    };
    const auto layer = [this](uintptr_t handle) { return static_cast<uint64_t>(handle - outer_layer_); }; // stable across frames
    key.push_back(buffer_scissor_enabled_);
    if (buffer_scissor_enabled_) // a region left over from earlier does not count while off
    {
        key.push_back(static_cast<uint32_t>(buffer_scissor_.Left()));
        key.push_back(static_cast<uint32_t>(buffer_scissor_.Top()));
        key.push_back(static_cast<uint32_t>(buffer_scissor_.Width()));
        key.push_back(static_cast<uint32_t>(buffer_scissor_.Height()));
    }
    matrix(buffer_identity_, buffer_transform_);
    key.push_back(buffer_clip_); // the stencil the layer's content is drawn against
    for (const Command &command : buffer_)
    {
        key.push_back(static_cast<uint64_t>(command.kind) + 1);
        switch (command.kind)
        {
        case Command::Kind::Geometry:
            key.push_back(Serial(geometry_serials_, command.a));
            key.push_back(Serial(texture_serials_, command.b));
            key.push_back(FloatBits(command.translation.x));
            key.push_back(FloatBits(command.translation.y));
            break;
        case Command::Kind::Shader:
            key.push_back(Serial(shader_serials_, command.a));
            key.push_back(Serial(geometry_serials_, command.b));
            key.push_back(Serial(texture_serials_, command.c));
            key.push_back(FloatBits(command.translation.x));
            key.push_back(FloatBits(command.translation.y));
            break;
        case Command::Kind::Scissor:
        case Command::Kind::ClipEnable:
            key.push_back(command.flag);
            break;
        case Command::Kind::ScissorRegion:
            key.push_back(static_cast<uint32_t>(command.region.Left()));
            key.push_back(static_cast<uint32_t>(command.region.Top()));
            key.push_back(static_cast<uint32_t>(command.region.Width()));
            key.push_back(static_cast<uint32_t>(command.region.Height()));
            break;
        case Command::Kind::Transform:
            matrix(command.identity, command.matrix);
            break;
        case Command::Kind::ClipMask:
            key.push_back(static_cast<uint64_t>(command.operation));
            key.push_back(Serial(geometry_serials_, command.a));
            key.push_back(FloatBits(command.translation.x));
            key.push_back(FloatBits(command.translation.y));
            break;
        case Command::Kind::Push:
            key.push_back(layer(command.a));
            break;
        case Command::Kind::Composite:
            key.push_back(layer(command.a));
            key.push_back(layer(command.b));
            key.push_back(command.c);
            for (Rml::CompiledFilterHandle filter : command.filters)
                key.push_back(Serial(filter_serials_, filter));
            break;
        case Command::Kind::Pop:
            break;
        }
    }
    key.push_back(0xf11'7e75); // the filters and the region they are composited in
    for (Rml::CompiledFilterHandle filter : filters)
        key.push_back(Serial(filter_serials_, filter));
    key.push_back(clip_enabled_ ? Mix(1, clip_hash_) : 0); // the stencil the result is composited through
    key.push_back(static_cast<uint32_t>(scissor_.Left()));
    key.push_back(static_cast<uint32_t>(scissor_.Top()));
    key.push_back(static_cast<uint32_t>(scissor_.Width()));
    key.push_back(static_cast<uint32_t>(scissor_.Height()));
    return key;
}

void FrameRenderer::ApplyTransform(bool identity, const Rml::Matrix4f &matrix)
{
    target_.SetTransform(identity ? nullptr : &matrix);
}

void FrameRenderer::DrawEntry(const Entry &entry)
{
    // The saved result lies in screen space: drawn without the element's transform.
    target_.SetTransform(nullptr);
    target_.RenderGeometry(entry.quad, {0, 0}, entry.texture);
    ApplyTransform(transform_identity_, transform_);
}

void FrameRenderer::SkipBuffer()
{
    for (const Command &command : buffer_)
        if (StateOnly(command))
            Forward(command);
    buffer_.clear();
    buffering_ = false;
    swallow_pop_ = true;
}

void FrameRenderer::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                                    Rml::Span<const Rml::CompiledFilterHandle> filters)
{
    ++stats_.layers;
    Fold(9), Fold(state_hash_), Fold(static_cast<uint64_t>(blend_mode));
    for (Rml::CompiledFilterHandle filter : filters)
        Fold(Serial(filter_serials_, filter));
    if (buffering_ && buffer_depth_ > 0) // inside the buffered layer
    {
        Command command{Command::Kind::Composite, source, destination, static_cast<uintptr_t>(blend_mode)};
        command.filters.assign(filters.begin(), filters.end());
        buffer_.push_back(command);
        return;
    }
    if (buffering_ && source == outer_layer_ && scissor_enabled_ && (scissor_.Width() <= 0 || scissor_.Height() <= 0))
    {
        // Composited into an empty region (an element scrolled out of its list): nothing of
        // it can reach the screen, so the backend draws none of it.
        SkipBuffer();
        return;
    }
    if (!buffering_ || source != outer_layer_ || blend_mode != Rml::BlendMode::Blend || !scissor_enabled_)
    {
        if (buffering_)
            Flush();
        target_.CompositeLayers(Real(source), Real(destination), blend_mode, filters);
        return;
    }

    std::vector<uint64_t> key = Key(filters);
    uint64_t hash = 0;
    for (uint64_t value : key)
        hash = Mix(hash, value);
    auto &bucket = cache_[hash];
    for (Entry &entry : bucket)
        if (entry.key == key)
        {
            // Unchanged since the frame it was made: the layer, its draws and the filter pass
            // are skipped; the state they leave behind (scissor, transform, and the stencil,
            // which every layer shares) is set as they would have set it.
            SkipBuffer();
            entry.used = frame_;
            ++stats_.cached_layers;
            DrawEntry(entry);
            return;
        }

    // First time: the backend draws the layer, filters it into a second layer and keeps
    // the result as a texture, which is then drawn where the composite would have gone.
    Flush();
    const Rml::LayerHandle filtered = target_.PushLayer();
    target_.CompositeLayers(Real(outer_layer_), filtered, Rml::BlendMode::Replace, filters);
    Entry entry;
    entry.key = std::move(key);
    entry.used = frame_;
    entry.texture = target_.SaveLayerAsTexture();
    target_.PopLayer();
    target_.PopLayer();
    --depth_;
    swallow_pop_ = true;
    if (!entry.texture)
    {
        // Every backend must save layers (RmlUi's box shadows need it too): said aloud, not hidden.
        Rml::Log::Message(Rml::Log::LT_ERROR, "UI renderer: the backend did not save a filtered layer as a texture; the element is not drawn");
        return;
    }
    Rml::Mesh mesh;
    const Rml::Vector2f origin(static_cast<float>(scissor_.Left()), static_cast<float>(scissor_.Top()));
    const Rml::Vector2f size(static_cast<float>(scissor_.Width()), static_cast<float>(scissor_.Height()));
    Rml::MeshUtilities::GenerateQuad(mesh, origin, size, Rml::ColourbPremultiplied(255, 255, 255, 255), {0, 0}, {1, 1});
    entry.quad = target_.CompileGeometry(mesh.vertices, mesh.indices);
    DrawEntry(entry);
    bucket.push_back(std::move(entry));
}

void FrameRenderer::PopLayer()
{
    Fold(10);
    if (!hash_marks_.empty())
    {
        // A layer that only made a texture (a box shadow, made once) is not part of what
        // the frame shows: the frames before and after it draw the same.
        if (hash_marks_.back().saved)
        {
            hash_ = hash_marks_.back().hash;
            if (record_)
                folded_.resize(hash_marks_.back().folded);
        }
        hash_marks_.pop_back();
    }
    if (swallow_pop_)
    {
        swallow_pop_ = false;
        return;
    }
    if (buffering_)
    {
        if (buffer_depth_ > 0)
        {
            buffer_.push_back(Command{Command::Kind::Pop});
            --buffer_depth_;
            return;
        }
        // Popped without a composite: nothing of it reaches the screen.
        SkipBuffer();
        swallow_pop_ = false;
        return;
    }
    --depth_;
    target_.PopLayer();
}

Rml::TextureHandle FrameRenderer::SaveLayerAsTexture()
{
    Fold(11);
    if (!hash_marks_.empty())
        hash_marks_.back().saved = true;
    if (buffering_) // a texture made from a layer (a box shadow): the backend must draw now
        Flush();
    ++stats_.textures;
    const Rml::TextureHandle texture = target_.SaveLayerAsTexture();
    texture_serials_[texture] = next_serial_++;
    return texture;
}

Rml::CompiledFilterHandle FrameRenderer::SaveLayerAsMaskImage()
{
    Fold(12);
    if (buffering_)
        Flush();
    const Rml::CompiledFilterHandle filter = target_.SaveLayerAsMaskImage();
    filter_serials_[filter] = next_serial_++;
    return filter;
}

} // namespace storm::ui
