#pragma once

#include "backend_select.hpp"
#include "bone_palettes.hpp"
#include "frame_overlap.hpp"
#include "shim.hpp"
#include "storm/render/frame_owner.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <string>

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

// What the GPU and its driver say about themselves. OpenGL ES: read once on bgfx's render thread (the only thread
// with the GL context). Vulkan: from the probe made before bgfx::init (backend_select.hpp). Empty strings on the
// headless Noop backend.
struct DeviceInfo
{
    bool vulkan = false;
    // OpenGL ES: glGetString(GL_VENDOR / GL_RENDERER / GL_VERSION). Vulkan: "vendor 0x5143", the device name,
    // "Vulkan 1.3.128, driver 0x80273000".
    std::string vendor, renderer, version;
    std::string extensions;       // OpenGL ES only: glGetString(GL_EXTENSIONS)
    bool anisotropy = false;      // anisotropic filtering (EXT_texture_filter_anisotropic / samplerAnisotropy)
    float max_anisotropy = 0.0f;  // the device's highest level; 0 without anisotropy
    // Textures a vertex shader can sample (bgfx's GL backend has no caps bit for it): OpenGL ES
    // GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, Vulkan maxPerStageDescriptorSampledImages.
    int vertex_texture_units = 0;
    // A whole name of the OpenGL ES extension list.
    bool has_extension(const char *name) const;
};

// Owns bgfx::init/shutdown, the surface and the frame-owner contract.
//
// Threads. bgfx runs multithreaded (BGFX_CONFIG_MULTITHREADED): the game thread only records frames;
// bgfx::frame() hands a frame to bgfx's render thread, which owns the EGL context, executes the GL backend and
// presents, after bgfx::frame() has returned. The render thread reports each finished frame
// (render_frame_done, patches/bgfx-render-thread-hooks.patch); the surface is given back to Android, and Swappy's
// window taken away, only when every frame handed over is finished. Nothing outside bgfx calls GL or EGL.
// render_thread=0 (set_render_thread) keeps bgfx single-threaded for measurements: the backend then runs inside
// bgfx::frame() on the game thread.
class Context final : public render::FrameOwner
{
  public:
    static Context &instance();

    // Before the device exists (render::SetRenderThread): false = bgfx renders on the game thread.
    void set_render_thread(bool enabled);
    bool render_thread() const { return render_thread_active_; }
    // render::SetFrameOverlap: -1 chosen per frame (FrameOverlap), 0 never, 1 always. Any time.
    void set_frame_overlap(int mode);
    // render::SetRenderBackend. Before the device exists.
    void set_backend(render::Backend backend, std::filesystem::path state_directory);
    // render::SetVulkanAsyncPipelines.
    void set_async_pipelines(bool enabled);
    bool async_pipelines() const { return async_pipelines_; }
    // The backend the device runs and why, as logged ("render backend: ..." without the prefix); empty before.
    const std::string &backend_choice() const { return backend_choice_; }
    // Valid from init on.
    const DeviceInfo &device_info() const { return device_info_; }
    // Any thread: a frame handed to the render thread is not finished yet (rendered and presented).
    bool frames_in_flight()
    {
        std::lock_guard lock(release_mutex_);
        return !render_idle();
    }

    BonePalettes &bone_palettes() { return bone_palettes_; }

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
    // render::HoldGamePicture: asked by the application, kept by the Device (Device::Present).
    void hold_picture(bool hold) { hold_requested_ = hold; }
    bool hold_requested() const { return hold_requested_; }
    void set_picture_held(bool held) { picture_held_ = held; }
    bool picture_held() const { return hold_requested_ && picture_held_; }
    // The game picture the last present() composited (host checks: which picture a frame showed).
    bgfx::TextureHandle presented_game_color() const { return presented_game_color_; }
    // Surface lifecycle (Android): pause stops presenting, resume re-attaches the native window.
    void surface_lost();
    // Marks the surface for re-attachment; the actual bgfx reset happens once SDL holds a native window.
    void surface_restored();
    // Any thread (Android UI thread, before it destroys the surface): stops further presenting frames and
    // blocks until the render thread has finished those handed over, or the timeout expires.
    void request_surface_release();
    // Game thread, at safe points: honours a pending release request and retries a pending restore.
    void poll_surface();

