#include "device.hpp"

#include <algorithm>

namespace storm::shim
{

// ---------------------------------------------------------------------------
// formats
FormatInfo format_info(D3DFORMAT format)
{
    switch (static_cast<uint32_t>(format))
    {
    case D3DFMT_A8R8G8B8:
        return {4, 0, false, true};
    case D3DFMT_X8R8G8B8:
        return {4, 0, false, false};
    case D3DFMT_R8G8B8:
        return {3, 0, false, false};
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_X4R4G4B4:
        return {2, 0, false, false};
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A8L8:
        return {2, 0, false, true};
    case D3DFMT_V8U8:
        return {2, 0, false, false};
    case D3DFMT_A8:
        return {1, 0, false, true};
    case D3DFMT_L8:
    case D3DFMT_P8:
        return {1, 0, false, false};
    case D3DFMT_Q8W8V8U8:
    case D3DFMT_X8L8V8U8:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
        return {4, 0, false, true};
    case D3DFMT_DXT1:
        return {0, 8, false, true};
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        return {0, 16, false, true};
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
        return {2, 0, true, false};
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D32:
    case D3DFMT_D24X4S4:
    case D3DFMT_D15S1:
        return {4, 0, true, false};
    default:
        logf("unknown D3DFORMAT %u treated as 32-bit", static_cast<uint32_t>(format));
        return {4, 0, false, true};
    }
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

void convert_to_rgba8(D3DFORMAT format, const uint8_t *src, uint32_t src_pitch, uint32_t width, uint32_t height,
                      uint8_t *dst)
{
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t *s = src + static_cast<size_t>(y) * src_pitch;
        uint8_t *d = dst + static_cast<size_t>(y) * width * 4;
        switch (static_cast<uint32_t>(format))
        {
        case D3DFMT_A8R8G8B8:
            for (uint32_t x = 0; x < width; ++x, s += 4, d += 4)
                d[0] = s[2], d[1] = s[1], d[2] = s[0], d[3] = s[3];
            break;
        case D3DFMT_X8R8G8B8:
            for (uint32_t x = 0; x < width; ++x, s += 4, d += 4)
                d[0] = s[2], d[1] = s[1], d[2] = s[0], d[3] = 255;
            break;
        case D3DFMT_A8B8G8R8:
        case D3DFMT_Q8W8V8U8:
            memcpy(d, s, static_cast<size_t>(width) * 4);
            break;
        case D3DFMT_X8B8G8R8:
        case D3DFMT_X8L8V8U8:
            for (uint32_t x = 0; x < width; ++x, s += 4, d += 4)
                d[0] = s[0], d[1] = s[1], d[2] = s[2], d[3] = 255;
            break;
        case D3DFMT_R8G8B8:
            for (uint32_t x = 0; x < width; ++x, s += 3, d += 4)
                d[0] = s[2], d[1] = s[1], d[2] = s[0], d[3] = 255;
            break;
        case D3DFMT_R5G6B5:
            for (uint32_t x = 0; x < width; ++x, s += 2, d += 4)
            {
                const uint32_t v = s[0] | (s[1] << 8);
                const uint32_t r = v >> 11, g = (v >> 5) & 63, b = v & 31;
                d[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
                d[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
                d[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
                d[3] = 255;
            }
            break;
        case D3DFMT_A1R5G5B5:
        case D3DFMT_X1R5G5B5:
            for (uint32_t x = 0; x < width; ++x, s += 2, d += 4)
            {
                const uint32_t v = s[0] | (s[1] << 8);
                const uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
                d[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
                d[1] = static_cast<uint8_t>((g << 3) | (g >> 2));
                d[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
                d[3] = (format == D3DFMT_X1R5G5B5 || (v & 0x8000)) ? 255 : 0;
            }
            break;
        case D3DFMT_A4R4G4B4:
        case D3DFMT_X4R4G4B4:
            for (uint32_t x = 0; x < width; ++x, s += 2, d += 4)
            {
                const uint32_t v = s[0] | (s[1] << 8);
                d[0] = static_cast<uint8_t>(((v >> 8) & 15) * 17);
                d[1] = static_cast<uint8_t>(((v >> 4) & 15) * 17);
                d[2] = static_cast<uint8_t>((v & 15) * 17);
                d[3] = format == D3DFMT_X4R4G4B4 ? 255 : static_cast<uint8_t>((v >> 12) * 17);
            }
            break;
        case D3DFMT_A8:
            for (uint32_t x = 0; x < width; ++x, ++s, d += 4)
                d[0] = d[1] = d[2] = 0, d[3] = s[0];
            break;
        case D3DFMT_L8:
        case D3DFMT_P8:
            for (uint32_t x = 0; x < width; ++x, ++s, d += 4)
                d[0] = d[1] = d[2] = s[0], d[3] = 255;
            break;
        case D3DFMT_A8L8:
            for (uint32_t x = 0; x < width; ++x, s += 2, d += 4)
                d[0] = d[1] = d[2] = s[0], d[3] = s[1];
            break;
        case D3DFMT_V8U8:
            for (uint32_t x = 0; x < width; ++x, s += 2, d += 4)
                d[0] = s[0], d[1] = s[1], d[2] = 0, d[3] = 255;
            break;
        default:
            memset(d, 255, static_cast<size_t>(width) * 4);
            break;
        }
    }
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
    if (bgfx::isValid(handle) && Context::instance().initialised())
    {
        if (device)
            device->purge_framebuffers(handle);
        bgfx::destroy(handle);
    }
}

uint8_t *TexStore::lock(uint32_t face, uint32_t level, uint32_t *pitch, uint32_t *slice_pitch)
{
    if (face >= faces() || level >= levels)
        return nullptr;
    Sub &s = sub(face, level);
    const uint32_t w = level_width(level), h = level_height(level);
    const uint32_t slice = level_size(format, w, h);
    const size_t total = static_cast<size_t>(slice) * level_depth(level);
    if (s.data.size() != total)
        s.data.assign(total, 0);
    ++s.lock_count;
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
    sub(face, level).dirty = true;
    any_dirty = true;
}

void TexStore::create_gpu()
{
    const bgfx::Caps *caps = Context::instance().caps();
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
    else
    {
        gpu_format = bgfx::TextureFormat::RGBA8;
        if (render_target)
            flags |= BGFX_TEXTURE_RT;
    }
    const bool mips = levels > 1;
    (void)caps;
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
}

void TexStore::sync()
{
    if (system_memory)
        return;
    if (!bgfx::isValid(handle))
    {
        create_gpu();
        if (!bgfx::isValid(handle))
            return;
        // A mipped texture whose lower levels were never written would sample undefined memory.
        if (!render_target && !depth_stencil)
            for (uint32_t face = 0; face < faces(); ++face)
                for (uint32_t level = 0; level < levels; ++level)
                    if (sub(face, level).data.empty())
                    {
                        lock(face, level, nullptr, nullptr);
                        sub(face, level).dirty = true;
                        any_dirty = true;
                    }
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
            const bgfx::Memory *memory;
            if (gpu_compressed)
            {
                memory = bgfx::copy(s.data.data(), static_cast<uint32_t>(s.data.size()));
            }
            else
            {
                memory = bgfx::alloc(w * h * d * 4);
                const uint32_t pitch = level_pitch(format, w);
                for (uint32_t z = 0; z < d; ++z)
                    convert_to_rgba8(format, s.data.data() + static_cast<size_t>(z) * pitch * h, pitch, w, h,
                                     memory->data + static_cast<size_t>(z) * w * h * 4);
            }
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

TexStore *store_of(IDirect3DBaseTexture9 *texture)
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
    desc->Width = store.level_width(level);
    desc->Height = store.level_height(level);
}

HRESULT lock_rect(TexStore &store, uint32_t face, uint32_t level, D3DLOCKED_RECT *locked, const RECT *rect)
{
    if (!locked)
        return D3DERR_INVALIDCALL;
    uint32_t pitch = 0;
    uint8_t *data = store.lock(face, level, &pitch, nullptr);
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
HRESULT Surface::LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD)
{
    return lock_rect(*store, face, level, locked, rect);
}
HRESULT Surface::UnlockRect()
{
    store->unlock(face, level);
    return D3D_OK;
}
HRESULT Surface::GetDevice(IDirect3DDevice9 **device)
{
    *device = store->device;
    store->device->AddRef();
    return D3D_OK;
}

HRESULT Texture2D::GetLevelDesc(UINT level, D3DSURFACE_DESC *desc)
{
    if (level >= store->levels)
        return D3DERR_INVALIDCALL;
    fill_desc(*store, level, desc);
    return D3D_OK;
}
HRESULT Texture2D::GetSurfaceLevel(UINT level, IDirect3DSurface9 **surface)
{
    if (level >= store->levels)
        return D3DERR_INVALIDCALL;
    *surface = new Surface(store, 0, level);
    return D3D_OK;
}
HRESULT Texture2D::LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD)
{
    return lock_rect(*store, 0, level, locked, rect);
}
HRESULT Texture2D::UnlockRect(UINT level)
{
    store->unlock(0, level);
    return D3D_OK;
}
HRESULT Texture2D::GetDevice(IDirect3DDevice9 **device)
{
    *device = store->device;
    store->device->AddRef();
    return D3D_OK;
}

HRESULT CubeTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC *desc)
{
    if (level >= store->levels)
        return D3DERR_INVALIDCALL;
    fill_desc(*store, level, desc);
    return D3D_OK;
}
HRESULT CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 **surface)
{
    if (level >= store->levels || static_cast<uint32_t>(face) >= 6)
        return D3DERR_INVALIDCALL;
    *surface = new Surface(store, static_cast<uint32_t>(face), level);
    return D3D_OK;
}
HRESULT CubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD)
{
    return lock_rect(*store, static_cast<uint32_t>(face), level, locked, rect);
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
HRESULT VolumeTexture::LockBox(UINT level, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD)
{
    uint32_t pitch = 0, slice = 0;
    uint8_t *data = store->lock(0, level, &pitch, &slice);
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
}

VertexBuffer::~VertexBuffer()
{
    if (bgfx::isValid(handle) && Context::instance().initialised())
        bgfx::destroy(handle);
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

HRESULT VertexDeclaration::GetDeclaration(D3DVERTEXELEMENT9 *elements_out, UINT *count)
{
    if (count)
        *count = static_cast<UINT>(elements.size()) + 1;
    if (elements_out)
    {
        std::copy(elements.begin(), elements.end(), elements_out);
        elements_out[elements.size()] = D3DDECL_END();
    }
    return D3D_OK;
}

namespace
{
template <class T> HRESULT get_function(const std::vector<DWORD> &bytecode, void *data, UINT *size)
{
    const UINT bytes = static_cast<UINT>(bytecode.size() * sizeof(DWORD));
    if (!data)
    {
        *size = bytes;
        return D3D_OK;
    }
    memcpy(data, bytecode.data(), std::min(bytes, *size));
    return D3D_OK;
}
} // namespace
HRESULT VertexShader::GetFunction(void *data, UINT *size)
{
    return get_function<VertexShader>(bytecode, data, size);
}
HRESULT PixelShader::GetFunction(void *data, UINT *size)
{
    return get_function<PixelShader>(bytecode, data, size);
}

} // namespace storm::shim
