#pragma once

#include "shim.hpp"
#include "storm/render/frame_owner.hpp"

#include <map>

struct SDL_Window;

namespace storm::shim
{

// Texture units: 0..4 2D stage N, 5..7 cube of stage 0..2, 8..10 volume of stage 0..2.
inline constexpr uint32_t kSamplerUnits = 13;

// Owns bgfx::init/shutdown, the surface and the frame-owner contract.
class Context final : public render::FrameOwner
{
  public:
    static Context &instance();

    bool init(SDL_Window *window, uint32_t width, uint32_t height, bool vsync);
    void shutdown();
    bool ready() const { return initialised_ && surface_.valid; }
    bool initialised() const { return initialised_; }
    void resize(uint32_t width, uint32_t height);
    void set_density(float density);
    // Surface lifecycle (Android): pause stops presenting, resume re-attaches the native window.
    void surface_lost();
    void surface_restored();

    void begin_frame_if_needed();
    // Composites `game_color` to the surface, runs client UI hooks and presents.
    void present(bgfx::TextureHandle game_color, uint32_t game_width, uint32_t game_height);
    // Executes queued commands without presenting (synchronous read-backs).
    void flush();

    uint64_t frame_index() const { return frame_.frame_index; }
    const bgfx::Caps *caps() const { return caps_; }
    bool flip_y() const { return flip_y_; }
    bgfx::ProgramHandle blit_program() const { return blit_program_; }
    bgfx::UniformHandle sampler(uint32_t unit) const { return samplers_[unit]; }
    void draw_fullscreen(uint16_t view, bgfx::TextureHandle texture, uint32_t sampler_flags, uint64_t state,
                         float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);

    // FrameOwner
    uint32_t ApiVersion() const override { return render::kFrameOwnerApiVersion; }
    void AddClient(render::FrameClient *client, int order) override;
    void RemoveClient(render::FrameClient *client) override;
    render::SurfaceMetrics Surface() const override { return surface_; }
    bool DeviceReady() const override { return ready(); }
    render::TextureId CreateTexture(const render::TextureDesc &desc, std::span<const std::byte> pixels) override;
    bool UpdateTexture(render::TextureId id, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                       std::span<const std::byte> pixels) override;
    void DestroyTexture(render::TextureId id) override;
    uint16_t BgfxTexture(render::TextureId id) const override;
    bool IsFormatSupported(render::PixelFormat format) const override;
    render::GamePicture AcquireGamePicture(std::string_view path) override;
    render::GamePicture QueryGamePicture(std::string_view path) override;
    void ReleaseGamePicture(std::string_view path) override;
    void SetFrameRateLimit(uint32_t fps) override { frame_limit_ = fps; }
    void set_picture_provider(render::GamePictureProvider *provider);

  private:
    struct Client
    {
        render::FrameClient *client;
        int order;
    };
    struct Picture
    {
        render::GamePicture picture;
        uint32_t refs = 0;
        uint32_t token = 0;
    };
    void create_resources();
    void pace();

    SDL_Window *window_ = nullptr;
    bool initialised_ = false;
    bool frame_open_ = false;
    bool flip_y_ = false;
    bool vsync_ = true;
    const bgfx::Caps *caps_ = nullptr;
    render::SurfaceMetrics surface_;
    render::FrameContext frame_;
    std::vector<Client> clients_;
    bgfx::ProgramHandle blit_program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle samplers_[kSamplerUnits] = {};
    bgfx::VertexLayout blit_layout_;
    std::unordered_map<std::string, Picture> pictures_;
    render::GamePictureProvider *picture_provider_ = nullptr;
    uint32_t frame_limit_ = 0;
    double last_present_ = 0.0;
};

struct Device final : Direct3DDevice9Stub
{
    Device(SDL_Window *window, const D3DPRESENT_PARAMETERS &params);
    ~Device() override;