    void begin_frame_if_needed();
    // Composites `game_color` to the surface, runs client UI hooks and presents.
    void present(bgfx::TextureHandle game_color, uint32_t game_width, uint32_t game_height);
    // The only place the game thread calls bgfx::frame(). A presenting frame is refused (false, bgfx::frame() not
    // called) when the surface is about to be destroyed; presentation is then suspended. A BGFX_FRAME_FLUSH frame
    // presents nothing and always runs: a read-back in progress completes, and the memory it writes stays valid.
    bool run_frame(uint32_t flags, uint32_t *frame_number = nullptr);

    uint64_t frame_index() const { return frame_.frame_index; }
    uint32_t last_frame() const { return last_frame_; } // bgfx::frame() number of the last presented frame
    // Counts bgfx::frame() calls (presents and read-backs): transient buffers live until the next one.
    uint32_t submitted_frames() const { return submitted_frames_; }
    const bgfx::Caps *caps() const { return caps_; }
    bool flip_y() const { return flip_y_; }
    // D3DTADDRESS_BORDER. OpenGL ES: GL_CLAMP_TO_BORDER (EXT/OES/NV_texture_border_clamp), which bgfx otherwise
    // turns into clamp-to-edge without telling. Vulkan: always.
    bool border_clamp() const { return border_clamp_; }
    // Any D3DSAMP_BORDERCOLOR. False on Vulkan without VK_EXT_custom_border_color: only transparent black, opaque
    // black and opaque white exist there (patches/bgfx-vk-builtin-border-colors.patch).
    bool border_colors() const { return border_colors_; }
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
  public:
    // A known picture drawn into a target and read back, before anything is presented on a backend that was just
    // chosen (Vulkan): shader modules, a pipeline, a texture upload, sampling, the target's orientation, the
    // read-back. False with `failure` set. Public for the host checks, which run it on every backend.
    bool backend_self_test(std::string *failure);

  private:
    bool start(SDL_Window *window, uint32_t width, uint32_t height, bool vsync); // null window: headless
    // bgfx::init on `type` for start(), up to the device's facts. `vulkan`: the probe's device for Vulkan. On
    // failure everything is torn down again and `failure` says why.
    bool start_backend(bgfx::RendererType::Enum type, const VulkanDevice *vulkan, uint32_t width, uint32_t height,
                       std::string *failure);
    // Undoes start_backend (and create_resources).
    void stop_backend();
    void create_resources();
    void try_restore();
    bool read_density(); // true when it changed
    uint32_t reset_flags() const;
    // Render thread (bgfx::setRenderFrameDone): frame `frame` is rendered and presented.
    static void render_frame_done(uint32_t frame);
    // True when the render thread has finished every frame handed over; release_mutex_ held.
    bool render_idle() const { return !in_frame_ && (!frames_handed_ || done_frame_ == handed_frame_); }
    // Waits for render_idle(); `lock` holds release_mutex_. False after `what` was logged as an error (3 s).
    bool wait_render_idle(std::unique_lock<std::mutex> &lock, const char *what);
    // Game thread, after presenting frame `number` was handed to the render thread: waits for the frame that
    // frame_overlap.hpp says (this one, or the one before). `game`: the game thread's work for the frame. Returns
    // the part of the time blocked for this frame (`blocked` in bgfx::frame() plus the wait here) that was not on
    // the frame's critical path, for the ADPF work clock.
    std::chrono::nanoseconds frame_gate(uint32_t number, std::chrono::nanoseconds game, std::chrono::nanoseconds blocked);

