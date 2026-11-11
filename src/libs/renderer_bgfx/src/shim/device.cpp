#include "device.hpp"
#include "effects/effects.hpp"
#include "perf.hpp"
#include "util.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>

IDirect3D9 *WINAPI Direct3DCreate9(UINT)
{
    return new storm::shim::Direct3D();
}

namespace storm::shim
{


// ---------------------------------------------------------------------------
// Direct3D
HRESULT Direct3D::GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9 *identifier)
{
    *identifier = {};
    snprintf(identifier->Description, sizeof(identifier->Description), "bgfx");
    snprintf(identifier->DeviceName, sizeof(identifier->DeviceName), "storm-bgfx");
    snprintf(identifier->Driver, sizeof(identifier->Driver), "bgfx");
    return D3D_OK;
}

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
                               IDirect3DDevice9 **device)
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
    byte_layout.begin().add(bgfx::Attrib::TexCoord7, 1, bgfx::AttribType::Uint8).end();
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
    post_color.reset();
    single_depth.reset();
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
    // Effects: MSAA of the scene target (fx::RendererState). bgfx resolves colour and depth when the frame buffer
    // is left; the composite and the post chain read the resolved textures.
    const auto &host = effects::Host::instance();
    const auto &state = host.renderer_state();
    const uint64_t msaa = state.msaa_samples >= 4 ? BGFX_TEXTURE_RT_MSAA_X4
                          : state.msaa_samples == 2 ? BGFX_TEXTURE_RT_MSAA_X2
                                                    : 0;
    // A multisampled depth is resolved into a texture only when an effect reads it (ambient occlusion, the high
    // water's scene capture): otherwise it stays a write-only render buffer, so leaving the scene target resolves
    // colour alone (less bandwidth on tile GPUs, and no depth/stencil resolve blit, which drivers get wrong).
    const uint64_t depth_flags = msaa | (msaa && !host.scene_depth_read() ? BGFX_TEXTURE_RT_WRITE_ONLY : 0);
    // A settings switch rebuilds only the targets it changes (an anisotropy change none of them).
    const auto fits = [&](const std::shared_ptr<TexStore> &store, uint64_t flags) {
        return store && store->width == width && store->height == height && store->target_flags == flags;
    };
    const auto target = [&](D3DFORMAT format, DWORD usage, uint64_t flags) {
        auto store = make_store(this, TexKind::Tex2D, width, height, 1, 1, usage, format, D3DPOOL_DEFAULT);
        store->backbuffer = true;
        store->target_flags = flags;
        return store;
    };
    if (!fits(back_color->store, msaa))
        back_color->store = target(D3DFMT_A8R8G8B8, D3DUSAGE_RENDERTARGET, msaa);
    if (!fits(back_depth->store, depth_flags))
        back_depth->store = target(D3DFMT_D24S8, D3DUSAGE_DEPTHSTENCIL, depth_flags);
    if (!host.post_active())
        post_color = nullptr;
    else if (!fits(post_color, msaa))
        post_color = target(D3DFMT_A8R8G8B8, D3DUSAGE_RENDERTARGET, msaa);
    if (!msaa)
        single_depth = nullptr;
    else if (!fits(single_depth, 0))
        single_depth = make_store(this, TexKind::Tex2D, width, height, 1, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8,
                                  D3DPOOL_DEFAULT);
    world_anisotropy = state.max_anisotropy > 1;
    view_dirty = true;
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

HRESULT Device::GetDirect3D(IDirect3D9 **d3d)
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

// ---------------------------------------------------------------------------
// frame / views
HRESULT Device::BeginScene()
{
    Context::instance().begin_frame_if_needed();
    return D3D_OK;
}

