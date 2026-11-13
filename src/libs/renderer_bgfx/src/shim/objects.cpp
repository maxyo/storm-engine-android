#include "device.hpp"
#include "perf.hpp"

#include <chrono>
#include <thread>

#include "../texture/core/service.hpp"

#include <bx/error.h>
#include <bx/readerwriter.h>

#include <algorithm>

namespace storm::shim
{

// ---------------------------------------------------------------------------
// formats
FormatInfo format_info(D3DFORMAT format)
{
    switch (static_cast<uint32_t>(format))
    {
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
        return {2, 0, true};
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D32:
    case D3DFMT_D24X4S4:
    case D3DFMT_D15S1:
        return {4, 0, true};
    default:
        break;
    }
    const bimg::TextureFormat::Enum texels = texture::d3d_format(static_cast<uint32_t>(format)).format;
    if (texels == bimg::TextureFormat::Unknown)
    {
        trap("unsupported D3DFORMAT (sized as 32-bit; texture/core d3d_format lists the supported ones)");
        return {4, 0, false};
    }
    const bimg::ImageBlockInfo &block = bimg::getBlockInfo(texels);
    if (bimg::isCompressed(texels))
        return {0, block.blockSize, false};
    return {static_cast<uint8_t>(block.bitsPerPixel / 8), 0, false};
}

uint32_t level_pitch(D3DFORMAT format, uint32_t width)
{
    const FormatInfo info = format_info(format);
    return info.block_bytes ? std::max(1u, (width + 3) / 4) * info.block_bytes : width * info.bytes_per_pixel;
}

uint32_t level_size(D3DFORMAT format, uint32_t width, uint32_t height)
{
    const FormatInfo info = format_info(format);
    return info.block_bytes ? level_pitch(format, width) * std::max(1u, (height + 3) / 4)
                            : level_pitch(format, width) * height;
}

// ---------------------------------------------------------------------------
// TexStore
TexStorePtr make_store(Device *device, TexKind kind, uint32_t width, uint32_t height, uint32_t depth, uint32_t levels,
                       DWORD usage, D3DFORMAT format, D3DPOOL pool)
{
    auto store = std::make_shared<TexStore>();
    store->device = device;
    store->kind = kind;
    store->width = std::max(1u, width);
    store->height = std::max(1u, height);
    store->depth = std::max(1u, depth);
    uint32_t full = 1;
    for (uint32_t size = std::max({store->width, store->height, kind == TexKind::Volume ? store->depth : 1u}); size > 1;
         size >>= 1)
        ++full;
    store->levels = levels == 0 ? full : std::min(levels, full);
    store->usage = usage;
    store->format = format;
    store->pool = pool;
    store->depth_stencil = (usage & D3DUSAGE_DEPTHSTENCIL) != 0 || format_info(format).depth;
    store->render_target = (usage & D3DUSAGE_RENDERTARGET) != 0;
    if (store->render_target || store->depth_stencil)
        store->levels = (usage & D3DUSAGE_AUTOGENMIPMAP) ? store->levels : std::min(store->levels, levels ? levels : 1u);
    store->subs.resize(static_cast<size_t>(store->faces()) * store->levels);
    return store;
}

TexStore::~TexStore()
{
    for (const Sub &s : subs)
        perf_resize(&render::PerfCounters::texture_shadow_bytes, s.data.size(), 0);
    texture::Service::instance().release(transcode_job);
    texture::Service::instance().add_fallback_bytes(-fallback_bytes);
    if (bgfx::isValid(handle) && Context::instance().initialised())
    {
        if (device)
            device->purge_framebuffers(handle);
        bgfx::destroy(handle);
    }
}

uint8_t *TexStore::lock(uint32_t face, uint32_t level, uint32_t *pitch, uint32_t *slice_pitch, bool read_only)
{
    if (face >= faces() || level >= levels)
        return nullptr;
    Sub &s = sub(face, level);
    const uint32_t w = level_width(level), h = level_height(level);
    const uint32_t slice = level_size(format, w, h);
    const size_t total = static_cast<size_t>(slice) * level_depth(level);
    if (s.data.size() != total)
    {
        if (shadow_released)
            trap("lock of a texture whose CPU copy was released after upload (compressed or immutable; contents read as zero)");
        perf_resize(&render::PerfCounters::texture_shadow_bytes, s.data.size(), total);
        s.data.assign(total, 0);
    }
    ++s.lock_count;
    s.written |= !read_only;
    if (pitch)
        *pitch = level_pitch(format, w);
    if (slice_pitch)
        *slice_pitch = slice;
    return s.data.data();
}

void TexStore::unlock(uint32_t face, uint32_t level)
{
    if (face >= faces() || level >= levels)
        return;
    Sub &s = sub(face, level);
    if (!s.written)
        return;
    s.written = false;
    s.dirty = true;
    any_dirty = true;
}

void TexStore::create_gpu()
{
    const FormatInfo info = format_info(format);
    uint64_t flags = 0;
    if (depth_stencil)
    {
        gpu_format = bgfx::TextureFormat::D24S8;
        flags |= BGFX_TEXTURE_RT;
    }
    else if (info.block_bytes && !render_target)
    {
        gpu_format = info.block_bytes == 8 ? bgfx::TextureFormat::BC1
                     : (format == D3DFMT_DXT2 || format == D3DFMT_DXT3) ? bgfx::TextureFormat::BC2
                                                                         : bgfx::TextureFormat::BC3;
        gpu_compressed = true;
    }
    else if (format == D3DFMT_A32B32G32R32F && !render_target)
        gpu_format = bgfx::TextureFormat::RGBA32F; // uploaded as it is (same channel order)
    else
    {
        gpu_format = bgfx::TextureFormat::RGBA8;
        if (render_target)
            flags |= BGFX_TEXTURE_RT;
    }
    flags |= target_flags;
    const bool mips = levels > 1;
    switch (kind)
    {
    case TexKind::Tex2D:
        handle = bgfx::createTexture2D(static_cast<uint16_t>(width), static_cast<uint16_t>(height), mips, 1, gpu_format,
                                       flags);
        break;
    case TexKind::Cube:
        handle = bgfx::createTextureCube(static_cast<uint16_t>(width), mips, 1, gpu_format, flags);
        break;
    case TexKind::Volume:
        handle = bgfx::createTexture3D(static_cast<uint16_t>(width), static_cast<uint16_t>(height),
                                       static_cast<uint16_t>(depth), mips, gpu_format, flags);
        break;
    }
    if (!bgfx::isValid(handle))
        logf("texture creation failed: %ux%ux%u levels=%u d3dformat=%u", width, height, depth, levels,
             static_cast<uint32_t>(format));
    else if (render_target)
        needs_clear = true;
}

namespace
{
bgfx::TextureFormat::Enum bc_format(D3DFORMAT format)
{
    return format_info(format).block_bytes == 8                    ? bgfx::TextureFormat::BC1
           : (format == D3DFMT_DXT2 || format == D3DFMT_DXT3) ? bgfx::TextureFormat::BC2
                                                               : bgfx::TextureFormat::BC3;
}

texture::Layout layout_of(const TexStore &store)
{
    const bgfx::TextureFormat::Enum bc = bc_format(store.format);
    texture::Layout layout;
    layout.format = bc == bgfx::TextureFormat::BC1   ? texture::DxtFormat::Bc1
                    : bc == bgfx::TextureFormat::BC2 ? texture::DxtFormat::Bc2
                                                     : texture::DxtFormat::Bc3;
    layout.width = store.width;
    layout.height = store.height;
    layout.levels = store.levels;
    layout.faces = store.faces();
    return layout;
}

// bgfx keeps a reference to the shared blob until it has consumed the upload.
const bgfx::Memory *ref_blob(std::shared_ptr<const texture::Blob> blob)
{
    auto *owner = new std::shared_ptr<const texture::Blob>(std::move(blob));
    return bgfx::makeRef((*owner)->data.get(), static_cast<uint32_t>((*owner)->size),
                         [](void *, void *user) { delete static_cast<std::shared_ptr<const texture::Blob> *>(user); },
                         owner);
}

enum class Tier
{
    Direct,    // DXT uploaded as BC (GPU supports S3TC)
    Transcode, // ETC2 from the cache or the transcode workers
    Rgba,      // decoded to RGBA8 here
};

const char *tier_name(Tier tier)
{
    return tier == Tier::Direct ? "bc" : tier == Tier::Transcode ? "etc2" : "rgba8";
}

// The tier for DXT payloads is fixed per texture kind when the first texture is created:
// it depends only on the GPU capabilities and the `texture_compression` setting, and it is
// logged once. A request the GPU cannot honour is reported as an error, never applied quietly.
Tier resolve_tier(uint16_t need, const char *kind)
{
    const bgfx::Caps *caps = Context::instance().caps();
    const bool bc = (caps->formats[bgfx::TextureFormat::BC1] & need) && (caps->formats[bgfx::TextureFormat::BC2] & need) &&
                    (caps->formats[bgfx::TextureFormat::BC3] & need);
    const bool etc2 = (caps->formats[bgfx::TextureFormat::ETC2] & need) && (caps->formats[bgfx::TextureFormat::ETC2A] & need);
    const Tier best = bc ? Tier::Direct : etc2 ? Tier::Transcode : Tier::Rgba;
    Tier tier = best;
    const char *requested = "auto";
    switch (texture::Service::instance().mode())
    {
    case texture::Mode::Bc:
        requested = "bc";
        tier = bc ? Tier::Direct : best;
        break;
    case texture::Mode::Etc2:
        requested = "etc2";
        tier = etc2 ? Tier::Transcode : best;
        break;
    case texture::Mode::Rgba:
        requested = "rgba";
        tier = Tier::Rgba;
        break;
    case texture::Mode::Auto:
        break;
    }
    if (texture::Service::instance().mode() != texture::Mode::Auto && tier == best && tier != Tier::Rgba &&
        strcmp(requested, tier_name(tier)) != 0)
        logf("ERROR: texture_compression=%s is not supported by this GPU for %s textures; using %s", requested, kind,
             tier_name(tier));
    if (tier == Tier::Rgba && texture::Service::instance().mode() != texture::Mode::Rgba)
        logf("ERROR: this GPU supports neither S3TC nor ETC2 for %s textures: DXT data is decoded to RGBA8 "
             "(up to 8x the texture memory)", kind);
    logf("texture tier for DXT %s textures: %s (requested %s, S3TC=%d, ETC2=%d)", kind, tier_name(tier), requested, bc, etc2);
    return tier;
}

Tier select_tier(const TexStore &store)
{
    if (!format_info(store.format).block_bytes)
        return Tier::Direct;
    static const Tier tier_2d = resolve_tier(BGFX_CAPS_FORMAT_TEXTURE_2D, "2D");
    static const Tier tier_cube = resolve_tier(BGFX_CAPS_FORMAT_TEXTURE_CUBE, "cube");
    return store.kind == TexKind::Cube ? tier_cube : tier_2d;
}

// The bgfx format of a sampled texture's D3D texels as they are (texture/core d3d_format). Its support on this GPU
// is logged once per format and kind: native, converted to RGBA8 by bgfx at upload (emulated), or missing (an error).
bgfx::TextureFormat::Enum texel_format(const TexStore &store)
{
    const auto format = static_cast<bgfx::TextureFormat::Enum>(texture::d3d_format(static_cast<uint32_t>(store.format)).format);
    const uint16_t native = store.kind == TexKind::Cube     ? BGFX_CAPS_FORMAT_TEXTURE_CUBE
                            : store.kind == TexKind::Volume ? BGFX_CAPS_FORMAT_TEXTURE_3D
                                                            : BGFX_CAPS_FORMAT_TEXTURE_2D;
    const uint16_t emulated = store.kind == TexKind::Cube     ? BGFX_CAPS_FORMAT_TEXTURE_CUBE_EMULATED
                              : store.kind == TexKind::Volume ? BGFX_CAPS_FORMAT_TEXTURE_3D_EMULATED
                                                              : BGFX_CAPS_FORMAT_TEXTURE_2D_EMULATED;
    static bool reported[bgfx::TextureFormat::Count][3] = {};
    bool &done = reported[format][static_cast<int>(store.kind)];
    if (!done)
    {
        done = true;
        const uint16_t caps = Context::instance().caps()->formats[format];
        const char *kind = store.kind == TexKind::Cube ? "cube" : store.kind == TexKind::Volume ? "3D" : "2D";
        if (caps & native)
            logf("texture format %s (D3DFORMAT %u) for %s textures: native", bimg::getName(bimg::TextureFormat::Enum(format)),
                 static_cast<uint32_t>(store.format), kind);
        else if (caps & emulated)
            logf("texture format %s (D3DFORMAT %u) for %s textures: not supported by this GPU, bgfx converts it to RGBA8 "
                 "at upload (twice the memory of 16-bit formats)",
                 bimg::getName(bimg::TextureFormat::Enum(format)), static_cast<uint32_t>(store.format), kind);
        else
            logf("ERROR: texture format %s (D3DFORMAT %u) for %s textures is neither supported nor emulated: such "
                 "textures fail to create",
                 bimg::getName(bimg::TextureFormat::Enum(format)), static_cast<uint32_t>(store.format), kind);
    }
    return format;
}

// One level of the CPU copy as the GPU takes it in `gpu_format`: the texels as they are, the unused bits of the X
// formats set (texture::set_opaque), or DXT decoded to RGBA8 (the Rgba tier). `out`: level_upload_size() bytes.
size_t level_upload_size(const TexStore &store, bgfx::TextureFormat::Enum gpu_format, uint32_t level)
{
    const uint32_t w = store.level_width(level), h = store.level_height(level);
    const size_t slice = format_info(store.format).block_bytes && gpu_format == bgfx::TextureFormat::RGBA8
                             ? static_cast<size_t>(w) * h * 4
                             : level_size(store.format, w, h);
    return slice * store.level_depth(level);
}

void write_level(const TexStore &store, const TexStore::Sub &sub, bgfx::TextureFormat::Enum gpu_format, uint32_t level,
                 uint8_t *out)
{
    const size_t bytes = level_upload_size(store, gpu_format, level);
    if (sub.data.empty())
    {
        memset(out, 0, bytes);
        return;
    }
    const uint32_t w = store.level_width(level), h = store.level_height(level), d = store.level_depth(level);
    if (format_info(store.format).block_bytes && gpu_format == bgfx::TextureFormat::RGBA8)
    {
        const size_t slice_bytes = level_size(store.format, w, h);
        for (uint32_t z = 0; z < d; ++z)
            texture::decode_rgba8(static_cast<uint32_t>(store.format), sub.data.data() + z * slice_bytes, w, h,
                                  out + static_cast<size_t>(z) * w * h * 4);
        return;
    }
    memcpy(out, sub.data.data(), bytes);
    const texture::D3dFormat texels = texture::d3d_format(static_cast<uint32_t>(store.format));
    if (texels.opaque)
        texture::set_opaque(texels.format, out, static_cast<size_t>(w) * h * d);
}

// bgfx can only restrict the mip range of a texture created from an image container;
// D3D textures with a partial mip chain (very common in .tx files) need that. bimg writes the DDS.
// `first_level` drops the largest levels (temporary textures while transcoding).
const bgfx::Memory *make_dds(const TexStore &store, bgfx::TextureFormat::Enum gpu_format, uint32_t first_level = 0)
{
    std::vector<uint8_t> payload; // face-major, level-minor, as DDS
    size_t total = 0;
    for (uint32_t level = first_level; level < store.levels; ++level)
        total += level_upload_size(store, gpu_format, level);
    payload.resize(total * store.faces());
    uint8_t *out = payload.data();
    for (uint32_t face = 0; face < store.faces(); ++face)
        for (uint32_t level = first_level; level < store.levels; ++level)
        {
            write_level(store, store.subs[face * store.levels + level], gpu_format, level, out);
            out += level_upload_size(store, gpu_format, level);
        }
    bimg::ImageContainer image{};
    image.m_format = static_cast<bimg::TextureFormat::Enum>(gpu_format);
    image.m_offset = UINT32_MAX;
    image.m_data = payload.data();
    image.m_size = static_cast<uint32_t>(payload.size());
    image.m_width = store.level_width(first_level);
    image.m_height = store.level_height(first_level);
    image.m_depth = store.kind == TexKind::Volume ? store.level_depth(first_level) : 0; // DDS: 0 unless a volume
    image.m_numLayers = 1;
    image.m_numMips = static_cast<uint8_t>(store.levels - first_level);
    image.m_cubeMap = store.kind == TexKind::Cube;
    bx::Error error;
    bx::SizerWriter sizer;
    const int32_t size = bimg::imageWriteDds(&sizer, image, nullptr, 0, &error);
    const bgfx::Memory *memory = bgfx::alloc(static_cast<uint32_t>(size));
    bx::StaticMemoryBlockWriter writer(memory->data, memory->size);
    bimg::imageWriteDds(&writer, image, nullptr, 0, &error);
    if (!error.isOk())
        trap("DDS container of a texture could not be written (bimg::imageWriteDds)");
    return memory;
}

void submit_transcode(TexStore &store)
{
    texture::Service &service = texture::Service::instance();
    if (!service.has_room())
        return;
    for (const TexStore::Sub &sub : store.subs)
        if (sub.data.empty())
        {
            trap("ETC2 transcode requested without the texture's DXT data");
            return;
        }
    std::vector<uint8_t> input;
    size_t total = 0;
    for (const TexStore::Sub &sub : store.subs)
        total += sub.data.size();
    input.reserve(total);
    for (const TexStore::Sub &sub : store.subs)
        input.insert(input.end(), sub.data.begin(), sub.data.end());
    const texture::Layout layout = layout_of(store);
    const texture::EtcFormat format = texture::choose_etc_format(layout.format, input.data(), input.size());
    store.transcode_job = service.submit(layout, store.transcode_key, format, std::move(input));
}

bgfx::TextureFormat::Enum etc_format(texture::EtcFormat format)
{
    return format == texture::EtcFormat::Rgb8 ? bgfx::TextureFormat::ETC2 : bgfx::TextureFormat::ETC2A;
}
} // namespace

// DXT payloads are never patched in place by the game, and file textures are marked immutable by
// the renderer, so their CPU copy (the bulk of all texture memory) is dropped once the GPU texture
// exists and no transcode job still needs it.
void TexStore::release_shadow()
{
    if ((!format_info(format).block_bytes && !immutable) || render_target || (fallback_bytes && !transcode_job))
        return;
    for (Sub &s : subs)
    {
        perf_resize(&render::PerfCounters::texture_shadow_bytes, s.data.size(), 0);
        std::vector<uint8_t>().swap(s.data);
    }
    shadow_released = true;
}

void TexStore::create_sampled()
{
    const Tier tier = select_tier(*this);
    if (tier == Tier::Transcode && create_transcoded())
    {
        for (Sub &s : subs)
            s.dirty = false;
        any_dirty = false;
        release_shadow();
        return;
    }
    gpu_compressed = format_info(format).block_bytes != 0 && tier != Tier::Rgba;
    gpu_format = !format_info(format).block_bytes ? texel_format(*this)
                 : gpu_compressed                 ? bc_format(format)
                                                  : bgfx::TextureFormat::RGBA8;
    handle = bgfx::createTexture(counted_texture_upload(make_dds(*this, gpu_format)));
    if (!bgfx::isValid(handle))
        logf("texture creation failed: %ux%u levels=%u d3dformat=%u", width, height, levels,
             static_cast<uint32_t>(format));
    // a decoded DXT texture cannot take partial DXT updates: re-locking recreates it
    transcoded = tier == Tier::Rgba && format_info(format).block_bytes != 0;
    for (Sub &s : subs)
        s.dirty = false;
    any_dirty = false;
    if (bgfx::isValid(handle))
        release_shadow();
}

bool TexStore::create_transcoded()
{
    for (const Sub &s : subs)
        if (s.data.empty())
            return false;
    const texture::Layout layout = layout_of(*this);
    texture::KeyBuilder key(layout);
    for (const Sub &s : subs)
        key.add(s.data.data(), s.data.size());
    transcode_key = key.key();
    texture::Service &service = texture::Service::instance();
    if (auto blob = service.lookup(transcode_key, layout))
    {
        const uint32_t internal_format = blob->data[28] | blob->data[29] << 8;
        handle = bgfx::createTexture(counted_texture_upload(ref_blob(std::make_shared<const texture::Blob>(std::move(*blob)))));
        gpu_format = internal_format == 0x9274 ? bgfx::TextureFormat::ETC2 : bgfx::TextureFormat::ETC2A;
        gpu_compressed = true;
        transcoded = bgfx::isValid(handle);
        return transcoded;
    }
    // Miss: show the DXT data now (without its top level when large, which
    // bounds the RGBA8 copy bgfx makes on GPUs without S3TC) and swap in the
    // ETC2 texture once a worker has produced it.
    const uint32_t first_level = levels > 1 && std::max(width, height) >= 1024 ? 1 : 0;
    gpu_compressed = true;
    gpu_format = bc_format(format);
    handle = bgfx::createTexture(counted_texture_upload(make_dds(*this, gpu_format, first_level)));
    if (!bgfx::isValid(handle))
        return false;
    transcoded = true;
    const bool bc_native = Context::instance().caps()->formats[gpu_format] &
                           (kind == TexKind::Cube ? BGFX_CAPS_FORMAT_TEXTURE_CUBE : BGFX_CAPS_FORMAT_TEXTURE_2D);
    for (uint32_t level = first_level; level < levels; ++level)
        fallback_bytes += bc_native ? level_size(format, level_width(level), level_height(level))
                                    : int64_t(level_width(level)) * level_height(level) * 4;
    fallback_bytes *= faces();
    service.add_fallback_bytes(fallback_bytes);
    submit_transcode(*this);
    return true;
}

// The interim (DXT decoded, top level dropped when large) texture becomes final: reported, never silent.
void TexStore::keep_interim(const char *reason)
{
    trap(reason);
    texture::Service &service = texture::Service::instance();
    service.release(transcode_job);
    service.add_fallback_bytes(-fallback_bytes);
    fallback_bytes = 0;
}

void TexStore::finish_transcode()
{
    if (!fallback_bytes)
        return;
    texture::Service &service = texture::Service::instance();
    if (!transcode_job)
    {
        // The queue was full at creation, or a job failed: (re)submit while the DXT data is still here.
        if (shadow_released)
        {
            keep_interim("ETC2 transcode cannot be retried: the DXT data was already released");
            return;
        }
        submit_transcode(*this);
        release_shadow(); // only once a job holds a copy of the data
        return;
    }
    const int state = transcode_job->state.load(std::memory_order_acquire);
    if (state == texture::Job::Failed)
    {
        service.release(transcode_job);
        if (shadow_released)
            keep_interim("ETC2 transcode failed and the DXT data is gone; the texture keeps its interim copy");
        return; // otherwise resubmitted on the next call
    }
    if (state != texture::Job::Done)
        return;
    const bgfx::TextureHandle etc = bgfx::createTexture(counted_texture_upload(ref_blob(transcode_job->result)));
    const bgfx::TextureFormat::Enum etc_gpu_format = etc_format(transcode_job->format);
    if (!bgfx::isValid(etc))
    {
        keep_interim("creating the transcoded ETC2 texture failed; the texture keeps its interim copy");
        return;
    }
    service.release(transcode_job);
    bgfx::destroy(handle);
    handle = etc;
    gpu_format = etc_gpu_format;
    texture::Service::instance().add_fallback_bytes(-fallback_bytes);
    fallback_bytes = 0;
}

// For holders of the bgfx handle outside the shim (UI game pictures): the handle must not change
// afterwards, so a pending ETC2 transcode is completed here instead of being swapped in later.
void TexStore::settle()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (transcoded && fallback_bytes)
    {
        finish_transcode();
        if (!fallback_bytes)
            break;
        if (std::chrono::steady_clock::now() > deadline)
        {
            keep_interim("ETC2 transcode of a pinned texture did not finish; it keeps its interim copy");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void TexStore::destroy_sampled()
{
    texture::Service::instance().release(transcode_job);
    texture::Service::instance().add_fallback_bytes(-fallback_bytes);
    fallback_bytes = 0;
    transcoded = false;
    shadow_released = false;
    if (bgfx::isValid(handle))
        bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
}

void TexStore::sync()
{
    if (system_memory)
        return;
    // Float textures (bone palettes) skip the DDS/transcode path: created empty, levels uploaded raw.
    const bool sampled = !render_target && !depth_stencil && format != D3DFMT_A32B32G32R32F;
    if (transcoded)
    {
        if (!any_dirty)
        {
            finish_transcode();
            return;
        }
        destroy_sampled(); // re-locked: start over from the new contents
    }
    if (!bgfx::isValid(handle))
    {
        if (sampled)
        {
            create_sampled();
            return;
        }
        create_gpu();
        if (!bgfx::isValid(handle))
            return;
    }
    if (!any_dirty || render_target || depth_stencil)
    {
        any_dirty = false;
        return;
    }
    any_dirty = false;
    for (uint32_t face = 0; face < faces(); ++face)
        for (uint32_t level = 0; level < levels; ++level)
        {
            Sub &s = sub(face, level);
            if (!s.dirty || s.data.empty())
                continue;
            s.dirty = false;
            const uint32_t w = level_width(level), h = level_height(level), d = level_depth(level);
            const bgfx::Memory *memory = bgfx::alloc(static_cast<uint32_t>(level_upload_size(*this, gpu_format, level)));
            write_level(*this, s, gpu_format, level, memory->data);
            counted_texture_upload(memory);
            switch (kind)
            {
            case TexKind::Tex2D:
                bgfx::updateTexture2D(handle, 0, static_cast<uint8_t>(level), 0, 0, static_cast<uint16_t>(w),
                                      static_cast<uint16_t>(h), memory);
                break;
            case TexKind::Cube:
                bgfx::updateTextureCube(handle, 0, static_cast<uint8_t>(face), static_cast<uint8_t>(level), 0, 0,
                                        static_cast<uint16_t>(w), static_cast<uint16_t>(h), memory);
                break;
            case TexKind::Volume:
                bgfx::updateTexture3D(handle, static_cast<uint8_t>(level), 0, 0, 0, static_cast<uint16_t>(w),
                                      static_cast<uint16_t>(h), static_cast<uint16_t>(d), memory);
                break;
            }
        }
}

TexStore *store_of(BaseTexture *texture)
{
    if (!texture)
        return nullptr;
    switch (texture->GetType())
    {
    case D3DRTYPE_TEXTURE:
        return static_cast<Texture2D *>(texture)->store.get();
    case D3DRTYPE_CUBETEXTURE:
        return static_cast<CubeTexture *>(texture)->store.get();
    case D3DRTYPE_VOLUMETEXTURE:
        return static_cast<VolumeTexture *>(texture)->store.get();
    default:
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// surfaces and textures
namespace
{
void fill_desc(const TexStore &store, uint32_t level, D3DSURFACE_DESC *desc)
{
    *desc = {};
    desc->Format = store.format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = store.usage;
    desc->Pool = store.pool;
    desc->Width = store.addressed_width(level);
    desc->Height = store.addressed_height(level);
}

HRESULT lock_rect(TexStore &store, uint32_t face, uint32_t level, D3DLOCKED_RECT *locked, const RECT *rect,
                  DWORD flags)
{
    if (!locked)
        return D3DERR_INVALIDCALL;
    uint32_t pitch = 0;
    uint8_t *data = store.lock(face, level, &pitch, nullptr, (flags & D3DLOCK_READONLY) != 0);
    if (!data)
        return D3DERR_INVALIDCALL;
    locked->Pitch = static_cast<INT>(pitch);
    if (rect)
    {
        const FormatInfo info = format_info(store.format);
        if (info.block_bytes)
            data += static_cast<size_t>(rect->top / 4) * pitch + static_cast<size_t>(rect->left / 4) * info.block_bytes;
        else
            data += static_cast<size_t>(rect->top) * pitch + static_cast<size_t>(rect->left) * info.bytes_per_pixel;
    }
    locked->pBits = data;
    return D3D_OK;
}
} // namespace

HRESULT Surface::GetDesc(D3DSURFACE_DESC *desc)
{
    fill_desc(*store, level, desc);
    return D3D_OK;
}
HRESULT Surface::LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    return lock_rect(*store, face, level, locked, rect, flags);
}
HRESULT Surface::UnlockRect()
{
    store->unlock(face, level);
    return D3D_OK;
}

HRESULT Texture2D::GetLevelDesc(UINT level, D3DSURFACE_DESC *desc)
{
    if (level >= store->levels)
        return D3DERR_INVALIDCALL;
    fill_desc(*store, level, desc);
    return D3D_OK;
}
HRESULT Texture2D::GetSurfaceLevel(UINT level, Surface **surface)
{
    if (level >= store->levels)
        return D3DERR_INVALIDCALL;
    *surface = new Surface(store, 0, level);
    return D3D_OK;
}
HRESULT Texture2D::LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    return lock_rect(*store, 0, level, locked, rect, flags);
}
HRESULT Texture2D::UnlockRect(UINT level)
{
    store->unlock(0, level);
    return D3D_OK;
}

HRESULT CubeTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC *desc)
{
    if (level >= store->levels)
        return D3DERR_INVALIDCALL;
    fill_desc(*store, level, desc);
    return D3D_OK;
}
HRESULT CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, Surface **surface)
{
    if (level >= store->levels || static_cast<uint32_t>(face) >= 6)
        return D3DERR_INVALIDCALL;
    *surface = new Surface(store, static_cast<uint32_t>(face), level);
    return D3D_OK;
}
HRESULT CubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked, const RECT *rect,
                                DWORD flags)
{
    return lock_rect(*store, static_cast<uint32_t>(face), level, locked, rect, flags);
}
HRESULT CubeTexture::UnlockRect(D3DCUBEMAP_FACES face, UINT level)
{
    store->unlock(static_cast<uint32_t>(face), level);
    return D3D_OK;
}

