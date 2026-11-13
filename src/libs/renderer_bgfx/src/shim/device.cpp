#include "device.hpp"
#include "effects/effects.hpp"
#include "perf.hpp"
#include "util.hpp"
#include "../texture/core/formats.hpp"

#include <bx/allocator.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>

namespace storm::shim
{
Direct3D *CreateDirect3D()
{
    return new Direct3D();
}

// ---------------------------------------------------------------------------
// Direct3D
HRESULT Direct3D::GetAdapterDisplayMode(UINT, D3DDISPLAYMODE *mode)
{
    SDL_DisplayMode current{};
    SDL_GetCurrentDisplayMode(0, &current);
    mode->Width = current.w > 0 ? current.w : 1280;
    mode->Height = current.h > 0 ? current.h : 720;
    mode->RefreshRate = current.refresh_rate;
    mode->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
}

void fill_caps(D3DCAPS9 *caps)
{
    *caps = {};
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->MaxTextureWidth = caps->MaxTextureHeight = 4096;
    caps->MaxVolumeExtent = 256;
    caps->MaxTextureBlendStages = kFfStages;
    caps->MaxSimultaneousTextures = kFfStages;
    caps->MaxActiveLights = kMaxLights;
    caps->MaxUserClipPlanes = 1;
    caps->MaxAnisotropy = 16;
    caps->MaxStreams = kMaxStreams;
    caps->MaxPrimitiveCount = 0xFFFFFF;
    caps->MaxVertexIndex = 0xFFFFFF;
    caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
    caps->PixelShaderVersion = D3DPS_VERSION(1, 4);
    caps->MaxVertexShaderConst = kVsConstants;
    caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
                              D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
                              D3DPTFILTERCAPS_MAGFLINEAR;
    caps->CubeTextureFilterCaps = caps->VolumeTextureFilterCaps = caps->TextureFilterCaps;
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_MIPMAP |
                        D3DPTEXTURECAPS_MIPCUBEMAP | D3DPTEXTURECAPS_PROJECTED | D3DPTEXTURECAPS_VOLUMEMAP |
                        D3DPTEXTURECAPS_PERSPECTIVE;
    caps->TextureOpCaps = 0x03FFFFFF;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP;
    caps->RasterCaps = D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_FOGRANGE |
                       D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_ANISOTROPY |
                       D3DPRASTERCAPS_SCISSORTEST | D3DPRASTERCAPS_DEPTHBIAS | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS;
    caps->ZCmpCaps = caps->AlphaCmpCaps = 0xFF;
    caps->SrcBlendCaps = caps->DestBlendCaps = 0x1FFF;
    caps->StencilCaps = 0xFF;
    caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_PUREDEVICE;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                              D3DPMISCCAPS_BLENDOP | D3DPMISCCAPS_TSSARGTEMP | D3DPMISCCAPS_COLORWRITEENABLE;
    caps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS |
                                 D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
    caps->FVFCaps = 8;
    caps->MaxPointSize = 64.0f;
    caps->NumSimultaneousRTs = 1;
    caps->PresentationIntervals = D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_IMMEDIATE;
}

HRESULT Direct3D::GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9 *caps)
{
    fill_caps(caps);
    return D3D_OK;
}

HRESULT Direct3D::CreateDevice(UINT, D3DDEVTYPE, HWND window, DWORD, D3DPRESENT_PARAMETERS *params,
                               Device **device)
{
    auto *sdl_window = static_cast<SDL_Window *>(window);
    const bool vsync = params->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE;
    int w = 0, h = 0;
    SDL_GetWindowSize(sdl_window, &w, &h);
    if (w <= 0 || h <= 0)
    {
        w = static_cast<int>(params->BackBufferWidth);
        h = static_cast<int>(params->BackBufferHeight);
    }
    if (!Context::instance().init(sdl_window, static_cast<uint32_t>(w), static_cast<uint32_t>(h), vsync))
        return D3DERR_NOTAVAILABLE;
    if (params->BackBufferWidth == 0 || params->BackBufferHeight == 0)
    {
        params->BackBufferWidth = static_cast<UINT>(w);
        params->BackBufferHeight = static_cast<UINT>(h);
    }
    *device = new Device(sdl_window, *params);
    return D3D_OK;
}

// ---------------------------------------------------------------------------
// Device lifetime
Device *Device::current = nullptr;

Device::Device(SDL_Window *sdl_window, const D3DPRESENT_PARAMETERS &present) : window(sdl_window), params(present)
{
    current = this;
    lights.resize(kMaxLights);
    shaders.assign(embedded_shader_count(), BGFX_INVALID_HANDLE);

    const struct
    {
        UniformId id;
        const char *name;
        bgfx::UniformType::Enum type;
        uint16_t count;
    } table[] = {
        {U_PROJ, "u_ffProj", bgfx::UniformType::Mat4, 1},
        {U_VS, "u_ffVs", bgfx::UniformType::Vec4, 4},
        {U_CLIP, "u_ffClipPlane", bgfx::UniformType::Vec4, 1},
        {U_MATERIAL, "u_ffMaterial", bgfx::UniformType::Vec4, 4},
        {U_AMBIENT, "u_ffAmbient", bgfx::UniformType::Vec4, 1},
        {U_LIGHTPOS, "u_ffLightPos", bgfx::UniformType::Vec4, kMaxLights},
        {U_LIGHTDIR, "u_ffLightDir", bgfx::UniformType::Vec4, kMaxLights},
        {U_LIGHTDIFFUSE, "u_ffLightDiffuse", bgfx::UniformType::Vec4, kMaxLights},
        {U_LIGHTAMBIENT, "u_ffLightAmbient", bgfx::UniformType::Vec4, kMaxLights},
        {U_LIGHTSPECULAR, "u_ffLightSpecular", bgfx::UniformType::Vec4, kMaxLights},
        {U_LIGHTATT, "u_ffLightAtt", bgfx::UniformType::Vec4, kMaxLights},
        {U_TEXCFG, "u_ffTexCfg", bgfx::UniformType::Vec4, kFfStages},
        {U_TEXMTX, "u_ffTexMtx", bgfx::UniformType::Mat4, kFfStages},
        {U_STAGE, "u_ffStage", bgfx::UniformType::Vec4, kFfStages * 3},
        {U_FS, "u_ffFs", bgfx::UniformType::Vec4, 4},
        {U_VSC, "u_vsc", bgfx::UniformType::Vec4, kVsConstants},
        {U_PSC, "u_psc", bgfx::UniformType::Vec4, kPsConstants},
        {U_BEM, "u_psBem", bgfx::UniformType::Vec4, kFfStages},
        {U_DEPTHBIAS, "u_ffDepthBias", bgfx::UniformType::Vec4, 1},
    };
    for (const auto &entry : table)
    {
        uniforms[entry.id].handle = bgfx::createUniform(entry.name, entry.type, entry.count);
        uniforms[entry.id].count = entry.count;
    }
    set_default_state();
    create_backbuffer(params.BackBufferWidth, params.BackBufferHeight);
}

Device::~Device()
{
    DeviceState empty;
    bind_state_refs(empty);
    if (rt)
        rt->Release();
    if (ds)
        ds->Release();
    if (back_color)
        back_color->Release();
    if (back_depth)
        back_depth->Release();
    for (auto &[key, handle] : framebuffers)
        bgfx::destroy(handle);
    framebuffers.clear();
    if (bgfx::isValid(frame_copy.staging) && Context::instance().initialised())
        bgfx::destroy(frame_copy.staging);
    frame_copy.target.reset();
    post_color.reset();
    single_depth.reset();
    scaled_color.reset();
    scaled_depth.reset();
    held_color.reset();
    held_depth.reset();
    current = nullptr;
    Context::instance().shutdown();
}