HRESULT Device::Present(const RECT *, const RECT *, HWND, const RGNDATA *)
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
    const bool scene_frame = scene_drawn;
    scene_done = false;
    scene_drawn = false;
    interface_drawn = false;
    context.set_game_target({back_color->store->handle.idx, back_depth->store->handle.idx, back_color->store->width,
                             back_color->store->height});
    auto &effects = effects::Host::instance();
    if (context.ready())
        effects.advance_switch(); // a settings change: programs, self-test (views kPostViewBegin..), then the switch
    context.present(back_color->store->handle, back_color->store->width, back_color->store->height);
    effects.end_frame(scene_frame, context.last_frame());
    if (effects.take_renderer_state_change())
    {
        // Between frames: the scene target follows the new MSAA setting, samplers the new anisotropy.
        create_scene_stores(back_color->store->width, back_color->store->height);
        context.refresh_reset_flags();
    }
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
    if (!scene_drawn)
    {
        // Nothing 3D in the game picture yet: the interface and the 3D drawn after it cannot be separated, so this
        // frame is presented without the post chain.
        if (!interface_first_logged && effects::Host::instance().post_active())
            logf("effects: an interface was drawn before the 3D scene; such frames are presented without post effects");
        interface_first_logged = true;
        return;
    }
    scene_done = true;
    auto &host = effects::Host::instance();
    if (!host.post_active() || !Context::instance().ready())
        return;
    if (!post_color)
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
    post_color->sync();
    clear_if_new(post_color.get());
    const uint16_t budget = render::kGameViewEnd - render::kGameViewBegin;
    if (views_used + fx::PostChain::kMaxViews > budget)
    {
        if (!post_view_logged)
            logf("ERROR: effects: %u game views used, no room for the post chain (%u): frame presented without it",
                 views_used, fx::PostChain::kMaxViews);
        post_view_logged = true;
        return;
    }
    TexStore *scene = back_color->store.get();
    scene->sync();
    back_depth->store->sync();
    effects::Host::PostTargets targets;
    targets.scene_color = scene->handle;
    // A multisampled depth is resolved into the texture with the colour only when an effect reads it.
    if (!(back_depth->store->target_flags & BGFX_TEXTURE_RT_WRITE_ONLY))
        targets.scene_depth = back_depth->store->handle;
    targets.width = static_cast<uint16_t>(scene->width);
    targets.height = static_cast<uint16_t>(scene->height);
    targets.target = framebuffer(post_color.get(), 0, 0, nullptr);
    targets.first_view = static_cast<bgfx::ViewId>(render::kGameViewBegin + views_used);
    memcpy(targets.view.data(), &scene_view.m[0][0], sizeof(float) * 16);
    memcpy(targets.projection.data(), &scene_proj.m[0][0], sizeof(float) * 16);
    views_used += host.submit_post(targets);
    // The interface is drawn on the processed picture, which is then presented.
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

HRESULT Device::GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **surface)
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
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    const uint32_t target_height = color->level_height(rt->level);
    const D3DVIEWPORT9 &vp = s.viewport;
    const uint32_t y = Context::instance().flip_y() ? target_height - std::min<uint32_t>(target_height, vp.Y + vp.Height)
                                                    : vp.Y;
    bgfx::setViewRect(view, static_cast<int16_t>(vp.X), static_cast<int16_t>(y), static_cast<uint16_t>(vp.Width),
                      static_cast<uint16_t>(vp.Height), vp.MinZ, vp.MaxZ);
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
        return trap("IDirect3DDevice9::Clear with rectangles is not supported"); // nothing is cleared
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
    const uint32_t rgba = ((color & 0x00FFFFFF) << 8) | (color >> 24);
    bgfx::setViewClear(view, bits, rgba, z, static_cast<uint8_t>(stencil));
    bgfx::touch(view);
    view_has_content = true;
    return D3D_OK;
}