HRESULT VolumeTexture::GetLevelDesc(UINT level, D3DVOLUME_DESC *desc)
{
    if (level >= store->levels)
        return D3DERR_INVALIDCALL;
    *desc = {};
    desc->Format = store->format;
    desc->Type = D3DRTYPE_VOLUME;
    desc->Usage = store->usage;
    desc->Pool = store->pool;
    desc->Width = store->level_width(level);
    desc->Height = store->level_height(level);
    desc->Depth = store->level_depth(level);
    return D3D_OK;
}
HRESULT VolumeTexture::LockBox(UINT level, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags)
{
    uint32_t pitch = 0, slice = 0;
    uint8_t *data = store->lock(0, level, &pitch, &slice, (flags & D3DLOCK_READONLY) != 0);
    if (!data || !locked)
        return D3DERR_INVALIDCALL;
    locked->RowPitch = static_cast<INT>(pitch);
    locked->SlicePitch = static_cast<INT>(slice);
    if (box)
        data += static_cast<size_t>(box->Front) * slice + static_cast<size_t>(box->Top) * pitch +
                static_cast<size_t>(box->Left) * format_info(store->format).bytes_per_pixel;
    locked->pBits = data;
    return D3D_OK;
}
HRESULT VolumeTexture::UnlockBox(UINT level)
{
    store->unlock(0, level);
    return D3D_OK;
}