void Device::create_backbuffer(uint32_t width, uint32_t height)
{
    if (rt)
        rt->Release();
    if (ds)
        ds->Release();
    if (back_color)
        back_color->Release();
    if (back_depth)
        back_depth->Release();
    back_color = new Surface(nullptr, 0, 0);
    back_depth = new Surface(nullptr, 0, 0);
    held_color.reset(); // parked stores of the old surfaces
    held_depth.reset();
    release_paused_picture();
    backbuffer_recreated = true;
    create_scene_stores(width, height);
    rt = back_color;
    rt->AddRef();
    ds = back_depth;
    ds->AddRef();
    s.viewport = {0, 0, width, height, 0.0f, 1.0f};
    s.scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    view_dirty = true;
}

void Device::create_scene_stores(uint32_t width, uint32_t height)
{
    // A parked frame goes back to the display-size stores first: they are the ones made or kept below.
    if (held_color)
    {
        back_color->store = std::move(held_color);
        back_depth->store = std::move(held_depth);
        held_color.reset();
        held_depth.reset();
    }
    // Effects: MSAA of the scene target (fx::RendererState). bgfx resolves colour and depth when the frame buffer
    // is left; the composite and the post chain read the resolved textures.
    const auto &host = effects::Host::instance();
    const auto &state = host.renderer_state();
    const uint64_t msaa = state.msaa_samples >= 4 ? BGFX_TEXTURE_RT_MSAA_X4
                          : state.msaa_samples == 2 ? BGFX_TEXTURE_RT_MSAA_X2
                                                    : 0;
    // fx internal scene height: on a taller display the 3D scene draws into stores of the internal size and the post
    // chain upscales it (FSR 1) onto the display-size picture, which carries no MSAA then: only the interfaces draw on
    // it. Without post effects («Низкая») the chain is the upscale alone.
    const float scale = fx::RenderScale(state.render_height, height);
    const bool scaled = scale < 1.0f;
    const uint32_t scaled_width = std::max(1u, static_cast<uint32_t>(std::lround(width * scale)));
    const uint32_t scaled_height = std::max(1u, static_cast<uint32_t>(std::lround(height * scale)));
    const uint64_t picture_msaa = scaled ? 0 : msaa;
    // A multisampled depth is resolved into a texture only when an effect reads it (ambient occlusion, the high
    // water's scene capture): otherwise it stays a write-only render buffer, so leaving the scene target resolves
    // colour alone (less bandwidth on tile GPUs, and no depth/stencil resolve blit, which drivers get wrong).
    const auto depth_flags = [&](uint64_t samples) {
        return samples | (samples && !host.scene_depth_read() ? BGFX_TEXTURE_RT_WRITE_ONLY : 0);
    };
    // A settings switch rebuilds only the targets it changes (an anisotropy change none of them).
    const auto fits = [](const std::shared_ptr<TexStore> &store, uint32_t w, uint32_t h, uint64_t flags) {
        return store && store->width == w && store->height == h && store->target_flags == flags;
    };
    const auto target = [&](uint32_t w, uint32_t h, D3DFORMAT format, DWORD usage, uint64_t flags) {
        auto store = make_store(this, TexKind::Tex2D, w, h, 1, 1, usage, format, D3DPOOL_DEFAULT);
        store->backbuffer = true;
        store->target_flags = flags;
        return store;
    };
    if (!fits(back_color->store, width, height, picture_msaa))
        back_color->store = target(width, height, D3DFMT_A8R8G8B8, D3DUSAGE_RENDERTARGET, picture_msaa);
    if (!fits(back_depth->store, width, height, depth_flags(picture_msaa)))
        back_depth->store = target(width, height, D3DFMT_D24S8, D3DUSAGE_DEPTHSTENCIL, depth_flags(picture_msaa));
    // The second display-size picture of the post chain's ping-pong; a scaled 3D frame needs none (the chain writes
    // the display-size picture directly), so it is made only when a 3D frame is drawn at the display size.
    if (!host.post_active() || scaled)
        post_color = nullptr;
    else if (!fits(post_color, width, height, msaa))
        post_color = target(width, height, D3DFMT_A8R8G8B8, D3DUSAGE_RENDERTARGET, msaa);
    if (scaled)
    {
        if (!fits(scaled_color, scaled_width, scaled_height, msaa))
            scaled_color = target(scaled_width, scaled_height, D3DFMT_A8R8G8B8, D3DUSAGE_RENDERTARGET, msaa);
        if (!fits(scaled_depth, scaled_width, scaled_height, depth_flags(msaa)))
            scaled_depth = target(scaled_width, scaled_height, D3DFMT_D24S8, D3DUSAGE_DEPTHSTENCIL, depth_flags(msaa));
        for (TexStore *store : {scaled_color.get(), scaled_depth.get()})
        {
            store->game_width = width;
            store->game_height = height;
        }
    }
    else
    {
        scaled_color = nullptr;
        scaled_depth = nullptr;
    }
    const std::array<uint32_t, 3> decided{scaled ? scaled_width : width, scaled ? scaled_height : height, state.render_height};
    if (decided != scene_size_logged)
    {
        scene_size_logged = decided;
        if (scaled)
            logf("effects: render scale %.2f: the 3D scene at %ux%u (internal height %u), upscaled by FSR 1 to %ux%u, "
                 "interfaces at %ux%u",
                 static_cast<double>(scale), scaled_width, scaled_height, state.render_height, width, height, width, height);
        else
            logf("effects: the 3D scene at the display size %ux%u%s", width, height,
                 state.render_height ? " (not taller than the internal height: no upscale)" : "");
    }
    if (!msaa)
        single_depth = nullptr;
    else if (!fits(single_depth, width, height, 0))
        single_depth = make_store(this, TexKind::Tex2D, width, height, 1, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8,
                                  D3DPOOL_DEFAULT);
    world_anisotropy = state.max_anisotropy > 1;
    view_dirty = true;
    if (scaled_next)
        enter_scaled_picture();
}

// The presented picture becomes the paused one; the game draws on a new store of the same kind meanwhile.
void Device::keep_paused_picture()
{
    const TexStore &shown = *back_color->store;
    auto fresh = make_store(this, TexKind::Tex2D, shown.width, shown.height, 1, 1, shown.usage, shown.format,
                            D3DPOOL_DEFAULT);
    fresh->backbuffer = true;
    fresh->target_flags = shown.target_flags;
    paused_picture = std::move(back_color->store);
    back_color->store = std::move(fresh);
    view_dirty = true;
    Context::instance().set_picture_held(true);
}

void Device::release_paused_picture()
{
    if (!paused_picture)
        return;
    paused_picture.reset();
    Context::instance().set_picture_held(false);
}

void Device::enter_scaled_picture()
{
    if (!scaled_color || held_color)
        return;
    held_color = std::move(back_color->store);
    held_depth = std::move(back_depth->store);
    back_color->store = scaled_color;
    back_depth->store = scaled_depth;
    held_depth_clear = false; // the parked depth is not drawn with until the scene ends
    view_dirty = true;
}

void Device::leave_scaled_picture(bool copy, bool depth_kept)
{
    if (!held_color)
        return;
    if (copy && Context::instance().ready())
    {
        // No post chain upscaled this frame (no 3D before the first interface, no surface, no views left): what was
        // drawn so far is copied up, bilinear, so the interface is drawn at the display size like on every frame.
        if (!scaled_copy_logged)
            logd("effects: a frame without a 3D scene on the scaled picture is copied up to the display size");
        scaled_copy_logged = true;
        Surface from(back_color->store, 0, 0), to(held_color, 0, 0);
        StretchRect(&from, nullptr, &to, nullptr, D3DTEXF_LINEAR);
    }
    back_color->store = std::move(held_color);
    back_depth->store = std::move(held_depth);
    held_color.reset();
    held_depth.reset();
    held_depth_clear = !depth_kept; // else it holds the scene's (copy_scene_depth)
    view_dirty = true;
}

std::array<float, 2> Device::target_scale() const
{
    const TexStore &store = *rt->store;
    return {static_cast<float>(store.level_width(rt->level)) / static_cast<float>(store.addressed_width(rt->level)),
            static_cast<float>(store.level_height(rt->level)) / static_cast<float>(store.addressed_height(rt->level))};
}