    // ---- IDirect3DDevice9 ----
    HRESULT TestCooperativeLevel() override { return D3D_OK; }
    UINT GetAvailableTextureMem() override { return 1024u * 1024u * 1024u; }
    HRESULT GetDeviceCaps(D3DCAPS9 *caps) override;
    HRESULT GetDirect3D(IDirect3D9 **d3d) override;
    HRESULT Reset(D3DPRESENT_PARAMETERS *params) override;
    HRESULT Present(const RECT *, const RECT *, HWND, const RGNDATA *) override;
    HRESULT GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **surface) override;
    HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                          IDirect3DTexture9 **texture, HANDLE *) override;
    HRESULT CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
                                D3DPOOL pool, IDirect3DVolumeTexture9 **texture, HANDLE *) override;
    HRESULT CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                              IDirect3DCubeTexture9 **texture, HANDLE *) override;
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **buffer,
                               HANDLE *) override;
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 **buffer,
                              HANDLE *) override;
    HRESULT CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                               IDirect3DSurface9 **surface, HANDLE *) override;
    HRESULT CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                      IDirect3DSurface9 **surface, HANDLE *) override;
    HRESULT CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                        IDirect3DSurface9 **surface, HANDLE *) override;
    HRESULT UpdateSurface(IDirect3DSurface9 *src, const RECT *src_rect, IDirect3DSurface9 *dst,
                          const POINT *dst_point) override;
    HRESULT GetRenderTargetData(IDirect3DSurface9 *target, IDirect3DSurface9 *dest) override;
    HRESULT StretchRect(IDirect3DSurface9 *src, const RECT *src_rect, IDirect3DSurface9 *dst, const RECT *dst_rect,
                        D3DTEXTUREFILTERTYPE filter) override;
    HRESULT SetRenderTarget(DWORD index, IDirect3DSurface9 *surface) override;
    HRESULT GetRenderTarget(DWORD index, IDirect3DSurface9 **surface) override;
    HRESULT SetDepthStencilSurface(IDirect3DSurface9 *surface) override;
    HRESULT GetDepthStencilSurface(IDirect3DSurface9 **surface) override;
    HRESULT BeginScene() override;
    HRESULT EndScene() override { return D3D_OK; }
    HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override;
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix) override;
    HRESULT SetViewport(const D3DVIEWPORT9 *viewport) override;
    HRESULT GetViewport(D3DVIEWPORT9 *viewport) override;
    HRESULT SetMaterial(const D3DMATERIAL9 *material) override;
    HRESULT GetMaterial(D3DMATERIAL9 *material) override;
    HRESULT SetLight(DWORD index, const D3DLIGHT9 *light) override;
    HRESULT GetLight(DWORD index, D3DLIGHT9 *light) override;
    HRESULT LightEnable(DWORD index, BOOL enable) override;
    HRESULT GetLightEnable(DWORD index, BOOL *enable) override;
    HRESULT SetClipPlane(DWORD index, const float *plane) override;
    HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override;
    HRESULT GetRenderState(D3DRENDERSTATETYPE state, DWORD *value) override;
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **block) override;
    HRESULT GetTexture(DWORD stage, IDirect3DBaseTexture9 **texture) override;
    HRESULT SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture) override;
    HRESULT GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD *value) override;
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override;
    HRESULT GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD *value) override;
    HRESULT SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) override;
    HRESULT SetScissorRect(const RECT *rect) override;
    HRESULT GetScissorRect(RECT *rect) override;
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, UINT start_vertex, UINT primitive_count) override;
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count,
                                 UINT start_index, UINT primitive_count) override;
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitive_count, const void *data, UINT stride) override;
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min_index, UINT vertex_count, UINT primitive_count,
                                   const void *index_data, D3DFORMAT index_format, const void *data,
                                   UINT stride) override;
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **decl) override;
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9 *decl) override;
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9 **decl) override;
    HRESULT SetFVF(DWORD fvf) override;
    HRESULT GetFVF(DWORD *fvf) override;
    HRESULT CreateVertexShader(const DWORD *function, IDirect3DVertexShader9 **shader) override;
    HRESULT SetVertexShader(IDirect3DVertexShader9 *shader) override;
    HRESULT GetVertexShader(IDirect3DVertexShader9 **shader) override;
    HRESULT SetVertexShaderConstantF(UINT start, const float *data, UINT count) override;
    HRESULT GetVertexShaderConstantF(UINT start, float *data, UINT count) override;
    HRESULT SetStreamSource(UINT stream, IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride) override;
    HRESULT GetStreamSource(UINT stream, IDirect3DVertexBuffer9 **buffer, UINT *offset, UINT *stride) override;
    HRESULT SetIndices(IDirect3DIndexBuffer9 *buffer) override;
    HRESULT GetIndices(IDirect3DIndexBuffer9 **buffer) override;
    HRESULT CreatePixelShader(const DWORD *function, IDirect3DPixelShader9 **shader) override;
    HRESULT SetPixelShader(IDirect3DPixelShader9 *shader) override;
    HRESULT GetPixelShader(IDirect3DPixelShader9 **shader) override;
    HRESULT SetPixelShaderConstantF(UINT start, const float *data, UINT count) override;
    HRESULT GetPixelShaderConstantF(UINT start, float *data, UINT count) override;
    HRESULT SetNPatchMode(float) override { return D3D_OK; }
    HRESULT SetSoftwareVertexProcessing(BOOL) override { return D3D_OK; }
    HRESULT ShowCursor(BOOL) override { return D3D_OK; }
    HRESULT EvictManagedResources() override { return D3D_OK; }
    void SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP *) override {}

    // ---- internals ----
    struct DrawCall
    {
        D3DPRIMITIVETYPE type{};
        uint32_t primitive_count = 0;
        bool indexed = false;
        int32_t base_vertex = 0;
        uint32_t start_vertex = 0; // non-indexed
        uint32_t start_index = 0;
        const uint8_t *up_vertices = nullptr;
        uint32_t up_stride = 0;
        const void *up_indices = nullptr;
        bool up_index32 = false;
        uint32_t up_vertex_count = 0;
    };
    HRESULT draw(const DrawCall &call);
    const LayoutInfo *current_layout();
    const LayoutInfo *fvf_layout(DWORD fvf, uint32_t stride);
    bool build_decl_layout(VertexDeclaration &decl);
    uint16_t ensure_view();
    uint16_t new_view();
    bgfx::FrameBufferHandle framebuffer(TexStore *color, uint32_t face, uint32_t level, TexStore *depth);
    void purge_framebuffers(bgfx::TextureHandle texture);
    void apply_ff_uniforms(const LayoutInfo &layout, bool vs_port, bool ps_port);
    uint64_t render_state_bits(D3DPRIMITIVETYPE type, bool *stencil, uint32_t *fstencil);
    uint32_t sampler_flags(uint32_t stage, const TexStore &store) const;
    void set_default_state();
    void bind_state_refs(const DeviceState &next);
    void create_backbuffer(uint32_t width, uint32_t height);
    bgfx::ProgramHandle program(uint32_t vs_id, uint32_t fs_id);
    void end_frame_stats();

    SDL_Window *window = nullptr;
    D3DPRESENT_PARAMETERS params{};
    DeviceState s;
    std::vector<Light> lights;
    Surface *back_color = nullptr, *back_depth = nullptr; // owned references
    Surface *rt = nullptr, *ds = nullptr;                  // owned references
    uint16_t view = 0;
    uint16_t views_used = 0;
    bool view_dirty = true;
    bool view_has_content = false;
    bool view_overflow_logged = false;

    struct FbKey
    {
        uint16_t color, depth;
        uint8_t face, level;
        bool operator<(const FbKey &o) const
        {
            return std::tie(color, depth, face, level) < std::tie(o.color, o.depth, o.face, o.level);
        }
    };
    std::map<FbKey, bgfx::FrameBufferHandle> framebuffers;
    std::unordered_map<uint64_t, std::unique_ptr<LayoutInfo>> fvf_layouts;
    std::map<std::pair<uint32_t, uint32_t>, bgfx::ProgramHandle> programs;
    std::vector<bgfx::ShaderHandle> shaders; // index = shader id
    bgfx::VertexLayout byte_layout;

    // uniform handles + last value sent (uniforms are sticky in submission order)
    struct Uniform
    {
        bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
        std::vector<float> last;
        uint16_t count = 1;
        bool sent = false;
    };
    enum UniformId
    {
        U_WORLDVIEW,
        U_PROJ,
        U_VS,
        U_CLIP,
        U_MATERIAL,
        U_AMBIENT,
        U_LIGHTPOS,
        U_LIGHTDIR,
        U_LIGHTDIFFUSE,
        U_LIGHTAMBIENT,
        U_LIGHTSPECULAR,
        U_LIGHTATT,
        U_TEXCFG,
        U_TEXMTX,
        U_STAGE,
        U_FS,
        U_VSC,
        U_PSC,
        U_BEM,
        U_COUNT
    };
    Uniform uniforms[U_COUNT];
    void set_uniform(UniformId id, const float *data, uint16_t count);
    void invalidate_uniforms();

    // per-frame statistics (S3)
    struct Stats
    {
        uint32_t draws = 0, up_bytes = 0, transient_vertex_bytes = 0, views = 0, clears = 0, skipped = 0;
    } stats, stats_max;
    uint64_t stats_frames = 0;
};

uint32_t shader_id_by_name(const char *name);
bgfx::ShaderHandle create_embedded_shader(uint32_t id);
uint32_t embedded_shader_count();

} // namespace storm::shim
