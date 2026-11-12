#include "effects/lighting.hpp"

#include "shim/device.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::effects
{

namespace
{

fx::Vec3 rgb(const D3DCOLORVALUE &c)
{
    return {c.r, c.g, c.b};
}

fx::PointLight point_light(const D3DLIGHT9 &l)
{
    fx::PointLight p;
    p.position = {l.Position.x, l.Position.y, l.Position.z};
    p.color = rgb(l.Diffuse);
    p.range = l.Range;
    p.att0 = l.Attenuation0;
    p.att1 = l.Attenuation1;
    p.att2 = l.Attenuation2;
    if (l.Type == D3DLIGHT_SPOT)
    {
        const float length = std::sqrt(l.Direction.x * l.Direction.x + l.Direction.y * l.Direction.y +
                                       l.Direction.z * l.Direction.z);
        if (length > 0.0f)
            p.direction = {l.Direction.x / length, l.Direction.y / length, l.Direction.z / length};
        p.cos_inner = std::cos(l.Theta * 0.5f);
        p.cos_outer = std::cos(l.Phi * 0.5f);
        p.falloff = l.Falloff;
    }
    return p;
}

// The per-draw budget (4 / 8) is a designed limit. Within it every light is taken (the shader applies range and
// cone); beyond it fx::SelectLights ranks them by their contribution at the draw's bounding sphere. `fade`: the
// weakest kept light fades out as the first left-out one catches up (lights that only exist per pixel, so a
// change of the chosen set is never a jump). `picked` marks the chosen entries of `lights`.
fx::LightSelection select(std::span<const fx::PointLight> lights, const fx::Vec3 &center, float radius, int budget,
                          bool fade, uint8_t *picked)
{
    fx::LightSelection selection;
    if (lights.size() <= static_cast<size_t>(budget))
    {
        for (const fx::PointLight &light : lights)
            selection.lights[selection.count++] = light;
        *picked = static_cast<uint8_t>((1u << lights.size()) - 1u);
        return selection;
    }
    selection = fx::SelectLights(lights, center, radius, budget, true, fade);
    *picked = 0;
    for (int i = 0; i < selection.count; ++i)
        for (size_t j = 0; j < lights.size(); ++j)
            if (!(*picked & (1u << j)) && memcmp(&lights[j], &selection.lights[i], sizeof(fx::PointLight)) == 0)
            {
                *picked |= static_cast<uint8_t>(1u << j);
                break;
            }
    return selection;
}
} // namespace

void Lighting::configure(const fx::Settings &settings, const fx::RendererState &state)
{
    settings_ = settings;
    flags_ = settings.lighting_flags;
    lighting_ = (flags_ & (fx::kLightingPerPixel | fx::kLightingPoint)) != 0;
    coverage_ = state.alpha_to_coverage;
    budget_ = fx::PointLightBudget(settings);
    shadows_.configure(settings);
    normal_maps_.configure(lighting_ && (flags_ & fx::kLightingNormalMap));
    find_shaders();
}

void Lighting::find_shaders()
{
    if (!vs_lit_)
    {
        vs_ff_ = shim::shader_id_by_name("vs_ff");
        vs_lit_ = shim::shader_id_by_name("vs_ff_lit");
        vs_lit_skin_ = shim::shader_id_by_name("vs_ff_lit_skin");
        fs_coverage_ = shim::shader_id_by_name("fs_ff_coverage");
        static const char *const baked[2][2] = {{"fs_ff_baked", "fs_ff_baked_l"}, {"fs_ff_baked_n", "fs_ff_baked_nl"}};
        for (int s = 0; s < 2; ++s)
            for (int n = 0; n < 2; ++n)
                for (int l = 0; l < 2; ++l)
                    for (int h = 0; h < 3; ++h)
                    {
                        // Suffix letters in the order of CMakeLists.txt: s, n, l, then h / o.
                        std::string name = "fs_ff_lit";
                        std::string suffix = std::string(s ? "s" : "") + (n ? "n" : "") + (l ? "l" : "") +
                                             (h == 1 ? "h" : h == 2 ? "o" : "");
                        if (!suffix.empty())
                            name += "_" + suffix;
                        fs_lit_[s][n][l][h] = shim::shader_id_by_name(name.c_str());
                    }
        for (int n = 0; n < 2; ++n)
            for (int l = 0; l < 2; ++l)
                fs_baked_[n][l] = shim::shader_id_by_name(baked[n][l]);
    }
}

bool Lighting::prepare_programs(shim::Device &device, const fx::Settings &settings, int &budget)
{
    find_shaders();
    // The choices of prepare() for these settings; the skinned twins are the shim's (draw.cpp skinned_variant).
    const uint32_t flags = settings.lighting_flags;
    const bool lighting = (flags & (fx::kLightingPerPixel | fx::kLightingPoint)) != 0;
    const bool per_pixel = (flags & fx::kLightingPerPixel) != 0;
    const int specular = per_pixel && (flags & fx::kLightingSpecular) ? 1 : 0;
    const int linear = (flags & fx::kLightingLinear) ? 1 : 0;
    const int relief = lighting && (flags & fx::kLightingNormalMap) ? 1 : 0;
    const int shadow = per_pixel && settings.shadows != fx::ShadowQuality::Off
                           ? (settings.shadows == fx::ShadowQuality::Soft ? 2 : 1)
                           : 0;
    if (fx::RendererStateFor(settings).alpha_to_coverage && !device.prepare_program(vs_ff_, fs_coverage_, budget))
        return false;
    if (lighting)
        for (int n = 0; n <= relief; ++n)
        {
            for (const int h : {0, shadow})
            {
                if (!device.prepare_program(vs_lit_, fs_lit_[specular][n][linear][h], budget))
                    return false;
                // Characters (the skinned draws) have no relief.
                if (n == 0 && !device.prepare_program(vs_lit_skin_, fs_lit_[specular][0][linear][h], budget))
                    return false;
            }
            if (!device.prepare_program(vs_lit_, fs_baked_[n][linear], budget))
                return false;
        }
    return !per_pixel || shadows_.prepare_programs(device, settings, budget);
}

void Lighting::set_technique(const char *name)
{
    technique_set_ = name && name[0];
    if (!technique_set_)
    {
        material_ = Material::Hull;
        return;
    }
    // CTechnique keeps block names in lower case.
    const auto starts = [name](const char *prefix) { return strncmp(name, prefix, strlen(prefix)) == 0; };
    if (starts("animation"))
        material_ = Material::Character;
    else if (starts("shipsail") || starts("shipflag"))
        material_ = Material::Sail;
    else
        material_ = Material::Stone;
}

void Lighting::prepare(const shim::Device &device, const shim::LayoutInfo &layout, uint32_t cube_variant,
                       const DrawSphere &sphere, FfDraw &draw)
{
    if (layout.rhw)
        return;
    const DWORD *rs = device.s.rs;
    const shim::TexStore *stage0 = shim::store_of(device.s.textures[0]);
    const shim::TexStore *target = device.rt ? device.rt->store.get() : nullptr;

    // ---- alpha to coverage ----
    const DWORD func = rs[D3DRS_ALPHAFUNC];
    const bool coverage = coverage_ && rs[D3DRS_ALPHATESTENABLE] && !rs[D3DRS_ALPHABLENDENABLE] &&
                          (func == D3DCMP_GREATER || func == D3DCMP_GREATEREQUAL) && target && target->target_flags != 0 &&
                          stage0 && stage0->kind == shim::TexKind::Tex2D && device.s.tss[0][D3DTSS_COLOROP] != D3DTOP_DISABLE;

    // ---- lighting ----
    const bool lit_state = rs[D3DRS_LIGHTING] != 0;
    const bool per_pixel = (flags_ & fx::kLightingPerPixel) != 0;
    bool lit = lighting_ && layout.has_normal && lit_state;
    // Baked colours: dynamic lights only (the lighter tool put the location's own lights into the colours).
    fx::LightSelection baked_lights;
    uint8_t unused = 0;
    bool baked = false;
    if (lighting_ && layout.has_normal && !lit_state && budget_ > 0 && !dynamic_lights_.empty())
    {
        // Ranked at the draw's own vertices: a torch lights the wall next to it wherever the camera is.
        if (sphere.valid)
        {
            baked_lights = select(dynamic_lights_, sphere.center, sphere.radius, budget_, true, &unused);
            baked = baked_lights.count > 0;
        }
        else if (!bounds_reported_)
        {
            bounds_reported_ = true;
            shim::logf("effects: a baked draw without vertex positions gets no dynamic lights");
        }
    }
    // Relief: stage 1 holds a height texture with a derived normal map (the game's 2x detail layer).
    bgfx::TextureHandle normal_map = BGFX_INVALID_HANDLE;
    shim::TexStore *stage1 = shim::store_of(device.s.textures[1]);
    if ((flags_ & fx::kLightingNormalMap) && lighting_ && layout.has_normal && stage1 &&
        device.s.tss[0][D3DTSS_COLOROP] != D3DTOP_DISABLE && device.s.tss[1][D3DTSS_COLOROP] != D3DTOP_DISABLE &&
        !(lit && material_ == Material::Character))
        normal_map = normal_maps_.handle(*stage1, fx_);
    baked |= !lit_state && bgfx::isValid(normal_map);
    // The interior's AO volume darkens what is drawn into the game picture: baked location geometry (which then takes
    // the baked variant) and lit models (characters, props). Reflections and other targets keep their look.
    const shim::TexStore *picture = device.back_color ? device.back_color->store.get() : nullptr;
    const bool ao = ao_volume_ && lighting_ && layout.has_normal && target && target == picture;
    baked |= !lit_state && ao;
    if (!lit && !baked && !coverage)
        return;
    if (cube_variant != 0)
    {
        if (!cube_reported_)
            shim::logf("effects: cube-mapped fixed-function draws have no lighting / coverage variant; they keep "
                       "vertex lighting and the alpha test");
        cube_reported_ = true;
        return;
    }

    draw.active = true;
    draw.coverage = {stage0 ? static_cast<float>(stage0->width) : 1.0f, stage0 ? static_cast<float>(stage0->height) : 1.0f,
                     coverage ? 1.0f : 0.0f, 0.0f};
    if (coverage)
    {
        draw.state = BGFX_STATE_BLEND_ALPHA_TO_COVERAGE;
        ++frame_.coverage;
    }
    const bool linear = (flags_ & fx::kLightingLinear) != 0;
    if (!lit && !baked)
    {
        draw.vs_id = vs_ff_;
        draw.fs_id = fs_coverage_;
        return;
    }
    draw.lit = true;
    draw.vs_id = vs_lit_;
    if (ao)
    {
        draw.ao = true;
        memcpy(draw.view.data(), &device.s.view.m[0][0], sizeof(float) * 16);
        ++frame_.ao;
    }


    // Sun and ambient as the fixed-function pipeline would use them; ComputeLighting gives the per-pixel terms.
    fx::LightingInput input;
    memcpy(input.view.data(), &device.s.view.m[0][0], sizeof(float) * 16);
    D3DCOLORVALUE ambient;
    {
        const DWORD c = rs[D3DRS_AMBIENT];
        ambient = {((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, 1.0f};
    }
    input.ambient = rgb(ambient);
    fx::PointLight points[shim::kMaxLights];
    uint8_t point_slot[shim::kMaxLights];
    size_t point_count = 0;
    int sun_slot = -1;
    uint32_t slot = 0;
    for (const shim::Light &entry : device.lights)
    {
        if (!entry.enabled || slot >= shim::kMaxLights)
            continue;
        const D3DLIGHT9 &l = entry.light;
        if (l.Type == D3DLIGHT_DIRECTIONAL)
        {
            if (sun_slot < 0)
            {
                sun_slot = static_cast<int>(slot);
                const float length = std::sqrt(l.Direction.x * l.Direction.x + l.Direction.y * l.Direction.y +
                                               l.Direction.z * l.Direction.z);
                if (length > 0.0f)
                    input.sun_dir = {-l.Direction.x / length, -l.Direction.y / length, -l.Direction.z / length};
                input.sun_diffuse = rgb(l.Diffuse);
            }
        }
        else
        {
            points[point_count] = point_light(l);
            point_slot[point_count++] = static_cast<uint8_t>(slot);
        }
        ++slot;
    }

    fx::LightingMaterial material = fx::LightingMaterial::Stone();
    bool two_sided = false;
    if (lit)
        switch (material_)
        {
        case Material::Hull:
            material = fx::LightingMaterial::Hull();
            break;
        case Material::Sail:
            material = fx::LightingMaterial::Sail();
            two_sided = rs[D3DRS_CULLMODE] == D3DCULL_NONE;
            break;
        case Material::Character:
            material = fx::LightingMaterial::Character();
            break;
        case Material::Stone:
            break;
        }
    draw.light = fx::ComputeLighting(input, material, sky_, settings_);
    auto &u = draw.light.u_fxLight;
    const bool hemisphere = per_pixel && (flags_ & fx::kLightingHemisphere);
    const bool specular = per_pixel && (flags_ & fx::kLightingSpecular);
    if (!per_pixel || sun_slot < 0 || !lit)
        u[1][0] = u[1][1] = u[1][2] = 0.0f; // the sun stays in the vertex sum (or there is none)
    else
    {
        draw.vertex_diffuse_off |= static_cast<uint8_t>(1u << sun_slot);
        if (specular)
            draw.vertex_specular_off |= static_cast<uint8_t>(1u << sun_slot);
    }
    if (hemisphere && lit)
        draw.vertex_ambient_off = true;
    else
        for (int i = 0; i < 3; ++i)
            u[3][i] = u[4][i] = 0.0f;
    if (!per_pixel || !(flags_ & fx::kLightingTranslucent))
        u[2][3] = 0.0f;
    if (!per_pixel || !(flags_ & fx::kLightingRim))
        u[5][1] = 0.0f;
    draw.ff = {two_sided && u[2][3] > 0.0f ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    const bool relief = bgfx::isValid(normal_map) && u[5][0] > 0.0f;
    if (relief)
    {
        draw.normal_map = normal_map;
        draw.normal_map_flags = device.sampler_flags(1, *stage1);
        ++frame_.relief;
    }

    if (lit)
    {
        fx::LightSelection selection;
        if (budget_ > 0 && point_count > 0)
        {
            uint8_t picked = 0;
            // Lights left out stay in the vertex sum, so the choice is not faded; it depends only on where the
            // draw's vertices and the lights are. Without positions the model origin stands for the draw.
            const D3DMATRIX &w = device.s.world;
            const fx::Vec3 center = sphere.valid ? sphere.center : fx::Vec3{w.m[3][0], w.m[3][1], w.m[3][2]};
            if (!sphere.valid && !bounds_reported_)
            {
                bounds_reported_ = true;
                shim::logf("effects: a lit draw without vertex positions: its lights are chosen at the model origin");
            }
            selection = select(std::span(points, point_count), center, sphere.valid ? sphere.radius : 0.0f, budget_, false,
                               &picked);
            for (size_t i = 0; i < point_count; ++i)
                if (picked & (1u << i))
                {
                    draw.vertex_diffuse_off |= static_cast<uint8_t>(1u << point_slot[i]);
                    if (specular)
                        draw.vertex_specular_off |= static_cast<uint8_t>(1u << point_slot[i]);
                }
            selection.dropped = 0; // the rest is lit per vertex
        }
        draw.points = fx::PackPointLights(selection, input.view);
        // Sun shadows: lit draws into the game picture (the first one of a frame prepares the map with its camera).
        if (per_pixel && sun_slot >= 0 && target && target == picture)
        {
            memcpy(draw.view.data(), &device.s.view.m[0][0], sizeof(float) * 16);
            draw.shadow = draw.caster = shadows_.prepare(draw.view);
        }
        const int shadow = draw.shadow ? (settings_.shadows == fx::ShadowQuality::Soft ? 2 : 1) : 0;
        draw.fs_id = fs_lit_[specular][relief][linear][shadow];
        frame_.shadowed += draw.shadow ? 1 : 0;
        ++frame_.lit;
    }
    else
    {
        draw.points = fx::PackPointLights(baked_lights, input.view);
        draw.fs_id = fs_baked_[relief][linear];
        ++frame_.baked;
    }
}

void Lighting::submit(const FfDraw &draw)
{
    if (!bgfx::isValid(u_coverage_))
    {
        u_coverage_ = bgfx::createUniform("u_fxCoverage", bgfx::UniformType::Vec4);
        u_ff_ = bgfx::createUniform("u_fxFf", bgfx::UniformType::Vec4);
    }
    bgfx::setUniform(u_coverage_, draw.coverage.data());
    if (!draw.lit)
        return;
    bgfx::setUniform(u_ff_, draw.ff.data());
    fx_.Apply(draw.light);
    fx_.Apply(draw.points);
    if (bgfx::isValid(draw.normal_map))
        fx_.BindNormalMap(draw.normal_map, draw.normal_map_flags);
    if (draw.shadow)
        shadows_.bind(draw.view);
    bind_ao_volume(draw);
}

void Lighting::bind_ao_volume(const FfDraw &draw)
{
    if (!bgfx::isValid(u_ao_params_))
    {
        s_ao_volume_ = bgfx::createUniform("s_fxAoVolume", bgfx::UniformType::Sampler);
        u_ao_matrix_ = bgfx::createUniform("u_fxAoVolume", bgfx::UniformType::Mat4);
        u_ao_params_ = bgfx::createUniform("u_fxAoParams", bgfx::UniformType::Vec4);
    }
    // Every lit / baked program reads u_fxAoParams: x = 0 turns the volume off for draws without it. Its 3D sampler
    // still needs a 3D texture on its unit (GL refuses a draw whose sampler types share a unit: an unbound sampler
    // stays on unit 0, the 2D stage 0), so those draws get a 1x1x1 open volume.
    if (!draw.ao || !ao_volume_)
    {
        if (!bgfx::isValid(ao_open_))
        {
            const uint8_t open = 255;
            ao_open_ = bgfx::createTexture3D(1, 1, 1, false, bgfx::TextureFormat::R8, BGFX_SAMPLER_POINT, bgfx::copy(&open, 1));
        }
        const float off[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_ao_params_, off);
        bgfx::setTexture(fx::kAoVolumeStage, s_ao_volume_, ao_open_);
        return;
    }
    if (draw.view != ao_view_)
    {
        float inverse_view[16];
        bx::mtxInverse(inverse_view, draw.view.data());
        bx::mtxMul(ao_view_to_uvw_.data(), inverse_view, ao_volume_->world_to_uvw.data());
        ao_view_ = draw.view;
    }
    const fx::AoVolumeLevel &level = fx::kAoVolumeLevel[std::min<uint8_t>(settings_.ao_volume, fx::kAoVolumeLevels)];
    const float params[4] = {level.strength, level.contrast, 0.5f * ao_volume_->cell, 0.0f};
    bgfx::setUniform(u_ao_matrix_, ao_view_to_uvw_.data());
    bgfx::setUniform(u_ao_params_, params);
    bgfx::setTexture(fx::kAoVolumeStage, s_ao_volume_, ao_volume_->texture);
}

bool Lighting::aerial_fog(const D3DMATRIX &view, bool vs_port, std::array<float, 2> &haze)
{
    if (!(flags_ & fx::kLightingAerial) || !sky_)
        return false;
    if (vs_port)
    {
        if (!aerial_port_reported_)
            shim::logf("effects: aerial perspective: draws with a ported vertex shader keep the fixed-function fog");
        aerial_port_reported_ = true;
        return false;
    }
    if (!bgfx::isValid(u_aerial_up_))
        u_aerial_up_ = bgfx::createUniform("u_fxAerialUp", bgfx::UniformType::Vec4);
    // World up in view space is the second row of the row-vector view matrix; the eye's height -t * R^T.
    const float up[4] = {view.m[1][0], view.m[1][1], view.m[1][2], 0.0f};
    const float eye_height = -(view.m[3][0] * view.m[1][0] + view.m[3][1] * view.m[1][1] + view.m[3][2] * view.m[1][2]);
    haze = fx::ComputeAerialFogHaze(eye_height);
    bgfx::setUniform(u_aerial_up_, up);
    ++frame_.aerial;
    return true;
}

void Lighting::end_frame()
{
    last_frame_ = frame_;
    frame_ = {};
    shadows_.end_frame();
    if (!dynamic_lights_this_frame_)
        dynamic_lights_.clear(); // no location reported them: none exist
    dynamic_lights_this_frame_ = false;
}

void Lighting::shutdown()
{
    fx_.Shutdown();
    shadows_.shutdown();
    if (bgfx::isValid(ao_open_))
        bgfx::destroy(ao_open_);
    ao_open_ = BGFX_INVALID_HANDLE;
    for (bgfx::UniformHandle *u : {&u_coverage_, &u_ff_, &u_aerial_up_, &s_ao_volume_, &u_ao_matrix_, &u_ao_params_})
    {
        if (bgfx::isValid(*u))
            bgfx::destroy(*u);
        *u = BGFX_INVALID_HANDLE;
    }
}

void Lighting::on_device_lost()
{
    fx_.OnDeviceLost();
    shadows_.on_device_lost();
    u_coverage_ = u_ff_ = u_aerial_up_ = s_ao_volume_ = u_ao_matrix_ = u_ao_params_ = BGFX_INVALID_HANDLE;
    ao_view_ = {};
    ao_open_ = BGFX_INVALID_HANDLE;
}

} // namespace storm::effects