void Device::set_default_state()
{
    s = DeviceState{};
    s.world = s.view = s.proj = identity();
    for (auto &m : s.texture)
        m = identity();
    DWORD *rs = s.rs;
    rs[D3DRS_ZENABLE] = D3DZB_TRUE;
    rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    rs[D3DRS_ZWRITEENABLE] = TRUE;
    rs[D3DRS_LASTPIXEL] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_DITHERENABLE] = FALSE;
    rs[D3DRS_FOGSTART] = as_dword(0.0f);
    rs[D3DRS_FOGEND] = as_dword(1.0f);
    rs[D3DRS_FOGDENSITY] = as_dword(1.0f);
    rs[D3DRS_STENCILFAIL] = rs[D3DRS_STENCILZFAIL] = rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILMASK] = rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_CLIPPING] = TRUE;
    rs[D3DRS_LIGHTING] = TRUE;
    rs[D3DRS_COLORVERTEX] = TRUE;
    rs[D3DRS_LOCALVIEWER] = TRUE;
    rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_POINTSIZE] = as_dword(1.0f);
    rs[D3DRS_POINTSIZE_MIN] = as_dword(1.0f);
    rs[D3DRS_POINTSCALE_A] = as_dword(1.0f);
    rs[D3DRS_POINTSIZE_MAX] = as_dword(64.0f);
    rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
    rs[D3DRS_COLORWRITEENABLE] = 0xF;
    rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
    for (uint32_t stage = 0; stage < kMaxStages; ++stage)
    {
        DWORD *t = s.tss[stage];
        t[D3DTSS_COLOROP] = stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        t[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        t[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAOP] = stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        t[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        t[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        t[D3DTSS_TEXCOORDINDEX] = stage;
        t[D3DTSS_COLORARG0] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
        t[D3DTSS_RESULTARG] = D3DTA_CURRENT;
        DWORD *sm = s.sampler[stage];
        sm[D3DSAMP_ADDRESSU] = sm[D3DSAMP_ADDRESSV] = sm[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        sm[D3DSAMP_MAGFILTER] = sm[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        sm[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        sm[D3DSAMP_MAXANISOTROPY] = 1;
    }
    s.material.Diffuse = {1, 1, 1, 1};
}

void Device::bind_state_refs(const DeviceState &next)
{
    for (uint32_t i = 0; i < kMaxStages; ++i)
        assign_ref(s.textures[i], next.textures[i]);
    assign_ref(s.vertex_texture, next.vertex_texture);
    for (uint32_t i = 0; i < kMaxStreams; ++i)
        assign_ref(s.stream[i], next.stream[i]);
    assign_ref(s.indices, next.indices);
    assign_ref(s.decl, next.decl);
    assign_ref(s.vs, next.vs);
    assign_ref(s.ps, next.ps);
}

HRESULT Device::GetDeviceCaps(D3DCAPS9 *caps)
{
    fill_caps(caps);
    return D3D_OK;
}

HRESULT Device::GetDirect3D(Direct3D **d3d)
{
    *d3d = new Direct3D();
    return D3D_OK;
}

HRESULT Device::Reset(D3DPRESENT_PARAMETERS *present)
{
    params = *present;
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w > 0 && h > 0)
        Context::instance().resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    DeviceState empty;
    bind_state_refs(empty);
    set_default_state();
    for (auto &[key, handle] : framebuffers)
        bgfx::destroy(handle);
    framebuffers.clear();
    create_backbuffer(params.BackBufferWidth, params.BackBufferHeight);
    return D3D_OK;
}

HRESULT Device::ResizeBackBuffer(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return D3DERR_INVALIDCALL;
    params.BackBufferWidth = width;
    params.BackBufferHeight = height;
    for (auto &[key, handle] : framebuffers) // made for the old back buffer among others
        bgfx::destroy(handle);
    framebuffers.clear();
    create_backbuffer(width, height);
    return D3D_OK;
}

// ---------------------------------------------------------------------------
// frame / views
HRESULT Device::BeginScene()
{
    Context::instance().begin_frame_if_needed();
    return D3D_OK;
}

HRESULT Device::Present()
{
    auto &context = Context::instance();
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w > 0 && h > 0)
        context.resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    if (context.ready())
    {
        back_color->store->sync();
        back_depth->store->sync();
    }
    // No interface marked the end of the scene: the whole frame is the scene. A frame whose interface was drawn
    // before any 3D cannot be split: it is presented as it is.
    if (!scene_done && !interface_drawn)
        scene_complete();
    leave_scaled_picture(true); // a frame that never reached a post chain (no surface): presented at the display size
    const bool scene_frame = scene_drawn;
    scene_done = false;
    scene_drawn = false;
    interface_drawn = false;
    context.set_game_target({back_color->store->handle.idx, back_depth->store->handle.idx, back_color->store->width,
                             back_color->store->height});
    auto &effects = effects::Host::instance();
    if (context.ready())
        effects.advance_switch(); // a settings change: programs, self-test (views kPostViewBegin..), then the switch
    // render::HoldGamePicture (a pause menu over the game): the first 3D frame presented after the hold was asked for
    // is kept and presented in place of the following frames, which draw no scene (GamePictureHeld). A kept picture
    // of another size than the game picture (the display or the window changed) is dropped: the next frame is drawn
    // in full and kept again; so is one the context stopped trusting (a lost or restored surface: Context::picture_held
    // went false and the engine drew this frame in full).
    if (paused_picture && (!context.picture_held() || paused_picture->width != back_color->store->width ||
                           paused_picture->height != back_color->store->height))
        release_paused_picture();
    const TexStorePtr &shown_store = paused_picture ? paused_picture : back_color->store;
    const TexStore &shown = *shown_store;
    if (context.ready())
        copy_frame(shown_store);
    const uint32_t presented = context.last_frame();
    context.present(shown.handle, shown.width, shown.height);
    // A frame in which the surface or the back buffer changed is not kept: drawn across a surface re-creation or a
    // resize its picture can be lost with the old surface (a black picture behind the menu after a rotation). The
    // engine draws the next one in full as well (nothing is held yet), and that one is kept.
    const bool stable = context.Surface().generation == frame_surface_generation && !backbuffer_recreated;
    if (context.hold_requested() && !paused_picture && scene_frame && stable && context.last_frame() != presented)
        keep_paused_picture();
    frame_surface_generation = context.Surface().generation;
    backbuffer_recreated = false;
    effects.end_frame(scene_frame, context.last_frame());
    // The render scale applies to 3D frames: the next frame starts on the scaled scene stores after a 3D frame, on the
    // display-size picture otherwise (menus, loading screens).
    scaled_next = scene_frame;
    if (effects.take_renderer_state_change())
    {
        release_paused_picture(); // drawn with the old settings: the next frame is drawn in full and kept again
        // Between frames: the scene target follows the new MSAA setting and render scale, samplers the new anisotropy.
        create_scene_stores(back_color->store->width, back_color->store->height);
        context.refresh_reset_flags();
    }
    else if (scaled_next)
        enter_scaled_picture();
    views_used = 0;
    view_dirty = true;
    view_has_content = false;
    invalidate_uniforms();
    last_draw = LastDraw{};
    transient_ranges.clear();
    return D3D_OK;
}

void Device::scene_complete()
{
    interface_drawn = true;
    // Every interface of the frame ends the scene (Renderer::MakePostProcess): only the first one runs the post
    // chain. The later ones draw on the processed picture; run again, the chain graded, bloomed and tone-mapped it
    // once more per interface (three times in a town with its battle interface, log and perk icons).
    if (scene_done)
        return;
    if (!scene_drawn)
    {
        // Nothing 3D in the game picture yet: the interface and the 3D drawn after it cannot be separated, so this
        // frame is presented without the post chain.
        if (!interface_first_logged && effects::Host::instance().post_active())
            logf("effects: an interface was drawn before the 3D scene; such frames are presented without post effects");
        interface_first_logged = true;
        leave_scaled_picture(true);
        return;
    }
    scene_done = true;
    auto &host = effects::Host::instance();
    // A scaled scene is upscaled straight onto the parked display-size picture; a display-size one ping-pongs.
    const bool scaled = held_color != nullptr;
    if (!(host.post_active() || scaled) || !Context::instance().ready())
    {
        leave_scaled_picture(true);
        return;
    }
    if (!scaled && !post_color)
    {
        // The settings switched post-processing on after the device was created.
        post_color = make_store(this, TexKind::Tex2D, back_color->store->width, back_color->store->height, 1, 1,
                                D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
        post_color->backbuffer = true;
        post_color->target_flags = back_color->store->target_flags;
    }
    // The post chain writes this target outside the shim's views: a new one (the first post frame after a settings
    // switch) is cleared now, in a view before the chain's. Cleared later, by the first shim view that draws the
    // interface into it, it wiped the processed scene: one black frame after every preset change.
    TexStore *out = scaled ? held_color.get() : post_color.get();
    out->sync();
    clear_if_new(out);
    const uint16_t budget = render::kGameViewEnd - render::kGameViewBegin;
    // A scaled scene also carries its depth to the display-size picture afterwards (one view).
    if (views_used + fx::PostChain::kMaxViews + (scaled ? 1 : 0) > budget)
    {
        if (!post_view_logged)
            logf("ERROR: effects: %u game views used, no room for the post chain (%u): frame presented without it",
                 views_used, fx::PostChain::kMaxViews);
        post_view_logged = true;
        leave_scaled_picture(true);
        return;
    }
    TexStore *scene = back_color->store.get();
    scene->sync();
    back_depth->store->sync();
    // The multisampled scene is resolved when bgfx leaves it for the chain's first view; nothing reads its
    // multisampled attachments afterwards in this frame (the interface draws on the processed picture, which takes a
    // single-sample depth) and the next frame clears them. Invalidating them after the resolve spares a tile GPU
    // writing them back to memory (patches/bgfx-gl-framebuffer-discard.patch). Single-sample targets are the
    // textures the chain reads: never discarded.
    // Vulkan: bgfx resolves a multisampled texture after its render pass, from the stored attachment (colour:
    // vkCmdResolveImage; a depth an effect reads: a shader pass), so the discard would leave nothing to resolve.
    // Only a multisampled depth that nothing reads can go there.
    if (view_color == scene && (scene->target_flags & BGFX_TEXTURE_RT_MSAA_MASK) &&
        (back_depth->store->target_flags & BGFX_TEXTURE_RT_MSAA_MASK))
    {
        uint16_t discard = BGFX_CLEAR_DISCARD_COLOR_0 | BGFX_CLEAR_DISCARD_DEPTH | BGFX_CLEAR_DISCARD_STENCIL;
        if (Context::instance().device_info().vulkan)
            discard = (back_depth->store->target_flags & BGFX_TEXTURE_RT_WRITE_ONLY)
                          ? BGFX_CLEAR_DISCARD_DEPTH | BGFX_CLEAR_DISCARD_STENCIL
                          : BGFX_CLEAR_NONE;
        bgfx::setViewClear(view, view_clear_set | discard, view_clear_rgba, view_clear_z, view_clear_stencil);
    }
    effects::Host::PostTargets targets;
    targets.scene_color = scene->handle;
    // A multisampled depth is resolved into the texture with the colour only when an effect reads it.
    if (!(back_depth->store->target_flags & BGFX_TEXTURE_RT_WRITE_ONLY))
        targets.scene_depth = back_depth->store->handle;
    targets.width = static_cast<uint16_t>(scene->width);
    targets.height = static_cast<uint16_t>(scene->height);
    targets.target_width = static_cast<uint16_t>(out->width);
    targets.target_height = static_cast<uint16_t>(out->height);
    targets.target = framebuffer(out, 0, 0, nullptr);
    targets.first_view = static_cast<bgfx::ViewId>(render::kGameViewBegin + views_used);
    memcpy(targets.view.data(), &scene_view.m[0][0], sizeof(float) * 16);
    memcpy(targets.projection.data(), &scene_proj.m[0][0], sizeof(float) * 16);
    views_used += host.submit_post(targets);
    // The interface is drawn on the processed picture, which is then presented.
    if (scaled)
    {
        // The game draws 3D after the scene boundary too (ship foam, particles, coronas, the ship pointer): on the
        // display-size picture, depth-tested against the scene as on the original renderer. Its depth gets the
        // scene's (the scaled depth is sampleable: Host::scene_depth_read); left cleared, those draws showed
        // through ships and islands.
        TexStore *scene_depth = back_depth->store.get();
        TexStore *display_depth = held_depth.get();
        host.copy_scene_depth(static_cast<bgfx::ViewId>(render::kGameViewBegin + views_used++), scene_depth->handle,
                              framebuffer(held_color.get(), 0, 0, display_depth),
                              static_cast<uint16_t>(display_depth->width), static_cast<uint16_t>(display_depth->height));
        leave_scaled_picture(false, true);
    }
    else
        std::swap(back_color->store, post_color);
    view_dirty = true;
}

uint16_t Device::effects_view(bool fresh)
{
    if (fresh)
        view_dirty = true;
    const uint16_t id = ensure_view();
    view_has_content = true; // a later Clear opens a new view instead of clearing the fx draws
    invalidate_uniforms();
    return id;
}

HRESULT Device::GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, Surface **surface)
{
    *surface = back_color;
    back_color->AddRef();
    return D3D_OK;
}

// D3D9 leaves the contents of a new render target undefined, but the reference (desktop drivers,
// DXVK) hands them out cleared, and the game relies on it: the sea environment cube map never
// renders its -Y face above water, and on mobile GPUs that face showed uninitialised memory as
// sparkling noise in the water. Every face and level starts at zero, like the reference.
void Device::clear_if_new(TexStore *store)
{
    if (!store->needs_clear || !Context::instance().ready())
        return;
    store->needs_clear = false;
    for (uint32_t face = 0; face < store->faces(); ++face)
        for (uint32_t level = 0; level < store->levels; ++level)
        {
            if (views_used >= render::kGameViewEnd - render::kGameViewBegin)
            {
                trap("view budget exhausted: a new render target stays uncleared");
                return;
            }
            const uint16_t id = render::kGameViewBegin + views_used++;
            bgfx::setViewFrameBuffer(id, framebuffer(store, face, level, nullptr));
            bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
            bgfx::setViewRect(id, 0, 0, static_cast<uint16_t>(store->level_width(level)),
                              static_cast<uint16_t>(store->level_height(level)));
            bgfx::setViewClear(id, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
            bgfx::touch(id);
        }
}

bgfx::FrameBufferHandle Device::framebuffer(TexStore *color, uint32_t face, uint32_t level, TexStore *depth)
{
    color->sync();
    if (depth)
        depth->sync();
    const FbKey key{color->handle.idx, depth ? depth->handle.idx : static_cast<uint16_t>(UINT16_MAX),
                    static_cast<uint8_t>(face), static_cast<uint8_t>(level)};
    const auto it = framebuffers.find(key);
    if (it != framebuffers.end())
        return it->second;
    bgfx::Attachment attachments[2];
    attachments[0].init(color->handle, bgfx::Access::Write, static_cast<uint16_t>(face), 1,
                        static_cast<uint16_t>(level), BGFX_ATTACHMENT_NONE);
    uint8_t count = 1;
    if (depth)
        attachments[count++].init(depth->handle, bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);
    const bgfx::FrameBufferHandle handle = bgfx::createFrameBuffer(count, attachments, false);
    if (!bgfx::isValid(handle))
        logf("framebuffer creation failed (%ux%u face=%u level=%u)", color->width, color->height, face, level);
    framebuffers[key] = handle;
    return handle;
}

void Device::purge_framebuffers(bgfx::TextureHandle texture)
{
    for (auto it = framebuffers.begin(); it != framebuffers.end();)
    {
        if (it->first.color == texture.idx || it->first.depth == texture.idx)
        {
            if (bgfx::isValid(it->second))
                bgfx::destroy(it->second);
            it = framebuffers.erase(it);
        }
        else
            ++it;
    }
}

uint16_t Device::new_view()
{
    // A never-written target is cleared in views ordered before the one that renders into it.
    rt->store->sync();
    clear_if_new(rt->store.get());
    if (views_used >= render::kGameViewEnd - render::kGameViewBegin)
    {
        if (!view_overflow_logged)
        {
            logf("ERROR: view budget exhausted (%u); later passes of this frame reuse the last view",
                 render::kGameViewEnd - render::kGameViewBegin);
            view_overflow_logged = true;
        }
        view_dirty = false;
        return view;
    }
    view = render::kGameViewBegin + views_used++;
    ++perf().views;
    view_dirty = false;
    view_has_content = false;
    TexStore *color = rt->store.get();
    // A depth buffer smaller than the colour target cannot be attached.
    TexStore *depth = ds ? ds->store.get() : nullptr;
    if (depth && (depth->width < color->level_width(rt->level) || depth->height < color->level_height(rt->level)))
        depth = nullptr;
    // Attachments must agree in sample count: other targets drawn with the multisampled scene depth get a
    // single-sample depth of the same size.
    if (depth && (depth->target_flags & BGFX_TEXTURE_RT_MSAA_MASK) != (color->target_flags & BGFX_TEXTURE_RT_MSAA_MASK))
    {
        if (single_depth && depth == back_depth->store.get() && !color->target_flags)
            depth = single_depth.get();
        else
        {
            logf("ERROR: render target and depth differ in MSAA samples; drawn without depth");
            depth = nullptr;
        }
    }
    bgfx::setViewFrameBuffer(view, framebuffer(color, rt->face, rt->level, depth));
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    // The display-size depth back from parking (render scale) holds an old frame: cleared in the first view that
    // uses it, in the same pass (a tile GPU loads nothing).
    view_clear_bits = BGFX_CLEAR_NONE;
    if (held_depth_clear && depth && depth == back_depth->store.get())
    {
        view_clear_bits = BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL;
        held_depth_clear = false;
    }
    bgfx::setViewClear(view, view_clear_bits, 0, 1.0f, 0);
    view_clear_set = view_clear_bits;
    view_clear_rgba = 0;
    view_clear_z = 1.0f;
    view_clear_stencil = 0;
    view_color = color;
    // The game addresses its viewport in display pixels; a scaled scene store has fewer.
    const uint32_t target_height = color->level_height(rt->level);
    const std::array<float, 2> scale = target_scale();
    const D3DVIEWPORT9 &vp = s.viewport;
    const auto x0 = static_cast<uint32_t>(std::lround(vp.X * scale[0]));
    const auto y0 = static_cast<uint32_t>(std::lround(vp.Y * scale[1]));
    const auto w = static_cast<uint32_t>(std::lround((vp.X + vp.Width) * scale[0])) - x0;
    const auto h = static_cast<uint32_t>(std::lround((vp.Y + vp.Height) * scale[1])) - y0;
    const uint32_t y = Context::instance().flip_y() ? target_height - std::min<uint32_t>(target_height, y0 + h) : y0;
    bgfx::setViewRect(view, static_cast<int16_t>(x0), static_cast<int16_t>(y), static_cast<uint16_t>(w),
                      static_cast<uint16_t>(h), vp.MinZ, vp.MaxZ);
    return view;
}

uint16_t Device::ensure_view()
{
    Context::instance().begin_frame_if_needed();
    if (view_dirty || views_used == 0)
        new_view();
    return view;
}

HRESULT Device::Clear(DWORD count, const D3DRECT *, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    if (count != 0)
        return trap("Device::Clear with rectangles is not supported"); // nothing is cleared
    if (!Context::instance().ready())
        return D3D_OK;
    Context::instance().begin_frame_if_needed();
    // bgfx clears at the start of a view, so a clear after draws opens a new one.
    if (view_dirty || views_used == 0 || view_has_content)
        new_view();
    uint16_t bits = 0;
    if (flags & D3DCLEAR_TARGET)
        bits |= BGFX_CLEAR_COLOR;
    if ((flags & D3DCLEAR_ZBUFFER) && ds)
        bits |= BGFX_CLEAR_DEPTH;
    if ((flags & D3DCLEAR_STENCIL) && ds)
        bits |= BGFX_CLEAR_STENCIL;
    if (!view_has_content)
        bits |= view_clear_bits; // the view's own depth clear (new_view) stays
    const uint32_t rgba = ((color & 0x00FFFFFF) << 8) | (color >> 24);
    bgfx::setViewClear(view, bits, rgba, z, static_cast<uint8_t>(stencil));
    view_clear_set = bits;
    view_clear_rgba = rgba;
    view_clear_z = z;
    view_clear_stencil = static_cast<uint8_t>(stencil);
    bgfx::touch(view);
    view_has_content = true;
    return D3D_OK;
}

HRESULT Device::SetRenderTarget(DWORD index, Surface *surface)
{
    if (index != 0 || !surface)
        return D3DERR_INVALIDCALL;
    auto *next = static_cast<Surface *>(surface);
    if (next != rt)
    {
        assign_ref(rt, next);
        view_dirty = true;
    }
    // D3D resets the viewport to the full target (in the pixels the game addresses).
    s.viewport = {0, 0, rt->store->addressed_width(rt->level), rt->store->addressed_height(rt->level), 0.0f, 1.0f};
    s.scissor = {0, 0, static_cast<LONG>(s.viewport.Width), static_cast<LONG>(s.viewport.Height)};
    view_dirty = true;
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(DWORD index, Surface **surface)
{
    if (index != 0)
        return D3DERR_NOTFOUND;
    *surface = rt;
    rt->AddRef();
    return D3D_OK;
}

HRESULT Device::SetDepthStencilSurface(Surface *surface)
{
    auto *next = static_cast<Surface *>(surface);
    if (next != ds)
    {
        assign_ref(ds, next);
        view_dirty = true;
    }
    return D3D_OK;
}

HRESULT Device::GetDepthStencilSurface(Surface **surface)
{
    *surface = ds;
    if (!ds)
        return D3DERR_NOTFOUND;
    ds->AddRef();
    return D3D_OK;
}

HRESULT Device::SetViewport(const D3DVIEWPORT9 *viewport)
{
    if (memcmp(&s.viewport, viewport, sizeof(*viewport)) != 0)
    {
        s.viewport = *viewport;
        view_dirty = true;
    }
    return D3D_OK;
}

HRESULT Device::GetViewport(D3DVIEWPORT9 *viewport)
{
    *viewport = s.viewport;
    return D3D_OK;
}

// ---------------------------------------------------------------------------
// resources
HRESULT Device::CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                              Texture2D **texture, HANDLE *)
{
    *texture = new Texture2D(make_store(this, TexKind::Tex2D, width, height, 1, levels, usage, format, pool));
    return D3D_OK;
}

HRESULT Device::CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
                                    D3DPOOL pool, VolumeTexture **texture, HANDLE *)
{
    *texture = new VolumeTexture(make_store(this, TexKind::Volume, width, height, depth, levels, usage, format, pool));
    return D3D_OK;
}

HRESULT Device::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                  CubeTexture **texture, HANDLE *)
{
    *texture = new CubeTexture(make_store(this, TexKind::Cube, edge, edge, 1, levels, usage, format, pool));
    return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL, VertexBuffer **buffer,
                                   HANDLE *)
{
    auto *vb = new VertexBuffer();
    vb->device = this;
    vb->shadow.assign(length, 0);
    perf_resize(&render::PerfCounters::buffer_shadow_bytes, 0, length);
    vb->usage = usage;
    vb->fvf = fvf;
    *buffer = vb;
    return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL, IndexBuffer **buffer,
                                  HANDLE *)
{
    auto *ib = new IndexBuffer();
    ib->device = this;
    ib->shadow.assign(length, 0);
    perf_resize(&render::PerfCounters::buffer_shadow_bytes, 0, length);
    ib->usage = usage;
    ib->index32 = format == D3DFMT_INDEX32;
    *buffer = ib;
    return D3D_OK;
}

HRESULT Device::CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                   Surface **surface, HANDLE *)
{
    *surface = new Surface(
        make_store(this, TexKind::Tex2D, width, height, 1, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT), 0, 0);
    return D3D_OK;
}

HRESULT Device::CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                          Surface **surface, HANDLE *)
{
    *surface = new Surface(
        make_store(this, TexKind::Tex2D, width, height, 1, 1, D3DUSAGE_DEPTHSTENCIL, format, D3DPOOL_DEFAULT), 0, 0);
    return D3D_OK;
}