    SDL_Window *window_ = nullptr;
    bool initialised_ = false;
    bool frame_open_ = false;
    bool restore_pending_ = false;
    render::PictureRect picture_rect_;
    bool hold_requested_ = false, picture_held_ = false;
    bgfx::TextureHandle presented_game_color_ = BGFX_INVALID_HANDLE;
    std::atomic<bool> restore_requested_{false};
    std::atomic<bool> density_refresh_requested_{false};
    bool surface_epoch_flag_ = false;
    bool restore_wait_logged_ = false;
    bool frame_aborted_ = false; // draws of an unfinished frame are still queued in bgfx
    std::atomic<bool> release_requested_{false};
    std::atomic<bool> surface_in_use_{false};
    // Guarded by release_mutex_; release_done_ is notified when any of them changes.
    bool in_frame_ = false;       // the game thread is inside bgfx::frame()
    bool frames_handed_ = false;  // handed_frame_ is a frame of this device
    uint32_t handed_frame_ = 0;   // number of the last frame bgfx::frame() handed to the render thread
    uint32_t done_frame_ = 0;     // number of the last frame the render thread finished
    bool render_info_read_ = false; // the render thread's first finished frame filled what follows
    int render_tid_ = 0;            // the render thread's Linux tid
    DeviceInfo render_gl_info_;     // OpenGL ES: the driver's strings, read on the render thread
    std::chrono::steady_clock::time_point hand_time_[4]{}; // when frame (number & 3) was given to bgfx::frame()
    std::chrono::steady_clock::time_point done_time_{};    // when done_frame_ was finished
    std::chrono::nanoseconds render_work_{0}; // render thread's time for done_frame_ without its present wait
    std::mutex release_mutex_;
    std::condition_variable release_done_;
    bool render_thread_ = true;         // requested
    bool render_thread_active_ = false; // this device runs bgfx's render thread
    bool gl_backend_ = false;           // the device has a GL context (OpenGL ES; not Vulkan, not headless)
    bool vulkan_backend_ = false;
    bool async_pipelines_ = true;
    DeviceInfo device_info_;
    render::Backend backend_request_ = render::Backend::Auto;
    std::filesystem::path backend_state_directory_;
    std::string backend_choice_;
    std::optional<BackendAttempt> backend_attempt_;       // a Vulkan start not yet confirmed by presented frames
    uint32_t backend_presented_ = 0;                        // presented frames of this device
    BonePalettes bone_palettes_;
    bool flip_y_ = false;
    bool border_clamp_ = false;
    bool border_colors_ = false;
    const char *timer_queries_ = "none (no GL context)";
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
    uint32_t last_frame_ = 0;
    uint32_t submitted_frames_ = 0;
    std::chrono::steady_clock::duration frame_blocked_{}; // the last bgfx::frame() call, wall time
    FrameOverlap overlap_;
    int overlap_mode_ = -1;
    bool overlap_logged_ = false; // the first choice of this device is in the log
    std::chrono::steady_clock::time_point gate_end_{}; // the game thread's work for the next frame starts here
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
    // The view changed size: the back buffer (and the scene stores behind it) is made again at the
    // new size and bound as render target; every other state and resource stays as it is. Between
    // frames only.
    HRESULT ResizeBackBuffer(uint32_t width, uint32_t height);
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
    const LayoutInfo *build_decl_layout(VertexDeclaration &decl);
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
    // fx internal scene height (fx::RenderScale(RendererState::render_height, display) < 1): a 3D frame draws into the internal-size scene stores
    // (enter_scaled_picture, at the start of a frame after a 3D frame); at the scene boundary the post chain upscales
    // the scene onto the display-size picture, which the interfaces then draw on (leave_scaled_picture: `copy` the
    // internal picture up when no post chain ran, e.g. an interface before any 3D; `depth_kept`: the display-size
    // depth was given the scene's, not cleared).
    void enter_scaled_picture();
    void leave_scaled_picture(bool copy, bool depth_kept = false);
    // Store pixels per game-addressed pixel of the current render target (1 except on the scaled scene stores).
    std::array<float, 2> target_scale() const;
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
    // fx render scale: the internal-size scene colour + depth, and the display-size stores parked while they draw.
    TexStorePtr scaled_color, scaled_depth, held_color, held_depth;
    // The picture presented again while a pause menu holds the game (render::HoldGamePicture).
    TexStorePtr paused_picture;
    uint32_t frame_surface_generation = 0; // the surface generation when the previous frame was presented
    bool backbuffer_recreated = false;     // create_backbuffer ran since the previous Present
    // A small copy of the presented frame every kFrameCopySeconds, read back without waiting (Present): what
    // GetRenderTargetData returns for the game's back buffer while there is no surface (a save made as the app goes
    // to the background: its thumbnail), as the read-back would without a surface to render on.
    struct FrameCopy
    {
        TexStorePtr target;                      // up to kFrameCopyWidth wide, the frame's aspect
        bgfx::TextureHandle staging = BGFX_INVALID_HANDLE;
        std::vector<uint8_t> reading, pixels;    // RGBA8: the read in flight, the last one finished
        uint32_t width = 0, height = 0, ready_frame = 0;
        uint32_t scene_frame = 0; // a bgfx frame number not later than the frame the copied scene was drawn in
        bool pending = false;
        std::chrono::steady_clock::time_point taken{}, finished{};
    } frame_copy;
    void copy_frame(const TexStorePtr &shown);
    bool read_frame_copy(Surface *to);
    void keep_paused_picture();
    void release_paused_picture();
    bool scaled_next = false;          // the next frame starts on the scaled stores (this one had a 3D scene)
    bool held_depth_clear = false;     // the display-size depth returned from parking is cleared in its next view
    uint16_t view_clear_bits = 0;      // clear bits new_view() gave the current view (Clear keeps them)
    // The current view's clear as last set (new_view, Clear): scene_complete adds discard flags without changing it.
    uint16_t view_clear_set = 0;
    uint32_t view_clear_rgba = 0;
    float view_clear_z = 1.0f;
    uint8_t view_clear_stencil = 0;
    TexStore *view_color = nullptr; // the current view's colour target
    bool scaled_copy_logged = false;
    std::array<uint32_t, 3> scene_size_logged{}; // scene width, height and internal height last logged
    bool scene_done = false;  // the post chain ran (or was not needed) in this presented frame
    bool scene_drawn = false; // a perspective draw reached the game picture in this presented frame
    bool interface_drawn = false; // an interface announced itself (MakePostProcess) in this presented frame
    bool post_view_logged = false;
    bool interface_first_logged = false;
    bool world_anisotropy = false; // fx::RendererState::max_anisotropy > 1, fixed when the device is created
    // D3DSAMP_BORDERCOLOR values in bgfx palette entries (BGFX_SAMPLER_BORDER_COLOR), allocated on first use.
    mutable std::vector<DWORD> border_colors;
    uint32_t border_color_index(DWORD color) const;
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
    std::vector<bgfx::ShaderHandle> shaders; // index = shader id

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
    // The GPU copy of a buffer drawn from its own storage (not as transient data): see draw.cpp.
    void upload_buffer(VertexBuffer &vb, const bgfx::VertexLayout &layout);
    void upload_buffer(IndexBuffer &ib, bool index32);
    bool uniforms_changed = false; // a uniform was set since the previous submit

};

uint32_t shader_id_by_name(const char *name);
bgfx::ShaderHandle create_embedded_shader(uint32_t id);
// The FF_OPAQUE twin (no discard: early depth stays on) of a fixed-function fragment shader, UINT32_MAX if none.
uint32_t opaque_twin(uint32_t fs_id);
uint32_t embedded_shader_count();

} // namespace storm::shim