HRESULT Device::SetRenderTarget(DWORD index, IDirect3DSurface9 *surface)
{
    if (index != 0 || !surface)
        return D3DERR_INVALIDCALL;
    auto *next = static_cast<Surface *>(surface);
    if (next != rt)
    {
        assign_ref(rt, next);
        view_dirty = true;
    }
    // D3D resets the viewport to the full target.
    s.viewport = {0, 0, rt->store->level_width(rt->level), rt->store->level_height(rt->level), 0.0f, 1.0f};
    s.scissor = {0, 0, static_cast<LONG>(s.viewport.Width), static_cast<LONG>(s.viewport.Height)};
    view_dirty = true;
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(DWORD index, IDirect3DSurface9 **surface)
{
    if (index != 0)
        return D3DERR_NOTFOUND;
    *surface = rt;
    rt->AddRef();
    return D3D_OK;
}

HRESULT Device::SetDepthStencilSurface(IDirect3DSurface9 *surface)
{
    auto *next = static_cast<Surface *>(surface);
    if (next != ds)
    {
        assign_ref(ds, next);
        view_dirty = true;
    }
    return D3D_OK;
}

HRESULT Device::GetDepthStencilSurface(IDirect3DSurface9 **surface)
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
                              IDirect3DTexture9 **texture, HANDLE *)
{
    *texture = new Texture2D(make_store(this, TexKind::Tex2D, width, height, 1, levels, usage, format, pool));
    return D3D_OK;
}

HRESULT Device::CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
                                    D3DPOOL pool, IDirect3DVolumeTexture9 **texture, HANDLE *)
{
    *texture = new VolumeTexture(make_store(this, TexKind::Volume, width, height, depth, levels, usage, format, pool));
    return D3D_OK;
}

HRESULT Device::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                  IDirect3DCubeTexture9 **texture, HANDLE *)
{
    *texture = new CubeTexture(make_store(this, TexKind::Cube, edge, edge, 1, levels, usage, format, pool));
    return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL, IDirect3DVertexBuffer9 **buffer,
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

HRESULT Device::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL, IDirect3DIndexBuffer9 **buffer,
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
                                   IDirect3DSurface9 **surface, HANDLE *)
{
    *surface = new Surface(
        make_store(this, TexKind::Tex2D, width, height, 1, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT), 0, 0);
    return D3D_OK;
}

HRESULT Device::CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                          IDirect3DSurface9 **surface, HANDLE *)
{
    *surface = new Surface(
        make_store(this, TexKind::Tex2D, width, height, 1, 1, D3DUSAGE_DEPTHSTENCIL, format, D3DPOOL_DEFAULT), 0, 0);
    return D3D_OK;
}

HRESULT Device::CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                            IDirect3DSurface9 **surface, HANDLE *)
{
    auto store = make_store(this, TexKind::Tex2D, width, height, 1, 1, 0, format, pool);
    store->system_memory = true;
    *surface = new Surface(store, 0, 0);
    return D3D_OK;
}