HRESULT Device::CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                            Surface **surface, HANDLE *)
{
    auto store = make_store(this, TexKind::Tex2D, width, height, 1, 1, 0, format, pool);
    store->system_memory = true;
    *surface = new Surface(store, 0, 0);
    return D3D_OK;
}

HRESULT Device::UpdateSurface(Surface *src, const RECT *src_rect, Surface *dst,
                              const POINT *dst_point)
{
    auto *from = static_cast<Surface *>(src);
    auto *to = static_cast<Surface *>(dst);
    if (!from || !to || from->store->format != to->store->format || format_info(from->store->format).block_bytes)
        return trap("Device::UpdateSurface(format)");
    const uint32_t bpp = format_info(from->store->format).bytes_per_pixel;
    uint32_t src_pitch = 0, dst_pitch = 0;
    const uint8_t *source = from->store->lock(from->face, from->level, &src_pitch, nullptr, true);
    uint8_t *dest = to->store->lock(to->face, to->level, &dst_pitch, nullptr, false);
    const uint32_t sw = from->store->level_width(from->level), sh = from->store->level_height(from->level);
    const uint32_t dw = to->store->level_width(to->level), dh = to->store->level_height(to->level);
    const uint32_t sx = src_rect ? src_rect->left : 0, sy = src_rect ? src_rect->top : 0;
    const uint32_t w = src_rect ? src_rect->right - src_rect->left : sw;
    const uint32_t h = src_rect ? src_rect->bottom - src_rect->top : sh;
    const uint32_t dx = dst_point ? dst_point->x : 0, dy = dst_point ? dst_point->y : 0;
    if (sx + w > sw || sy + h > sh || dx + w > dw || dy + h > dh)
        return D3DERR_INVALIDCALL;
    for (uint32_t y = 0; y < h; ++y)
        memcpy(dest + static_cast<size_t>(dy + y) * dst_pitch + static_cast<size_t>(dx) * bpp,
               source + static_cast<size_t>(sy + y) * src_pitch + static_cast<size_t>(sx) * bpp,
               static_cast<size_t>(w) * bpp);
    to->store->unlock(to->face, to->level);
    return D3D_OK;
}

