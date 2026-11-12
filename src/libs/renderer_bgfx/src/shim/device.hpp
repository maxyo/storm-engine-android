#pragma once

#include "shim.hpp"
#include "storm/render/frame_owner.hpp"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>

struct SDL_Window;

namespace storm::effects
{
struct FfDraw;
}

namespace storm::shim
{

// Texture units: 0..4 2D stage N, 5..7 cube of stage 0..2, 8..10 volume of stage 0..2,
// 11 cube of stage 3, 12 volume of stage 4 (pixel ports), 13 the bone palette (vertex shader).
inline constexpr uint32_t kSamplerUnits = 14;
inline constexpr uint32_t kBonePaletteUnit = 13;

// Owns bgfx::init/shutdown, the surface and the frame-owner contract.
class Context final : public render::FrameOwner
{
  public:
    static Context &instance();

    bool init(SDL_Window *window, uint32_t width, uint32_t height, bool vsync);
    // Host checks without a display (tests/renderer_bgfx): bgfx's Noop renderer, no window, no surface events.
    bool init_headless(uint32_t width, uint32_t height);
    void shutdown();
    bool ready() const { return initialised_ && surface_.valid; }
    bool initialised() const { return initialised_; }
    void resize(uint32_t width, uint32_t height);
    // Re-applies the reset flags (graphics effects changed the anisotropy) without a size change.
    void refresh_reset_flags();
    // Any thread: the display density changed; the game thread re-reads it at its next safe point.
    void request_density_refresh();
    // Any thread: Android handed over a surface; restore once SDL holds its native window.
    void request_surface_restore();
    // The game picture on the surface as of the last composite (game thread).
    render::PictureRect picture_rect() const { return picture_rect_; }
    // Surface lifecycle (Android): pause stops presenting, resume re-attaches the native window.
    void surface_lost();
    // Marks the surface for re-attachment; the actual bgfx reset happens once SDL holds a native window.
    void surface_restored();
    // Any thread (Android UI thread, before it destroys the surface): stops further bgfx frames and
    // blocks while one is executing (bgfx renders and swaps inside bgfx::frame()), or the timeout expires.
    void request_surface_release();
    // Game thread, at safe points: honours a pending release request and retries a pending restore.
    void poll_surface();

    void begin_frame_if_needed();
    // Composites `game_color` to the surface, runs client UI hooks and presents.
    void present(bgfx::TextureHandle game_color, uint32_t game_width, uint32_t game_height);
    // The only place the game thread calls bgfx::frame(). Returns false without calling it when the
    // surface is about to be destroyed; presentation is then suspended.
    bool run_frame(uint32_t flags, uint32_t *frame_number = nullptr);

    uint64_t frame_index() const { return frame_.frame_index; }
    uint32_t last_frame() const { return last_frame_; } // bgfx::frame() number of the last presented frame
    // Counts bgfx::frame() calls (presents and read-backs): transient buffers live until the next one.
    uint32_t submitted_frames() const { return submitted_frames_; }
    const bgfx::Caps *caps() const { return caps_; }
    bool flip_y() const { return flip_y_; }
    bgfx::UniformHandle sampler(uint32_t unit) const { return samplers_[unit]; }
    void draw_fullscreen(uint16_t view, bgfx::TextureHandle texture, uint32_t sampler_flags, uint64_t state,
                         float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);

    // FrameOwner
    uint32_t ApiVersion() const override { return render::kFrameOwnerApiVersion; }
    void AddClient(render::FrameClient *client, int order) override;
    void RemoveClient(render::FrameClient *client) override;
    render::SurfaceMetrics Surface() const override { return surface_; }
    GameTarget CurrentGameTarget() const override { return game_target_; }
    void set_game_target(const GameTarget &target) { game_target_ = target; }
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
    bool start(SDL_Window *window, uint32_t width, uint32_t height, bool vsync); // null window: headless
    void create_resources();
    void pace();
    void try_restore();
    bool read_density(); // true when it changed
    uint32_t reset_flags() const;