// ---------------------------------------------------------------------------
// buffers
void BufferBase::mark(uint32_t offset, uint32_t size)
{
    dirty_begin = std::min(dirty_begin, offset);
    dirty_end = std::max(dirty_end, offset + size);
    write_frame = Context::instance().frame_index();
    ++write_serial;
}

VertexBuffer::~VertexBuffer()
{
    if (device && Context::instance().initialised()) // the device is gone once the context is shut down
        device->forget_transient(this);
    if (bgfx::isValid(handle) && Context::instance().initialised())
        bgfx::destroy(handle);
    perf_resize(&render::PerfCounters::buffer_shadow_bytes, shadow.size(), 0);
    perf_resize(&render::PerfCounters::buffer_gpu_bytes, gpu_bytes, 0);
}
HRESULT VertexBuffer::Lock(UINT offset, UINT size, void **data, DWORD flags)
{
    if (!data || offset > shadow.size())
        return D3DERR_INVALIDCALL;
    if (size == 0 || offset + size > shadow.size())
        size = static_cast<UINT>(shadow.size()) - offset;
    *data = shadow.data() + offset;
    ++lock_count;
    if (!(flags & D3DLOCK_READONLY))
        mark(offset, size);
    return D3D_OK;
}
HRESULT VertexBuffer::Unlock()
{
    if (lock_count)
        --lock_count;
    return D3D_OK;
}
HRESULT VertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC *desc)
{
    *desc = {};
    desc->Format = D3DFMT_VERTEXDATA;
    desc->Type = D3DRTYPE_VERTEXBUFFER;
    desc->Usage = usage;
    desc->Pool = D3DPOOL_MANAGED;
    desc->Size = static_cast<UINT>(shadow.size());
    desc->FVF = fvf;
    return D3D_OK;
}