HRESULT Device::StretchRect(Surface *src, const RECT *src_rect, Surface *dst, const RECT *dst_rect,
                            D3DTEXTUREFILTERTYPE filter)
{
    auto *from = static_cast<Surface *>(src);
    auto *to = static_cast<Surface *>(dst);
    if (!from || !to)
        return D3DERR_INVALIDCALL;
    if (!Context::instance().ready())
        return D3D_OK;
    if (!from->store->render_target || to->store->system_memory || to->store->depth_stencil)
        return D3DERR_INVALIDCALL; // CPU-side copies go through the lock path of the caller
    if (!to->store->render_target)
    {
        // The engine copies render targets into ordinary textures (D3DX surface loads). Keep the
        // copy on the GPU: the destination becomes a render target and loses its CPU contents.
        TexStore &store = *to->store;
        if (format_info(store.format).block_bytes)
            return D3DERR_INVALIDCALL;
        if (bgfx::isValid(store.handle))
        {
            purge_framebuffers(store.handle);
            bgfx::destroy(store.handle);
            store.handle = BGFX_INVALID_HANDLE;
        }
        store.render_target = true;
        for (auto &sub : store.subs)
            sub.dirty = false;
        store.any_dirty = false;
    }
    from->store->sync();
    if (!bgfx::isValid(from->store->handle))
        return D3DERR_INVALIDCALL;
    // Temporarily route a dedicated view to the destination.
    Surface *saved_rt = rt;
    Surface *saved_ds = ds;
    const D3DVIEWPORT9 saved_viewport = s.viewport;
    rt = to;
    ds = nullptr;
    const uint32_t dw = to->store->addressed_width(to->level), dh = to->store->addressed_height(to->level);
    if (dst_rect)
        s.viewport = {static_cast<DWORD>(dst_rect->left), static_cast<DWORD>(dst_rect->top),
                      static_cast<DWORD>(dst_rect->right - dst_rect->left),
                      static_cast<DWORD>(dst_rect->bottom - dst_rect->top), 0.0f, 1.0f};
    else
        s.viewport = {0, 0, dw, dh, 0.0f, 1.0f};
    view_dirty = true;
    const uint16_t id = ensure_view();
    const float sw = static_cast<float>(from->store->addressed_width(from->level));
    const float sh = static_cast<float>(from->store->addressed_height(from->level));
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (src_rect)
    {
        u0 = src_rect->left / sw;
        u1 = src_rect->right / sw;
        v0 = src_rect->top / sh;
        v1 = src_rect->bottom / sh;
    }
    uint32_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    if (filter == D3DTEXF_NONE || filter == D3DTEXF_POINT)
        flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
    // In a flipped target the picture's top row is at clip y = -1.
    if (Context::instance().flip_y())
        std::swap(v0, v1);
    Context::instance().draw_fullscreen(id, from->store->handle, flags, BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, u0,
                                        v0, u1, v1);
    view_has_content = true;
    rt = saved_rt;
    ds = saved_ds;
    s.viewport = saved_viewport;
    view_dirty = true;
    return D3D_OK;
}