HRESULT Device::UpdateSurface(IDirect3DSurface9 *src, const RECT *src_rect, IDirect3DSurface9 *dst,
                              const POINT *dst_point)
{
    auto *from = static_cast<Surface *>(src);
    auto *to = static_cast<Surface *>(dst);
    if (!from || !to || from->store->format != to->store->format || format_info(from->store->format).block_bytes)
        return trap("IDirect3DDevice9::UpdateSurface(format)");
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

HRESULT Device::StretchRect(IDirect3DSurface9 *src, const RECT *src_rect, IDirect3DSurface9 *dst, const RECT *dst_rect,
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
    const uint32_t dw = to->store->level_width(to->level), dh = to->store->level_height(to->level);
    if (dst_rect)
        s.viewport = {static_cast<DWORD>(dst_rect->left), static_cast<DWORD>(dst_rect->top),
                      static_cast<DWORD>(dst_rect->right - dst_rect->left),
                      static_cast<DWORD>(dst_rect->bottom - dst_rect->top), 0.0f, 1.0f};
    else
        s.viewport = {0, 0, dw, dh, 0.0f, 1.0f};
    view_dirty = true;
    const uint16_t id = ensure_view();
    const float sw = static_cast<float>(from->store->level_width(from->level));
    const float sh = static_cast<float>(from->store->level_height(from->level));
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

HRESULT Device::GetRenderTargetData(IDirect3DSurface9 *target, IDirect3DSurface9 *dest)
{
    auto *from = static_cast<Surface *>(target);
    auto *to = static_cast<Surface *>(dest);
    if (!from || !to || !from->store->render_target)
        return D3DERR_INVALIDCALL;
    if (!Context::instance().ready())
        return D3DERR_DEVICELOST; // no read-back without a surface (the caller keeps its previous data)
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
    for (int i = 0; i < 4 && current < ready_frame; ++i)
        if (!Context::instance().run_frame(BGFX_FRAME_FLUSH, &current))
            break; // the surface is going away: the copy keeps whatever was read so far
    // A flush ends the bgfx frame: views and sticky uniforms start over.
    views_used = 0;
    view_has_content = false;
    invalidate_uniforms();
    bgfx::destroy(staging);
    uint32_t pitch = 0;
    uint8_t *out = to->store->lock(to->face, to->level, &pitch, nullptr, false);
    const D3DFORMAT format = to->store->format;
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint8_t *row = pixels.data() + static_cast<size_t>(y) * w * 4;
        uint8_t *d = out + static_cast<size_t>(y) * pitch;
        if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8)
            for (uint32_t x = 0; x < w; ++x, row += 4, d += 4)
                d[0] = row[2], d[1] = row[1], d[2] = row[0], d[3] = row[3];
        else if (format == D3DFMT_R5G6B5)
            for (uint32_t x = 0; x < w; ++x, row += 4, d += 2)
            {
                const uint16_t v = static_cast<uint16_t>(((row[0] >> 3) << 11) | ((row[1] >> 2) << 5) | (row[2] >> 3));
                d[0] = static_cast<uint8_t>(v);
                d[1] = static_cast<uint8_t>(v >> 8);
            }
    }
    to->store->unlock(to->face, to->level);
    return D3D_OK;
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
        return trap("IDirect3DDevice9::SetTransform(state)");
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
        return trap("IDirect3DDevice9::GetTransform(state)");
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

HRESULT Device::GetTexture(DWORD stage, IDirect3DBaseTexture9 **texture)
{
    if (stage >= kMaxStages)
        return D3DERR_INVALIDCALL;
    *texture = s.textures[stage];
    if (*texture)
        (*texture)->AddRef();
    return D3D_OK;
}
HRESULT Device::SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture)
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

HRESULT Device::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **decl)
{
    auto *result = new VertexDeclaration();
    for (; elements->Stream != 0xFF; ++elements)
        result->elements.push_back(*elements);
    *decl = result;
    return D3D_OK;
}
HRESULT Device::SetVertexDeclaration(IDirect3DVertexDeclaration9 *decl)
{
    assign_ref(s.decl, static_cast<VertexDeclaration *>(decl));
    return D3D_OK;
}
HRESULT Device::GetVertexDeclaration(IDirect3DVertexDeclaration9 **decl)
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

HRESULT Device::SetStreamSource(UINT stream, IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride)
{
    if (stream >= kMaxStreams)
        return trap("IDirect3DDevice9::SetStreamSource(stream>=4)");
    assign_ref(s.stream[stream], static_cast<VertexBuffer *>(buffer));
    s.stream_offset[stream] = offset;
    s.stream_stride[stream] = stride;
    return D3D_OK;
}
HRESULT Device::GetStreamSource(UINT stream, IDirect3DVertexBuffer9 **buffer, UINT *offset, UINT *stride)
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
HRESULT Device::SetIndices(IDirect3DIndexBuffer9 *buffer)
{
    assign_ref(s.indices, static_cast<IndexBuffer *>(buffer));
    return D3D_OK;
}
HRESULT Device::GetIndices(IDirect3DIndexBuffer9 **buffer)
{
    *buffer = s.indices;
    if (s.indices)
        s.indices->AddRef();
    return D3D_OK;
}

HRESULT Device::SetVertexShaderConstantF(UINT start, const float *data, UINT count)
{
    if (start + count > kVsConstants)
        return trap("IDirect3DDevice9::SetVertexShaderConstantF(range)");
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
        return trap("IDirect3DDevice9::SetPixelShaderConstantF(range)");
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
HRESULT Device::CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9 **block)
{
    *block = new StateBlock(this);
    return D3D_OK;
}

} // namespace storm::shim