IndexBuffer::~IndexBuffer()
{
    if (bgfx::isValid(handle) && Context::instance().initialised())
        bgfx::destroy(handle);
    perf_resize(&render::PerfCounters::buffer_shadow_bytes, shadow.size(), 0);
    perf_resize(&render::PerfCounters::buffer_gpu_bytes, gpu_bytes, 0);
}
HRESULT IndexBuffer::Lock(UINT offset, UINT size, void **data, DWORD flags)
{
    if (!data || offset > shadow.size())
        return D3DERR_INVALIDCALL;
    if (size == 0 || offset + size > shadow.size())
        size = static_cast<UINT>(shadow.size()) - offset;
    *data = shadow.data() + offset;
    ++lock_count;
    if (!(flags & D3DLOCK_READONLY))
        mark(offset, size);
    return D3D_OK;
}
HRESULT IndexBuffer::Unlock()
{
    if (lock_count)
        --lock_count;
    return D3D_OK;
}
HRESULT IndexBuffer::GetDesc(D3DINDEXBUFFER_DESC *desc)
{
    *desc = {};
    desc->Format = index32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16;
    desc->Type = D3DRTYPE_INDEXBUFFER;
    desc->Usage = usage;
    desc->Pool = D3DPOOL_MANAGED;
    desc->Size = static_cast<UINT>(shadow.size());
    return D3D_OK;
}

} // namespace storm::shim