HRESULT Device::GetRenderTargetData(Surface *target, Surface *dest)
{
    auto *from = static_cast<Surface *>(target);
    auto *to = static_cast<Surface *>(dest);
    if (!from || !to || !from->store->render_target)
        return D3DERR_INVALIDCALL;
    if (!Context::instance().ready())
    {
        // No read-back without a surface to render on. The game's back buffer still holds the last frame presented:
        // its copy (copy_frame) stands in for it, logged; any other target keeps the caller's previous data.
        if (from == back_color && read_frame_copy(to))
            return D3D_OK;
        return D3DERR_DEVICELOST;
    }
    if (from->store->game_width)
    {
        // The scaled scene store: read at the size the game addresses (its GetDesc), scaled up like the post chain's
        // input would be.
        Surface resized(make_store(this, TexKind::Tex2D, from->store->game_width, from->store->game_height, 1, 1,
                                   D3DUSAGE_RENDERTARGET, from->store->format, D3DPOOL_DEFAULT),
                        0, 0);
        const HRESULT copied = StretchRect(from, nullptr, &resized, nullptr, D3DTEXF_LINEAR);
        return copied == D3D_OK ? GetRenderTargetData(&resized, dest) : copied;
    }
    from->store->sync();
    const uint32_t w = from->store->level_width(from->level), h = from->store->level_height(from->level);
    if (to->store->level_width(to->level) != w || to->store->level_height(to->level) != h)
        return D3DERR_INVALIDCALL;
    // Copy into a CPU-readable texture, execute the queued frame without presenting, then read.
    const bgfx::TextureHandle staging = bgfx::createTexture2D(
        static_cast<uint16_t>(w), static_cast<uint16_t>(h), false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(staging))
        return D3DERR_INVALIDCALL;
    view_dirty = true;
    const uint16_t id = ensure_view();
    bgfx::TextureRegion source, destination;
    source.init(from->store->handle);
    source.mip = static_cast<uint8_t>(from->level);
    source.z = static_cast<uint16_t>(from->face);
    destination.init(staging);
    bgfx::blit(id, destination, source);
    view_has_content = true;
    view_dirty = true;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    bgfx::TextureRegion read_region;
    read_region.init(staging);
    const uint32_t ready_frame = bgfx::read(read_region, pixels.data());
    uint32_t current = 0;
    // The render thread writes `pixels` while it executes the frame the read was recorded in; that frame is finished
    // when bgfx::frame() has returned the next number, and `ready_frame` is one later still. Flush frames present
    // nothing and are never refused (Context::run_frame), so `pixels` outlives the write.
    while (current < ready_frame)
        Context::instance().run_frame(BGFX_FRAME_FLUSH, &current);
    // A flush ends the bgfx frame: views and sticky uniforms start over.
    views_used = 0;
    view_has_content = false;
    invalidate_uniforms();
    bgfx::destroy(staging);
    uint32_t pitch = 0;
    uint8_t *out = to->store->lock(to->face, to->level, &pitch, nullptr, false);
    // RGBA8 into the surface's own texels (texture/core d3d_format): A8R8G8B8/X8R8G8B8 are BGRA8, R5G6B5 is B5G6R5.
    static bx::DefaultAllocator allocator;
    const auto texels = texture::d3d_format(static_cast<uint32_t>(to->store->format)).format;
    if (bimg::isCompressed(texels) || texels == bimg::TextureFormat::Unknown)
        trap("Device::GetRenderTargetData into an unsupported destination format (left unchanged)");
    else
        for (uint32_t y = 0; y < h; ++y) // row by row: the surface's pitch
            bimg::imageConvert(&allocator, out + static_cast<size_t>(y) * pitch, texels,
                               pixels.data() + static_cast<size_t>(y) * w * 4, bimg::TextureFormat::RGBA8, w, 1, 1);
    to->store->unlock(to->face, to->level);
    return D3D_OK;
}