    SDL_Window *window_ = nullptr;
    bool initialised_ = false;
    bool frame_open_ = false;
    bool restore_pending_ = false;
    render::PictureRect picture_rect_;
    std::atomic<bool> restore_requested_{false};
    std::atomic<bool> density_refresh_requested_{false};
    bool surface_epoch_flag_ = false;
    bool restore_wait_logged_ = false;
    bool frame_aborted_ = false; // draws of an unfinished frame are still queued in bgfx
    std::atomic<bool> release_requested_{false};
    std::atomic<bool> surface_in_use_{false};
    bool in_frame_ = false; // guarded by release_mutex_
    std::mutex release_mutex_;
    std::condition_variable release_done_;
    bool flip_y_ = false;
    bool vsync_ = true;
    const bgfx::Caps *caps_ = nullptr;
    render::SurfaceMetrics surface_;
    render::FrameContext frame_;
    std::vector<Client> clients_;
    bgfx::ProgramHandle blit_program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle samplers_[kSamplerUnits] = {};
    bgfx::VertexLayout blit_layout_;
    bgfx::VertexBufferHandle blit_vertices_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle blit_uv_ = BGFX_INVALID_HANDLE;
    std::unordered_map<std::string, Picture> pictures_;
    render::GamePictureProvider *picture_provider_ = nullptr;
    GameTarget game_target_;
    uint32_t frame_limit_ = 0;
    uint32_t last_frame_ = 0;
    uint32_t submitted_frames_ = 0;
    double last_present_ = 0.0;
};

struct Device final : Object
{
    Device(SDL_Window *window, const D3DPRESENT_PARAMETERS &params);
    ~Device() override;

