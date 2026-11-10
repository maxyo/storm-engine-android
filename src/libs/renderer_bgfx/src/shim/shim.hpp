// D3D9-shaped shim over bgfx: internal declarations.
//
// The engine keeps talking to IDirect3DDevice9 & co. (third_party/d3d9-headers);
// the objects behind those interfaces are implemented here on bgfx handles.
// All D3D conventions (row vectors, 0..1 depth, top-left render targets,
// integer pixel centres, BGRA colours, triangle fans) are translated in this
// directory only.
#pragma once

#include "d3d9_stubs.gen.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::texture
{
struct Job;
}

namespace storm::shim
{

void logf(const char *format, ...);
// Writes the per-method trap counters collected so far to the log.
void dump_traps();

struct Device;
class Context;

// ---------------------------------------------------------------------------
// formats
struct FormatInfo
{
    uint8_t bytes_per_pixel = 0; // 0 for block formats
    uint8_t block_bytes = 0;     // 8 or 16 for DXTn
    bool depth = false;
    bool has_alpha = false;
};
FormatInfo format_info(D3DFORMAT format);
uint32_t level_pitch(D3DFORMAT format, uint32_t width);
uint32_t level_size(D3DFORMAT format, uint32_t width, uint32_t height);
// Converts one level of an uncompressed D3D format to RGBA8 (R first).
void convert_to_rgba8(D3DFORMAT format, const uint8_t *src, uint32_t src_pitch, uint32_t width, uint32_t height,
                      uint8_t *dst);

// ---------------------------------------------------------------------------
// texture storage shared by textures and surfaces
enum class TexKind : uint8_t
{
    Tex2D,
    Cube,
    Volume,
};

struct TexStore
{
    Device *device = nullptr;
    TexKind kind = TexKind::Tex2D;
    uint32_t width = 0, height = 0, depth = 1, levels = 1;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    bool render_target = false;
    bool needs_clear = false; // a render target whose GPU memory was never written (see Device::clear_if_new)
    bool depth_stencil = false;
    bool system_memory = false; // never uploaded (offscreen plain surfaces)
    bool backbuffer = false;    // the game's offscreen backbuffer colour/depth
    uint64_t target_flags = 0;  // extra bgfx texture flags of a render target (MSAA of the scene target)

    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    bgfx::TextureFormat::Enum gpu_format = bgfx::TextureFormat::Unknown;
    bool gpu_compressed = false;

    struct Sub
    {
        std::vector<uint8_t> data; // D3D-format shadow copy; may be released after upload
        bool dirty = false;
        bool written = false; // locked for writing since the last unlock (D3DLOCK_READONLY locks are not)
        uint32_t lock_count = 0;
    };
    std::vector<Sub> subs; // face * levels + level
    bool any_dirty = false;
    uint32_t bound_frame = 0;

    // DXT -> ETC2 transcode (tier 2): `handle` is either the ETC2 texture or a
    // temporary uncompressed one while `transcode_job` runs.
    bool transcoded = false;          // the handle is not in `format`; re-locking recreates it
    uint64_t transcode_key = 0;
    int64_t fallback_bytes = 0;       // size of the temporary texture, 0 once ETC2 is in place
    std::shared_ptr<texture::Job> transcode_job;

    ~TexStore();
    uint32_t faces() const { return kind == TexKind::Cube ? 6 : 1; }
    uint32_t level_width(uint32_t level) const { return std::max(1u, width >> level); }
    uint32_t level_height(uint32_t level) const { return std::max(1u, height >> level); }
    uint32_t level_depth(uint32_t level) const { return kind == TexKind::Volume ? std::max(1u, depth >> level) : 1; }
    Sub &sub(uint32_t face, uint32_t level) { return subs[face * levels + level]; }
    // A read-only lock (D3DLOCK_READONLY) does not make the level dirty: its unlock uploads nothing.
    uint8_t *lock(uint32_t face, uint32_t level, uint32_t *pitch, uint32_t *slice_pitch, bool read_only);
    void unlock(uint32_t face, uint32_t level);
    // Creates the bgfx texture if needed and uploads dirty levels.
    void sync();
    void create_gpu();
    void release_shadow();
    void settle();
    bool shadow_released = false;
    void create_sampled();
    void destroy_sampled();
    bool create_transcoded();
    void finish_transcode();
    void keep_interim(const char *reason);
};
using TexStorePtr = std::shared_ptr<TexStore>;
TexStorePtr make_store(Device *device, TexKind kind, uint32_t width, uint32_t height, uint32_t depth, uint32_t levels,
                       DWORD usage, D3DFORMAT format, D3DPOOL pool);

struct Surface final : Direct3DSurface9Stub
{
    TexStorePtr store;
    uint32_t face = 0, level = 0;
    Surface(TexStorePtr s, uint32_t f, uint32_t l) : store(std::move(s)), face(f), level(l) {}
    HRESULT GetDesc(D3DSURFACE_DESC *desc) override;
    HRESULT LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override;
    HRESULT UnlockRect() override;
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_SURFACE; }
    HRESULT GetDevice(IDirect3DDevice9 **device) override;
};