namespace
{
constexpr uint32_t kFrameCopyWidth = 512;
constexpr auto kFrameCopySeconds = std::chrono::seconds(2);
} // namespace

void Device::copy_frame(const TexStorePtr &shown)
{
    auto &context = Context::instance();
    FrameCopy &copy = frame_copy;
    const auto now = std::chrono::steady_clock::now();
    if (copy.pending && context.last_frame() >= copy.ready_frame)
    {
        copy.pixels.swap(copy.reading); // the render thread wrote `reading` in a frame that has finished
        copy.pending = false;
        copy.finished = copy.taken;
    }
    if (copy.pending || (copy.taken != std::chrono::steady_clock::time_point{} && now - copy.taken < kFrameCopySeconds))
        return;
    if (!shown || !bgfx::isValid(shown->handle) || shown->width == 0 || shown->height == 0)
        return;
    const uint32_t w = std::min(kFrameCopyWidth, shown->width);
    const uint32_t h = std::max<uint32_t>(1, w * shown->height / shown->width);
    if (!copy.target || copy.width != w || copy.height != h)
    {
        if (bgfx::isValid(copy.staging))
            bgfx::destroy(copy.staging);
        copy.target = make_store(this, TexKind::Tex2D, w, h, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
        copy.staging = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h), false, 1,
                                             bgfx::TextureFormat::RGBA8,
                                             BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_U_CLAMP |
                                                 BGFX_SAMPLER_V_CLAMP);
        copy.width = w;
        copy.height = h;
        copy.pixels.clear();
    }
    if (!bgfx::isValid(copy.staging))
        return;
    Surface from(shown, 0, 0), to(copy.target, 0, 0);
    if (StretchRect(&from, nullptr, &to, nullptr, D3DTEXF_LINEAR) != D3D_OK)
        return;
    copy.target->sync();
    view_dirty = true;
    const uint16_t id = ensure_view();
    bgfx::TextureRegion source, destination, read_region;
    source.init(copy.target->handle);
    destination.init(copy.staging);
    bgfx::blit(id, destination, source);
    view_has_content = true;
    view_dirty = true;
    copy.reading.resize(static_cast<size_t>(copy.width) * copy.height * 4);
    read_region.init(copy.staging);
    // Finished once bgfx::frame() has returned this number (see GetRenderTargetData); `reading` is not touched before.
    copy.ready_frame = bgfx::read(read_region, copy.reading.data());
    copy.pending = true;
    copy.taken = now;
}

bool Device::read_frame_copy(Surface *to)
{
    const FrameCopy &copy = frame_copy;
    if (copy.pixels.empty())
        return false;
    const uint32_t w = to->store->level_width(to->level), h = to->store->level_height(to->level);
    static bx::DefaultAllocator allocator;
    const auto texels = texture::d3d_format(static_cast<uint32_t>(to->store->format)).format;
    if (bimg::isCompressed(texels) || texels == bimg::TextureFormat::Unknown || w == 0 || h == 0)
        return false;
    const double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - copy.finished).count();
    logf("GetRenderTargetData: no surface (the app is in the background): the back buffer is read from the copy of "
         "the frame presented %.1f s ago (%ux%u for %ux%u)",
         age, copy.width, copy.height, w, h);
    uint32_t pitch = 0;
    uint8_t *out = to->store->lock(to->face, to->level, &pitch, nullptr, false);
    std::vector<uint8_t> row(static_cast<size_t>(w) * 4);
    for (uint32_t y = 0; y < h; ++y)
    {
        // The copy's rows top first, like the read-back's (GetRenderTargetData): nearest texel
        const uint8_t *src = copy.pixels.data() + static_cast<size_t>(y * copy.height / h) * copy.width * 4;
        for (uint32_t x = 0; x < w; ++x)
            std::memcpy(&row[x * 4], src + static_cast<size_t>(x * copy.width / w) * 4, 4);
        bimg::imageConvert(&allocator, out + static_cast<size_t>(y) * pitch, texels, row.data(),
                           bimg::TextureFormat::RGBA8, w, 1, 1);
    }
    to->store->unlock(to->face, to->level);
    return true;
}

// ---------------------------------------------------------------------------
// simple state
HRESULT Device::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    if (state == D3DTS_VIEW)
        s.view = *matrix;
    else if (state == D3DTS_PROJECTION)
        s.proj = *matrix;
    else if (state == D3DTS_WORLD)
        s.world = *matrix;
    else if (state >= D3DTS_TEXTURE0 && state <= D3DTS_TEXTURE7)
        s.texture[state - D3DTS_TEXTURE0] = *matrix;
    else
        return trap("Device::SetTransform(state)");
    return D3D_OK;
}

HRESULT Device::GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    if (state == D3DTS_VIEW)
        *matrix = s.view;
    else if (state == D3DTS_PROJECTION)
        *matrix = s.proj;
    else if (state == D3DTS_WORLD)
        *matrix = s.world;
    else if (state >= D3DTS_TEXTURE0 && state <= D3DTS_TEXTURE7)
        *matrix = s.texture[state - D3DTS_TEXTURE0];
    else
        return trap("Device::GetTransform(state)");
    return D3D_OK;
}

HRESULT Device::SetMaterial(const D3DMATERIAL9 *material)
{
    s.material = *material;
    return D3D_OK;
}
HRESULT Device::GetMaterial(D3DMATERIAL9 *material)
{
    *material = s.material;
    return D3D_OK;
}

HRESULT Device::SetLight(DWORD index, const D3DLIGHT9 *light)
{
    if (index >= 256)
        return D3DERR_INVALIDCALL;
    if (index >= lights.size())
        lights.resize(index + 1);
    lights[index].light = *light;
    return D3D_OK;
}
HRESULT Device::GetLight(DWORD index, D3DLIGHT9 *light)
{
    if (index >= lights.size())
        return D3DERR_INVALIDCALL;
    *light = lights[index].light;
    return D3D_OK;
}
HRESULT Device::LightEnable(DWORD index, BOOL enable)
{
    if (index >= 256)
        return D3DERR_INVALIDCALL;
    if (index >= lights.size())
        lights.resize(index + 1);
    lights[index].enabled = enable != 0;
    return D3D_OK;
}
HRESULT Device::GetLightEnable(DWORD index, BOOL *enable)
{
    if (index >= lights.size())
        return D3DERR_INVALIDCALL;
    *enable = lights[index].enabled;
    return D3D_OK;
}