    // ---- the D3D9-shaped device API Renderer drives ----
    HRESULT TestCooperativeLevel() { return D3D_OK; }
    UINT GetAvailableTextureMem() { return 1024u * 1024u * 1024u; }
    HRESULT GetDeviceCaps(D3DCAPS9 *caps);
    HRESULT GetDirect3D(Direct3D **d3d);
    HRESULT Reset(D3DPRESENT_PARAMETERS *params);
    HRESULT Present();
    HRESULT GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, Surface **surface);
    HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                          Texture2D **texture, HANDLE *);
    HRESULT CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
                                D3DPOOL pool, VolumeTexture **texture, HANDLE *);
    HRESULT CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                              CubeTexture **texture, HANDLE *);
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, VertexBuffer **buffer,
                               HANDLE *);
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IndexBuffer **buffer,
                              HANDLE *);
    HRESULT CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                               Surface **surface, HANDLE *);
    HRESULT CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                      Surface **surface, HANDLE *);
    HRESULT CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                        Surface **surface, HANDLE *);
    HRESULT UpdateSurface(Surface *src, const RECT *src_rect, Surface *dst,
                          const POINT *dst_point);
    HRESULT GetRenderTargetData(Surface *target, Surface *dest);
    HRESULT StretchRect(Surface *src, const RECT *src_rect, Surface *dst, const RECT *dst_rect,
                        D3DTEXTUREFILTERTYPE filter);
    HRESULT SetRenderTarget(DWORD index, Surface *surface);
    HRESULT GetRenderTarget(DWORD index, Surface **surface);
    HRESULT SetDepthStencilSurface(Surface *surface);
    HRESULT GetDepthStencilSurface(Surface **surface);
    HRESULT BeginScene();
    HRESULT EndScene() { return D3D_OK; }
    HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix);
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix);
    HRESULT SetViewport(const D3DVIEWPORT9 *viewport);
    HRESULT GetViewport(D3DVIEWPORT9 *viewport);
    HRESULT SetMaterial(const D3DMATERIAL9 *material);
    HRESULT GetMaterial(D3DMATERIAL9 *material);
    HRESULT SetLight(DWORD index, const D3DLIGHT9 *light);
    HRESULT GetLight(DWORD index, D3DLIGHT9 *light);
    HRESULT LightEnable(DWORD index, BOOL enable);
    HRESULT GetLightEnable(DWORD index, BOOL *enable);
    HRESULT SetClipPlane(DWORD index, const float *plane);
    HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value);
    HRESULT GetRenderState(D3DRENDERSTATETYPE state, DWORD *value);
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE type, StateBlock **block);
    HRESULT GetTexture(DWORD stage, BaseTexture **texture);
    HRESULT SetTexture(DWORD stage, BaseTexture *texture);
    HRESULT GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD *value);
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value);
    HRESULT GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD *value);
    HRESULT SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value);
    HRESULT SetScissorRect(const RECT *rect);
    HRESULT GetScissorRect(RECT *rect);
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, UINT start_vertex, UINT primitive_count);
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count,
                                 UINT start_index, UINT primitive_count);
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitive_count, const void *data, UINT stride);
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min_index, UINT vertex_count, UINT primitive_count,
                                   const void *index_data, D3DFORMAT index_format, const void *data,
                                   UINT stride);
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, VertexDeclaration **decl);
    HRESULT SetVertexDeclaration(VertexDeclaration *decl);
    HRESULT GetVertexDeclaration(VertexDeclaration **decl);
    HRESULT SetFVF(DWORD fvf);
    HRESULT GetFVF(DWORD *fvf);
    HRESULT CreateVertexShader(const DWORD *function, VertexShader **shader);
    HRESULT SetVertexShader(VertexShader *shader);
    HRESULT GetVertexShader(VertexShader **shader);
    HRESULT SetVertexShaderConstantF(UINT start, const float *data, UINT count);
    HRESULT GetVertexShaderConstantF(UINT start, float *data, UINT count);
    HRESULT SetStreamSource(UINT stream, VertexBuffer *buffer, UINT offset, UINT stride);
    HRESULT GetStreamSource(UINT stream, VertexBuffer **buffer, UINT *offset, UINT *stride);
    HRESULT SetIndices(IndexBuffer *buffer);
    HRESULT GetIndices(IndexBuffer **buffer);
    HRESULT CreatePixelShader(const DWORD *function, PixelShader **shader);
    HRESULT SetPixelShader(PixelShader *shader);
    HRESULT GetPixelShader(PixelShader **shader);
    HRESULT SetPixelShaderConstantF(UINT start, const float *data, UINT count);
    HRESULT GetPixelShaderConstantF(UINT start, float *data, UINT count);
    HRESULT SetNPatchMode(float) { return D3D_OK; }
    HRESULT SetSoftwareVertexProcessing(BOOL) { return D3D_OK; }
    HRESULT ShowCursor(BOOL) { return D3D_OK; }
    HRESULT EvictManagedResources() { return D3D_OK; }

    // ---- internals ----
    struct DrawCall
    {
        D3DPRIMITIVETYPE type{};
        uint32_t primitive_count = 0;
        bool indexed = false;
        int32_t base_vertex = 0;
        uint32_t min_index = 0;    // indexed: D3D MinVertexIndex / NumVertices (the vertices the indices reach)
        uint32_t index_vertex_count = 0;
        uint32_t start_vertex = 0; // non-indexed
        uint32_t start_index = 0;
        const uint8_t *up_vertices = nullptr;
        uint32_t up_stride = 0;
        const void *up_indices = nullptr;
        bool up_index32 = false;
        uint32_t up_vertex_count = 0;
    };
    HRESULT draw(const DrawCall &call);
    // Object-space bounding sphere of the vertices a draw reaches (stream 0 positions); false when unknown.
    bool draw_bounds(const DrawCall &call, const LayoutInfo &layout, uint32_t element_count, float center[3],
                     float &radius);
    void skip_draw(const char *reason);
    const LayoutInfo *current_layout();
    const LayoutInfo *fvf_layout(DWORD fvf, uint32_t stride);
    bool build_decl_layout(VertexDeclaration &decl);
    uint16_t ensure_view();
    uint16_t new_view();
    bgfx::FrameBufferHandle framebuffer(TexStore *color, uint32_t face, uint32_t level, TexStore *depth);
    // Clears a render target that has never been written, before its first use.
    void clear_if_new(TexStore *store);
    void purge_framebuffers(bgfx::TextureHandle texture);
    void apply_ff_uniforms(const LayoutInfo &layout, bool vs_port, bool ps_port, const effects::FfDraw &fx);
    uint64_t render_state_bits(D3DPRIMITIVETYPE type, bool *stencil, uint32_t *fstencil);
    uint32_t sampler_flags(uint32_t stage, const TexStore &store) const;
    void set_default_state();
    void bind_state_refs(const DeviceState &next);
    void create_backbuffer(uint32_t width, uint32_t height);
    // The game picture's colour/depth stores for the current effects state (MSAA), kept inside the Surfaces the
    // engine holds (GetBackBuffer / GetRenderTarget results stay valid).
    void create_scene_stores(uint32_t width, uint32_t height);
    // Graphics effects (src/effects): the 3D scene of this frame is complete; runs the post chain on it and
    // makes its output the game picture the interface is drawn on. Once per presented frame.
    void scene_complete();
    // Graphics effects (src/effects/world.cpp): the view for an fx draw on the current render target, in submission
    // order; `fresh` opens a new one. Marked as drawn into; the shim re-sends its sticky uniforms afterwards.
    uint16_t effects_view(bool fresh);
    static Device *current; // the one device (effects hooks reach it through the public render API)
    bgfx::ProgramHandle program(uint32_t vs_id, uint32_t fs_id);
    // Creates program(vs_id, fs_id) unless it exists, within `budget` (reduced when one is created): the effects
    // prepare the programs of new settings ahead of their first draw. False when the budget is spent.
    bool prepare_program(uint32_t vs_id, uint32_t fs_id, int &budget);

    SDL_Window *window = nullptr;
    D3DPRESENT_PARAMETERS params{};
    DeviceState s;
    std::vector<Light> lights;
    Surface *back_color = nullptr, *back_depth = nullptr; // owned references
    Surface *rt = nullptr, *ds = nullptr;                  // owned references
    // Effects: the second game picture the post chain writes (swapped with back_color's store at the scene
    // boundary), and a single-sample depth for other render targets while the scene target is multisampled.
    TexStorePtr post_color, single_depth;
    bool scene_done = false;  // the post chain ran (or was not needed) in this presented frame
    bool scene_drawn = false; // a perspective draw reached the game picture in this presented frame
    bool interface_drawn = false; // an interface announced itself (MakePostProcess) in this presented frame
    bool post_view_logged = false;
    bool interface_first_logged = false;
    bool world_anisotropy = false; // fx::RendererState::max_anisotropy > 1, fixed when the device is created
    D3DMATRIX scene_view{}, scene_proj{}; // camera of the last perspective draw into the game picture
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
    std::unordered_map<uint16_t, uint32_t> program_uniform_bytes; // by program handle: user uniforms of both shaders
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
        U_DEPTHBIAS,
        U_COUNT
    };
    Uniform uniforms[U_COUNT];
    void set_uniform(UniformId id, const float *data, uint16_t count);
    void invalidate_uniforms();

    // What the previous draw of the frame submitted, for the change counters (perf_stats.hpp).
    struct LastDraw
    {
        uint16_t program = UINT16_MAX;
        uint64_t state = UINT64_MAX;
        uint32_t stencil = UINT32_MAX, factor = UINT32_MAX;
        uint16_t texture[kSamplerUnits];
        uint32_t sampler[kSamplerUnits];
        LastDraw()
        {
            std::fill(std::begin(texture), std::end(texture), UINT16_MAX);
            std::fill(std::begin(sampler), std::end(sampler), UINT32_MAX);
        }
    };
    LastDraw last_draw;

    // Vertex ranges of buffers written in this frame that are already in transient memory: later
    // draws of the same (or a contained) range bind the copy instead of copying again. Valid until
    // the next bgfx::frame(); a new Lock of the buffer (write_serial) or its release invalidates them.
    struct TransientRange
    {
        uint32_t bgfx_frame;
        const VertexBuffer *vb;
        uint32_t serial;
        uint16_t layout;
        uint32_t stride;
        size_t begin, bytes; // in the buffer's shadow copy
        bgfx::TransientVertexBuffer tvb;
    };
    std::vector<TransientRange> transient_ranges;
    void forget_transient(const VertexBuffer *vb);
    bool uniforms_changed = false; // a uniform was set since the previous submit

};

uint32_t shader_id_by_name(const char *name);
bgfx::ShaderHandle create_embedded_shader(uint32_t id);
uint32_t embedded_shader_count();

} // namespace storm::shim