struct Texture2D final : Direct3DTexture9Stub
{
    TexStorePtr store;
    explicit Texture2D(TexStorePtr s) : store(std::move(s)) {}
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD GetLevelCount() override { return store->levels; }
    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override;
    HRESULT GetSurfaceLevel(UINT level, IDirect3DSurface9 **surface) override;
    HRESULT LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override;
    HRESULT UnlockRect(UINT level) override;
    HRESULT AddDirtyRect(const RECT *) override { return D3D_OK; }
    void GenerateMipSubLevels() override {}
    void PreLoad() override {}
    DWORD SetLOD(DWORD) override { return 0; }
    DWORD GetLOD() override { return 0; }
    HRESULT GetDevice(IDirect3DDevice9 **device) override;
};

struct CubeTexture final : Direct3DCubeTexture9Stub
{
    TexStorePtr store;
    explicit CubeTexture(TexStorePtr s) : store(std::move(s)) {}
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_CUBETEXTURE; }
    DWORD GetLevelCount() override { return store->levels; }
    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override;
    HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 **surface) override;
    HRESULT LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override;
    HRESULT UnlockRect(D3DCUBEMAP_FACES face, UINT level) override;
    HRESULT AddDirtyRect(D3DCUBEMAP_FACES, const RECT *) override { return D3D_OK; }
    void GenerateMipSubLevels() override {}
    void PreLoad() override {}
};

struct VolumeTexture final : Direct3DVolumeTexture9Stub
{
    TexStorePtr store;
    explicit VolumeTexture(TexStorePtr s) : store(std::move(s)) {}
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_VOLUMETEXTURE; }
    DWORD GetLevelCount() override { return store->levels; }
    HRESULT GetLevelDesc(UINT level, D3DVOLUME_DESC *desc) override;
    HRESULT LockBox(UINT level, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags) override;
    HRESULT UnlockBox(UINT level) override;
    HRESULT AddDirtyBox(const D3DBOX *) override { return D3D_OK; }
    void GenerateMipSubLevels() override {}
    void PreLoad() override {}
};

// Returns the storage behind any of the three texture classes (or nullptr).
TexStore *store_of(IDirect3DBaseTexture9 *texture);

// ---------------------------------------------------------------------------
// buffers
struct BufferBase
{
    Device *device = nullptr;
    std::vector<uint8_t> shadow;
    uint32_t dirty_begin = UINT32_MAX, dirty_end = 0; // bytes not yet sent to the GPU copy
    uint64_t write_frame = UINT64_MAX;                // frame of the last Unlock
    uint32_t write_serial = 0;                        // incremented by every writing Lock
    uint32_t lock_count = 0;
    DWORD usage = 0;
    void mark(uint32_t offset, uint32_t size);
};

struct VertexBuffer final : Direct3DVertexBuffer9Stub, BufferBase
{
    DWORD fvf = 0;
    bgfx::VertexBufferHandle handle = BGFX_INVALID_HANDLE; // immutable snapshot, re-created when stale
    ~VertexBuffer() override;
    HRESULT Lock(UINT offset, UINT size, void **data, DWORD flags) override;
    HRESULT Unlock() override;
    HRESULT GetDesc(D3DVERTEXBUFFER_DESC *desc) override;
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
};