HRESULT Device::SetClipPlane(DWORD index, const float *plane)
{
    if (index >= 6)
        return D3DERR_INVALIDCALL;
    memcpy(s.clip_plane[index], plane, sizeof(float) * 4);
    return D3D_OK;
}

HRESULT Device::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    if (static_cast<uint32_t>(state) >= kRenderStateCount)
        return D3DERR_INVALIDCALL;
    s.rs[state] = value;
    return D3D_OK;
}
HRESULT Device::GetRenderState(D3DRENDERSTATETYPE state, DWORD *value)
{
    if (static_cast<uint32_t>(state) >= kRenderStateCount)
        return D3DERR_INVALIDCALL;
    *value = s.rs[state];
    return D3D_OK;
}

HRESULT Device::GetTexture(DWORD stage, BaseTexture **texture)
{
    if (stage >= kMaxStages)
        return D3DERR_INVALIDCALL;
    *texture = s.textures[stage];
    if (*texture)
        (*texture)->AddRef();
    return D3D_OK;
}
HRESULT Device::SetTexture(DWORD stage, BaseTexture *texture)
{
    if (stage == D3DVERTEXTEXTURESAMPLER0)
    {
        assign_ref(s.vertex_texture, texture);
        return D3D_OK;
    }
    if (stage >= kMaxStages)
        return D3DERR_INVALIDCALL;
    assign_ref(s.textures[stage], texture);
    return D3D_OK;
}
HRESULT Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD *value)
{
    if (stage >= kMaxStages || static_cast<uint32_t>(type) >= 33)
        return D3DERR_INVALIDCALL;
    *value = s.tss[stage][type];
    return D3D_OK;
}
HRESULT Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    if (stage >= kMaxStages || static_cast<uint32_t>(type) >= 33)
        return D3DERR_INVALIDCALL;
    s.tss[stage][type] = value;
    return D3D_OK;
}
HRESULT Device::GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD *value)
{
    if (sampler >= kMaxStages || static_cast<uint32_t>(type) >= 14)
        return D3DERR_INVALIDCALL;
    *value = s.sampler[sampler][type];
    return D3D_OK;
}
HRESULT Device::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    if (sampler >= kMaxStages || static_cast<uint32_t>(type) >= 14)
        return D3DERR_INVALIDCALL;
    s.sampler[sampler][type] = value;
    return D3D_OK;
}
HRESULT Device::SetScissorRect(const RECT *rect)
{
    s.scissor = *rect;
    return D3D_OK;
}
HRESULT Device::GetScissorRect(RECT *rect)
{
    *rect = s.scissor;
    return D3D_OK;
}

HRESULT Device::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, VertexDeclaration **decl)
{
    auto *result = new VertexDeclaration();
    for (; elements->Stream != 0xFF; ++elements)
        result->elements.push_back(*elements);
    *decl = result;
    return D3D_OK;
}
HRESULT Device::SetVertexDeclaration(VertexDeclaration *decl)
{
    assign_ref(s.decl, static_cast<VertexDeclaration *>(decl));
    return D3D_OK;
}
HRESULT Device::GetVertexDeclaration(VertexDeclaration **decl)
{
    *decl = s.decl;
    if (s.decl)
        s.decl->AddRef();
    return D3D_OK;
}
HRESULT Device::SetFVF(DWORD fvf)
{
    s.fvf = fvf;
    if (fvf != 0)
        assign_ref(s.decl, static_cast<VertexDeclaration *>(nullptr));
    return D3D_OK;
}
HRESULT Device::GetFVF(DWORD *fvf)
{
    *fvf = s.decl ? 0 : s.fvf;
    return D3D_OK;
}

HRESULT Device::SetStreamSource(UINT stream, VertexBuffer *buffer, UINT offset, UINT stride)
{
    if (stream >= kMaxStreams)
        return trap("Device::SetStreamSource(stream>=4)");
    assign_ref(s.stream[stream], static_cast<VertexBuffer *>(buffer));
    s.stream_offset[stream] = offset;
    s.stream_stride[stream] = stride;
    return D3D_OK;
}
HRESULT Device::GetStreamSource(UINT stream, VertexBuffer **buffer, UINT *offset, UINT *stride)
{
    if (stream >= kMaxStreams)
        return D3DERR_INVALIDCALL;
    *buffer = s.stream[stream];
    if (*buffer)
        (*buffer)->AddRef();
    if (offset)
        *offset = s.stream_offset[stream];
    if (stride)
        *stride = s.stream_stride[stream];
    return D3D_OK;
}
HRESULT Device::SetIndices(IndexBuffer *buffer)
{
    assign_ref(s.indices, static_cast<IndexBuffer *>(buffer));
    return D3D_OK;
}
HRESULT Device::GetIndices(IndexBuffer **buffer)
{
    *buffer = s.indices;
    if (s.indices)
        s.indices->AddRef();
    return D3D_OK;
}

HRESULT Device::SetVertexShaderConstantF(UINT start, const float *data, UINT count)
{
    if (start + count > kVsConstants)
        return trap("Device::SetVertexShaderConstantF(range)");
    memcpy(s.vs_const[start], data, sizeof(float) * 4 * count);
    return D3D_OK;
}
HRESULT Device::GetVertexShaderConstantF(UINT start, float *data, UINT count)
{
    if (start + count > kVsConstants)
        return D3DERR_INVALIDCALL;
    memcpy(data, s.vs_const[start], sizeof(float) * 4 * count);
    return D3D_OK;
}
HRESULT Device::SetPixelShaderConstantF(UINT start, const float *data, UINT count)
{
    if (start + count > kPsConstants)
        return trap("Device::SetPixelShaderConstantF(range)");
    memcpy(s.ps_const[start], data, sizeof(float) * 4 * count);
    return D3D_OK;
}
HRESULT Device::GetPixelShaderConstantF(UINT start, float *data, UINT count)
{
    if (start + count > kPsConstants)
        return D3DERR_INVALIDCALL;
    memcpy(data, s.ps_const[start], sizeof(float) * 4 * count);
    return D3D_OK;
}

// ---------------------------------------------------------------------------
// state blocks
StateBlock::StateBlock(Device *d) : device(d)
{
    Capture();
}
StateBlock::~StateBlock()
{
    for (auto *texture : state.textures)
        if (texture)
            texture->Release();
    for (auto *buffer : state.stream)
        if (buffer)
            buffer->Release();
    if (state.indices)
        state.indices->Release();
    if (state.decl)
        state.decl->Release();
    if (state.vs)
        state.vs->Release();
    if (state.ps)
        state.ps->Release();
}
HRESULT StateBlock::Capture()
{
    const DeviceState &current = device->s;
    for (uint32_t i = 0; i < kMaxStages; ++i)
        assign_ref(state.textures[i], current.textures[i]);
    for (uint32_t i = 0; i < kMaxStreams; ++i)
        assign_ref(state.stream[i], current.stream[i]);
    assign_ref(state.indices, current.indices);
    assign_ref(state.decl, current.decl);
    assign_ref(state.vs, current.vs);
    assign_ref(state.ps, current.ps);
    // The raw copy below must not disturb the references taken above.
    DeviceState copy = current;
    std::copy(std::begin(state.textures), std::end(state.textures), std::begin(copy.textures));
    state = copy;
    lights = device->lights;
    return D3D_OK;
}
HRESULT StateBlock::Apply()
{
    device->bind_state_refs(state);
    DeviceState copy = state;
    const DeviceState &held = device->s;
    std::copy(std::begin(held.textures), std::end(held.textures), std::begin(copy.textures));
    const bool viewport_changed = memcmp(&device->s.viewport, &state.viewport, sizeof(D3DVIEWPORT9)) != 0;
    device->s = copy;
    device->lights = lights;
    if (viewport_changed)
        device->view_dirty = true;
    return D3D_OK;
}
HRESULT Device::CreateStateBlock(D3DSTATEBLOCKTYPE, StateBlock **block)
{
    *block = new StateBlock(this);
    return D3D_OK;
}

} // namespace storm::shim