struct IndexBuffer final : Direct3DIndexBuffer9Stub, BufferBase
{
    bool index32 = false;
    bgfx::IndexBufferHandle handle = BGFX_INVALID_HANDLE;
    ~IndexBuffer() override;
    HRESULT Lock(UINT offset, UINT size, void **data, DWORD flags) override;
    HRESULT Unlock() override;
    HRESULT GetDesc(D3DINDEXBUFFER_DESC *desc) override;
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
};

// ---------------------------------------------------------------------------
// vertex layouts
inline constexpr uint32_t kMaxStreams = 4;
struct LayoutInfo
{
    bgfx::VertexLayout layout[kMaxStreams];
    bgfx::VertexLayoutHandle handle[kMaxStreams] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                                    BGFX_INVALID_HANDLE};
    uint16_t stride[kMaxStreams] = {};
    uint8_t stream_count = 0;
    bool rhw = false, has_normal = false, has_color0 = false, has_color1 = false;
    uint8_t texcoord_size[8] = {};
    uint8_t texcoord_count = 0;
};

struct VertexDeclaration final : Direct3DVertexDeclaration9Stub
{
    std::vector<D3DVERTEXELEMENT9> elements; // without the D3DDECL_END terminator
    LayoutInfo info;
    HRESULT GetDeclaration(D3DVERTEXELEMENT9 *elements_out, UINT *count) override;
};

// ---------------------------------------------------------------------------
// programmable shaders: technique shader keys mapped to hand-ported bgfx shaders (ports.hpp)
struct ShaderPort;
struct VertexShader final : Direct3DVertexShader9Stub
{
    std::vector<DWORD> bytecode;
    const ShaderPort *port = nullptr;
    HRESULT GetFunction(void *data, UINT *size) override;
};
struct PixelShader final : Direct3DPixelShader9Stub
{
    std::vector<DWORD> bytecode;
    const ShaderPort *port = nullptr;
    HRESULT GetFunction(void *data, UINT *size) override;
};

// ---------------------------------------------------------------------------
// device state
inline constexpr uint32_t kMaxStages = 8;
inline constexpr uint32_t kFfStages = 5;
inline constexpr uint32_t kMaxLights = 8;
inline constexpr uint32_t kRenderStateCount = 210;
inline constexpr uint32_t kVsConstants = 96;
inline constexpr uint32_t kPsConstants = 8;

struct DeviceState
{
    DWORD rs[kRenderStateCount] = {};
    DWORD tss[kMaxStages][33] = {};
    DWORD sampler[kMaxStages][14] = {};
    D3DMATRIX world{}, view{}, proj{};
    D3DMATRIX texture[kMaxStages] = {};
    D3DMATERIAL9 material{};
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    float clip_plane[6][4] = {};
    float vs_const[kVsConstants][4] = {};
    float ps_const[kPsConstants][4] = {};
    DWORD fvf = 0;
    // non-owning in the struct; the Device keeps the matching references
    IDirect3DBaseTexture9 *textures[kMaxStages] = {};
    VertexBuffer *stream[kMaxStreams] = {};
    UINT stream_offset[kMaxStreams] = {}, stream_stride[kMaxStreams] = {};
    IndexBuffer *indices = nullptr;
    VertexDeclaration *decl = nullptr;
    VertexShader *vs = nullptr;
    PixelShader *ps = nullptr;
};

struct Light
{
    D3DLIGHT9 light{};
    bool enabled = false;
};

struct StateBlock final : Direct3DStateBlock9Stub
{
    Device *device;
    DeviceState state;
    std::vector<Light> lights;
    explicit StateBlock(Device *d);
    ~StateBlock() override;
    HRESULT Capture() override;
    HRESULT Apply() override;
};

struct Direct3D final : Direct3D9Stub
{
    UINT GetAdapterCount() override { return 1; }
    HRESULT GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *identifier) override;
    HRESULT GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode) override;
    HRESULT GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 *caps) override;
    HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE, DWORD *) override
    {
        return D3DERR_NOTAVAILABLE;
    }
    HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) override { return D3D_OK; }
    HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }
    HRESULT CreateDevice(UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *params,
                         IDirect3DDevice9 **device) override;
};

void fill_caps(D3DCAPS9 *caps);

} // namespace storm::shim
