#include "effects/lighting.hpp"

#include "shim/device.hpp"
#include "storm/fx/height_map.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::effects
{

namespace
{

// Absolute world -> view from the device's camera-relative world -> view (row vectors): (p - origin) * view.
std::array<float, 16> AbsoluteView(const std::array<float, 16> &view, const std::array<float, 3> &origin)
{
    std::array<float, 16> v = view;
    for (int j = 0; j < 3; ++j)
        v[12 + j] -= origin[0] * view[j] + origin[1] * view[4 + j] + origin[2] * view[8 + j];
    return v;
}

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
    // Our own lightmaps for the towns with the per-pixel lighting (Средняя / Высокая); Низкая keeps the game's.
    town_light_.set_enabled(settings.town_light);
    town_light_.set_debug(settings.town_light_debug);
    lamps_.configure(settings);
    gloss_ = (flags_ & fx::kLightingPerPixel) && (flags_ & fx::kLightingSpecular) && settings.gloss;
    // The relief's material texture (height and gloss) is uploaded when the parallax, the cavity or the gloss reads it.
    normal_maps_.configure(lighting_ && (flags_ & fx::kLightingNormalMap), settings.normal_map_size,
                           settings.parallax != fx::ParallaxQuality::Off || settings.relief_cavity || gloss_);
    normal_maps_.configure_gloss(gloss_, settings.gloss_map_size);
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
        // Suffix letters in the order of CMakeLists.txt: s, n, p, l, then h / o.
        static const char *const relief[3] = {"", "n", "np"};
        const auto variant = [](const char *base, const std::string &suffix) {
            return shim::shader_id_by_name((suffix.empty() ? std::string(base) : std::string(base) + "_" + suffix).c_str());
        };
        for (int s = 0; s < 2; ++s)
            for (int n = 0; n < 3; ++n)
                for (int l = 0; l < 2; ++l)
                    for (int h = 0; h < 3; ++h)
                    {
                        const std::string suffix = std::string(s ? "s" : "") + relief[n] + (l ? "l" : "") +
                                                   (h == 1 ? "h" : h == 2 ? "o" : "");
                        fs_lit_[s][n][l][h] = variant("fs_ff_lit", suffix);
                        fs_baked_[s][n][l][h] = variant("fs_ff_baked", suffix);
                    }
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
    // 1: the normal maps, 2: and the parallax variants (draws whose map has no height keep the plain relief ones).
    const bool height = settings.parallax != fx::ParallaxQuality::Off || settings.relief_cavity;
    const int relief = lighting && (flags & fx::kLightingNormalMap) ? (height ? 2 : 1) : 0;
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
            // The town's walls take the shadowed twin (settings.town_light); baked draws with a gloss map (or every one
            // under the gloss debug view) or wet in the rain the specular one.
            for (const int h : {0, settings.town_walls ? shadow : 0})
                for (int s = 0; s <= (specular && (settings.gloss || settings.wet_surfaces) ? 1 : 0); ++s)
                    if (!device.prepare_program(vs_lit_, fs_baked_[s][n][linear][h], budget))
                        return false;
        }
    if (per_pixel && settings.lamp_shadows && !casters_.prepare_programs(device, Casters::Pass::Lamp, budget))
        return false;
    return !per_pixel || settings.shadows == fx::ShadowQuality::Off ||
           casters_.prepare_programs(device, Casters::Pass::Sun, budget);
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
                       const DrawSphereSource &sphere_source, FfDraw &draw)
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
    // The room's lamps relight what is drawn into the game picture, each with its shadow (effects/lamp_shadows.hpp):
    // baked location geometry (which then takes the baked variant) and lit models. The first receiver of a frame
    // gives the lamps around its camera their slots.
    const shim::TexStore *picture = device.back_color ? device.back_color->store.get() : nullptr;
    bool lamp_on = false;
    if (lamps_.enabled() && lighting_ && layout.has_normal && target && target == picture)
    {
        const D3DMATRIX &v = device.s.view;
        std::array<float, 3> eye{}; // absolute, like the lamps
        for (int j = 0; j < 3; ++j)
            eye[j] = origin_[j] - (v.m[3][0] * v.m[j][0] + v.m[3][1] * v.m[j][1] + v.m[3][2] * v.m[j][2]);
        lamp_on = lamps_.prepare(const_cast<shim::Device &>(device), eye, casters_);
    }
    // Baked colours: dynamic lights only (the lighter tool put the location's own lights into the colours).
    fx::LightSelection baked_lights;
    uint8_t unused = 0;
    bool baked = !lit_state && lamp_on;
    if (lighting_ && layout.has_normal && !lit_state && budget_ > 0 && !dynamic_lights_.empty())
    {
        // The dynamic lights are absolute; the draw's sphere and view are in the device's camera-relative world.
        if (!device_lights_origin_valid_)
        {
            device_dynamic_lights_ = dynamic_lights_;
            for (fx::PointLight &l : device_dynamic_lights_)
                for (int k = 0; k < 3; ++k)
                    l.position[k] -= origin_[k];
            device_lights_origin_valid_ = true;
        }
        // The candidates of this draw: a carried lantern that has a lamp slot is lit by the lamps, with its shadow
        // (where the lamps light this draw), so it is not added a second time.
        candidate_lights_.clear();
        for (size_t i = 0; i < dynamic_lights_.size(); ++i)
            if (!lamp_on || !lamps_.relit(dynamic_lights_[i].position))
                candidate_lights_.push_back(device_dynamic_lights_[i]);
        // Ranked at the draw's own vertices: a torch lights the wall next to it wherever the camera is.
        if (const DrawSphere &sphere = sphere_source.get(); sphere.valid)
        {
            if (!candidate_lights_.empty())
                baked_lights = select(candidate_lights_, sphere.center, sphere.radius, budget_, true, &unused);
            baked |= baked_lights.count > 0;
        }
        else if (!bounds_reported_)
        {
            bounds_reported_ = true;
            shim::logf("effects: a baked draw without vertex positions gets no dynamic lights");
        }
    }
    // Relief: the normal map of stage 1's texture (a grey height texture: the game's 2x detail layer, on the second UV
    // set) or else of stage 0's (the colour texture of stone, wood, plaster, ground).
    bgfx::TextureHandle normal_map = BGFX_INVALID_HANDLE;
    float relief_fade = 0.0f; // a map just uploaded fades in (effects::kReliefFade)
    int relief_stage = -1;
    NormalMaps::Relief relief_map;
    if ((flags_ & fx::kLightingNormalMap) && lighting_ && layout.has_normal && device.s.tss[0][D3DTSS_COLOROP] != D3DTOP_DISABLE &&
        !(lit && material_ == Material::Character))
        for (const int stage : {1, 0})
        {
            shim::TexStore *store = shim::store_of(device.s.textures[stage]);
            if (!store || !store->normal_map || device.s.tss[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE)
                continue;
            relief_map = normal_maps_.handle(*store, fx_);
            draw.relief_entry = store->normal_map.get();
            normal_map = relief_map.handle;
            relief_fade = relief_map.fade;
            relief_stage = stage;
            break;
        }
    baked |= !lit_state && bgfx::isValid(normal_map);
    // Gloss: stage 0's colour texture's: the G of its relief's material texture when its relief class has a height
    // (relief_store.hpp), else its own gloss map (characters stay matte: no map, their material has no highlight).
    NormalMaps::Relief gloss_map;
    fx::GlossClass gloss_class = fx::GlossClass::None;
    bool metal_mask = false; // the material's R is a metal mask (a gloss map's), not a relief's height
    shim::TexStore *gloss_store = shim::store_of(device.s.textures[0]);
    if (gloss_ && layout.has_normal && gloss_store && device.s.tss[0][D3DTSS_COLOROP] != D3DTOP_DISABLE &&
        !(lit && material_ == Material::Character))
    {
        if (gloss_store->normal_map && gloss_store->normal_map->material_gloss != fx::GlossClass::None)
        {
            const NormalMaps::Relief material = relief_stage == 0 ? relief_map : normal_maps_.handle(*gloss_store, fx_);
            gloss_map = {material.material, material.fade};
            gloss_class = gloss_store->normal_map->material_gloss;
        }
        else if (gloss_store->gloss_map)
        {
            gloss_map = normal_maps_.gloss(*gloss_store, fx_);
            gloss_class = gloss_store->gloss_map->gloss;
            metal_mask = true;
        }
    }
    const bool gloss_debug = gloss_ && settings_.gloss_debug != 0;
    baked |= !lit_state && (bgfx::isValid(gloss_map.handle) || gloss_debug) && target && target == picture;
    // The interior's AO volume darkens what is drawn into the game picture: baked location geometry (which then takes
    // the baked variant) and lit models (characters, props). Reflections and other targets keep their look.
    const bool ao = ao_volume_ && lighting_ && layout.has_normal && target && target == picture;
    baked |= !lit_state && ao;
    // Under a location lightmap (ours in its place: the shim binds TownLight::replacement): no cascade, see below.
    bool lightmapped = false;
    for (int stage = 0; stage < 4 && !lightmapped; ++stage)
        if (device.s.tss[stage][D3DTSS_COLOROP] != D3DTOP_DISABLE)
            if (const shim::TexStore *store = shim::store_of(device.s.textures[stage]); store && store->lightmap)
                lightmapped = true;
    // A town's walls and props without a lightmap, drawn with the game's baked colours into the game picture: our
    // sun, sky and bounce relight them as our lightmaps relight the ground (town_light.hpp TownWallTone).
    const bool town_wall = per_pixel && lighting_ && layout.has_normal && !lit_state && !lightmapped && target &&
                           target == picture && !lamps_.interior() && settings_.town_walls && town_light_.walls_active();
    baked |= town_wall;
    // Rain: the outdoor surfaces drawn into the game picture are wet (characters and sails keep their look); baked
    // location geometry then takes the baked variant.
    const bool wet = settings_.wet_surfaces && wet_.level > 0.0f && lighting_ && layout.has_normal && target &&
                     target == picture && !lamps_.interior() &&
                     !(lit && (material_ == Material::Character || material_ == Material::Sail));
    baked |= !lit_state && wet;
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
        ++frame_.ao;
    }
    if (lamp_on)
    {
        draw.lamps = true;
        ++frame_.lamp_lit;
        // Lit draws are what is not in the still geometry (characters, props, a ship's hull, masts and sails): they
        // cast into the faces of the lamps their sphere reaches.
        if (lit && sphere_source.get().valid)
        {
            DrawSphere absolute = sphere_source.get(); // like the lamp faces
            for (int k = 0; k < 3; ++k)
                absolute.center[k] += origin_[k];
            if (lamps_.reaches(absolute.center, absolute.radius))
            {
                draw.lamp_caster = draw.caster = true;
                draw.sphere = absolute;
            }
        }
    }
    if (wet)
    {
        draw.wet = {{{wet_.level, settings_.wet_surfaces >= 2 ? 1.0f : 0.0f, 0.0f, 0.0f},
                     {wet_.sky[0], wet_.sky[1], wet_.sky[2], 0.0f}}};
        ++frame_.wet;
    }
    if (ao || lamp_on || visibility_ || wet)
    {
        memcpy(draw.view.data(), &device.s.view.m[0][0], sizeof(float) * 16);
        draw.origin = origin_;
        draw.world_view = AbsoluteView(draw.view, origin_);
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
        else if (lamp_on && l.Type == D3DLIGHT_POINT &&
                 lamps_.relit({l.Position.x + origin_[0], l.Position.y + origin_[1], l.Position.z + origin_[2]}))
        {
            // One of the room's lamps: the receiver lights it per pixel from the lamp grid, with its shadow.
            draw.vertex_diffuse_off |= static_cast<uint8_t>(1u << slot);
            draw.vertex_specular_off |= static_cast<uint8_t>(1u << slot);
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
    // The per-pixel sun reaches a pixel by how open it is (the visibility volume, ff_fragment.sh): under a roof none
    // of it arrives, whatever the scripts say of the place's weather.
    if (!lit && per_pixel && (bgfx::isValid(gloss_map.handle) || wet) && weather_sun_.present)
    {
        // A glossy (or wet) baked surface: the bake holds the sun's light, its highlight is added per pixel
        // (ff_fragment.sh, by how open the pixel is) from the weather's sun (a baked draw has the fixed-function lights
        // off).
        fx::LightingInput sun = input;
        sun.sun_dir = {weather_sun_.dir[0], weather_sun_.dir[1], weather_sun_.dir[2]};
        sun.sun_diffuse = {weather_sun_.color[0], weather_sun_.color[1], weather_sun_.color[2]};
        const fx::LightingUniforms from_weather = fx::ComputeLighting(sun, material, sky_, settings_);
        for (int k = 0; k < 3; ++k)
        {
            u[0][k] = from_weather.u_fxLight[0][k];
            u[1][k] = from_weather.u_fxLight[1][k];
        }
    }
    else if (!per_pixel || sun_slot < 0 || !lit)
        u[1][0] = u[1][1] = u[1][2] = 0.0f; // the sun stays in the vertex sum (or there is none)
    else
    {
        draw.vertex_diffuse_off |= static_cast<uint8_t>(1u << sun_slot);
        if (specular)
            draw.vertex_specular_off |= static_cast<uint8_t>(1u << sun_slot);
    }
    // Lit models of an interior stand in the room's own light (the baked colours around them, ff_fragment.sh
    // ffRoomLight) instead of the weather's ambient.
    draw.room_light = lit && lamp_on && lamps_.room_light();
    if ((hemisphere || draw.room_light) && lit)
        draw.vertex_ambient_off = true;
    if (!hemisphere || !lit)
        for (int i = 0; i < 3; ++i)
            u[3][i] = u[4][i] = 0.0f;
    if (!per_pixel || !(flags_ & fx::kLightingTranslucent))
        u[2][3] = 0.0f;
    if (!per_pixel || !(flags_ & fx::kLightingRim))
        u[5][1] = 0.0f;
    u[5][0] *= relief_fade;
    const bool has_relief = bgfx::isValid(normal_map) && u[5][0] > 0.0f;
    draw.ff = {two_sided && u[2][3] > 0.0f ? 1.0f : 0.0f, has_relief ? static_cast<float>(relief_stage) : 0.0f, 0.0f, 0.0f};
    int relief = 0; // the variant: 1 the normal map, 2 with the parallax
    if (has_relief)
    {
        relief = 1;
        draw.normal_map = normal_map;
        draw.normal_map_flags = device.sampler_flags(relief_stage, *shim::store_of(device.s.textures[relief_stage]));
        ++frame_.relief;
        // The parallax: the relief stage's map with its height (stage 0's colour texture, or stage 1's grey height
        // texture with its own heights), on coordinates that are not projected (the variant reads that stage where the
        // relief is); its depth fades in with the relief.
        // A clamp-addressed stage (a texture drawn once over its polygon, not tiled) has none: the shifted reads would
        // run off its edge into the clamped border.
        const uint32_t u = draw.normal_map_flags & BGFX_SAMPLER_U_MASK, v = draw.normal_map_flags & BGFX_SAMPLER_V_MASK;
        const bool clamped = u == BGFX_SAMPLER_U_CLAMP || u == BGFX_SAMPLER_U_BORDER || v == BGFX_SAMPLER_V_CLAMP ||
                             v == BGFX_SAMPLER_V_BORDER;
        // The cavity needs no shift: it also draws on a clamped stage 0 or with the parallax off.
        const bool parallax = settings_.parallax != fx::ParallaxQuality::Off && !clamped && relief_map.parallax;
        if (bgfx::isValid(relief_map.material) && relief_map.depth > 0.0f && (parallax || settings_.relief_cavity) &&
            !(device.s.tss[relief_stage][D3DTSS_TEXTURETRANSFORMFLAGS] & D3DTTFF_PROJECTED))
        {
            relief = 2;
            draw.parallax_variant = true;
            draw.parallax_untrusted = settings_.parallax != fx::ParallaxQuality::Off && !clamped && !relief_map.parallax;
            draw.material = relief_map.material;
            draw.material_flags = draw.normal_map_flags;
            // The class's own depth (stored with the height, fx::ReliefDepthTexels), the player's strength, the fade-in.
            const float depth = relief_map.depth * std::clamp(settings_.parallax_strength, 0.0f, 3.0f) * relief_fade;
            const bool occlusion = parallax && settings_.parallax == fx::ParallaxQuality::Occlusion;
            // The relief's own shadow towards the sun: occlusion mapping only, not indoors (no sun shines there).
            const bool self_shadow = occlusion && settings_.parallax_shadow && !lamps_.interior();
            draw.parallax = {depth / static_cast<float>(relief_map.width), depth / static_cast<float>(relief_map.height_texels),
                             !parallax ? 0.0f : occlusion ? static_cast<float>(fx::kParallaxSteps) : 1.0f,
                             std::log2(static_cast<float>(std::max(relief_map.width, relief_map.height_texels))),
                             self_shadow ? fx::kParallaxShadow : 0.0f, settings_.parallax_debug ? 1.0f : 0.0f,
                             settings_.relief_cavity ? fx::kReliefCavity : 0.0f, fx::kParallaxMaxDepth};
            if (!draw.parallax_untrusted)
                ++frame_.parallax;
        }
    }
    // Gloss: the map's texels shape the highlight; u_fxGloss.w switches on the lamps' and the baked surfaces'
    // highlights (develop's look without gloss: the lit materials' fixed highlight only).
    // The parallax's material, when it is bound, is the same texture (stage 0's relief); a stage 1 parallax binds stage
    // 1's at the same unit: stage 0's gloss yields to it (counted in the census).
    const bool gloss_yields = draw.parallax_variant && relief_stage == 1 && bgfx::isValid(gloss_map.handle);
    if (gloss_yields)
        ++frame_.gloss_yields;
    const bool glossy = !gloss_yields && bgfx::isValid(gloss_map.handle) && gloss_map.fade > 0.0f;
    if (glossy)
    {
        draw.material = gloss_map.handle;
        draw.material_flags = device.sampler_flags(0, *gloss_store);
        ++frame_.glossy;
    }
    // The metal tint: by a gloss map's metal mask (its R, u_fxGloss.y > 0); a relief's material has its height in R,
    // so there by its polished texels, their gloss (u_fxGloss.y < 0, ff_fragment.sh).
    const float metal_tint = fx::GlossMetalTint(gloss_class);
    draw.gloss = {glossy ? gloss_map.fade : 0.0f, glossy ? (metal_mask ? metal_tint : -metal_tint) : 0.0f,
                  gloss_ ? static_cast<float>(settings_.gloss_debug) : 0.0f, gloss_ ? settings_.gloss_strength : 0.0f};
    // Wet baked surfaces take the highlights too (their gloss is the rain's, ff_wet.sh).
    const bool baked_specular = specular && ((gloss_ && (glossy || gloss_debug)) || wet);

    if (lit)
    {
        fx::LightSelection selection;
        if (budget_ > 0 && point_count > 0)
        {
            uint8_t picked = 0;
            // Lights left out stay in the vertex sum, so the choice is not faded; it depends only on where the
            // draw's vertices and the lights are. Without positions the model origin stands for the draw.
            const D3DMATRIX &w = device.s.world;
            const DrawSphere &sphere = sphere_source.get();
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
        // Surfaces under a location lightmap carry the sun's shadows already (ours: the same sun, TownLight) and the
        // place's still geometry casts from its own tiles: they neither receive nor cast.
        // (Where one walks under a roof no sun shadow map is made: the sun reaches a pixel there by its openness.)
        if (per_pixel && sun_slot >= 0 && target && target == picture && !lamps_.interior() && !lightmapped)
        {
            memcpy(draw.view.data(), &device.s.view.m[0][0], sizeof(float) * 16);
            draw.shadow = draw.sun_caster = shadows_.prepare(const_cast<shim::Device &>(device), draw.view, origin_, casters_);
            draw.caster |= draw.sun_caster;
        }
        // How the draw's material casts (effects/casters.hpp): cut where the colour pass's alpha test discards,
        // nothing for a blended material that writes no depth.
        if (draw.caster)
        {
            draw.caster_material = caster_material(device, layout);
            if (!draw.caster_material.casts)
                draw.caster = draw.sun_caster = draw.lamp_caster = false;
        }
        const int shadow = draw.shadow ? (settings_.shadows == fx::ShadowQuality::Soft ? 2 : 1) : 0;
        draw.fs_id = fs_lit_[specular][relief][linear][shadow];
        frame_.shadowed += draw.shadow ? 1 : 0;
        ++frame_.lit;
    }
    else
    {
        draw.points = fx::PackPointLights(baked_lights, input.view);
        int shadow = 0;
        if (town_wall)
        {
            draw.town_wall = true;
            ++frame_.town_walls;
            // They receive the cascade (the place's still geometry casts into it from its tiles).
            if (settings_.shadows != fx::ShadowQuality::Off && sun_slot >= 0)
            {
                memcpy(draw.view.data(), &device.s.view.m[0][0], sizeof(float) * 16);
                draw.shadow = shadows_.prepare(const_cast<shim::Device &>(device), draw.view, origin_, casters_);
                shadow = draw.shadow ? (settings_.shadows == fx::ShadowQuality::Soft ? 2 : 1) : 0;
            }
            if (draw.view == std::array<float, 16>{})
                memcpy(draw.view.data(), &device.s.view.m[0][0], sizeof(float) * 16);
        }
        draw.fs_id = fs_baked_[baked_specular ? 1 : 0][relief][linear][shadow];
        ++frame_.baked;
    }
}

const char *Lighting::ParallaxMissName(ParallaxMiss miss)
{
    static const char *const names[] = {"no relief by its class", "ported shader", "decal", "pretransformed",
                                        "no vertex normals", "cube map", "character", "switched off",
                                        "stage 1 height texture without a height", "map not ready", "map without height",
                                        "projected coordinates", "clamp-addressed", "no relief on the draw",
                                        "its class takes none, too few trusted texels or excluded", "other"};
    static_assert(std::size(names) == kParallaxMisses);
    return names[static_cast<int>(miss)];
}

void Lighting::census(const shim::Device &device, const shim::LayoutInfo &layout, bool ported, bool decal,
                      uint32_t cube_variant, const FfDraw &draw)
{
    const shim::TexStore *s0 = shim::store_of(device.s.textures[0]);
    if (!s0 || s0->kind != shim::TexKind::Tex2D || device.s.tss[0][D3DTSS_COLOROP] == D3DTOP_DISABLE)
        return; // untextured
    // The relief's stage: stage 1's grey height texture when it took the draw, else stage 0's colour texture.
    const shim::TexStore *s1 = shim::store_of(device.s.textures[1]);
    const bool stage1 = s1 && s1->normal_map && device.s.tss[1][D3DTSS_COLOROP] != D3DTOP_DISABLE;
    const int stage = stage1 ? 1 : 0;
    const shim::TexStore &texture = stage1 ? *s1 : *s0;
    if (draw.parallax_variant)
    {
        // counted as frame_.parallax
        if (draw.parallax_untrusted)
        {
            ++frame_.parallax_miss[static_cast<size_t>(ParallaxMiss::NotTrusted)];
            cover(texture, texture.normal_map->relief, static_cast<uint8_t>(ParallaxMiss::NotTrusted));
            return;
        }
        cover(texture, texture.normal_map->relief, kCoverageParallax);
        return;
    }
    const uint32_t flags = device.sampler_flags(stage, texture);
    const uint32_t u = flags & BGFX_SAMPLER_U_MASK, v = flags & BGFX_SAMPLER_V_MASK;
    const NormalMapEntry *entry = texture.normal_map.get();
    ParallaxMiss miss = ParallaxMiss::Other;
    if (!entry)
        miss = ParallaxMiss::NoReliefClass;
    else if (ported)
        miss = ParallaxMiss::Ported;
    else if (decal)
        miss = ParallaxMiss::Decal;
    else if (layout.rhw)
        miss = ParallaxMiss::Pretransformed;
    else if (!layout.has_normal)
        miss = ParallaxMiss::NoNormals;
    else if (cube_variant != 0)
        miss = ParallaxMiss::Cube;
    else if (device.s.rs[D3DRS_LIGHTING] && material_ == Material::Character)
        miss = ParallaxMiss::Character;
    else if (!lighting_ || !(flags_ & fx::kLightingNormalMap) ||
             (settings_.parallax == fx::ParallaxQuality::Off && !settings_.relief_cavity))
        miss = ParallaxMiss::Off;
    else if (entry->state != NormalMapEntry::Uploaded)
        miss = ParallaxMiss::NotReady;
    else if (!bgfx::isValid(entry->material) || entry->depth <= 0.0f)
        miss = stage1 ? ParallaxMiss::Stage1 : ParallaxMiss::NoHeight;
    else if (device.s.tss[stage][D3DTSS_TEXTURETRANSFORMFLAGS] & D3DTTFF_PROJECTED)
        miss = ParallaxMiss::Projected;
    else if (u == BGFX_SAMPLER_U_CLAMP || u == BGFX_SAMPLER_U_BORDER || v == BGFX_SAMPLER_V_CLAMP || v == BGFX_SAMPLER_V_BORDER)
        miss = ParallaxMiss::Clamped;
    else if (!bgfx::isValid(draw.normal_map))
        miss = ParallaxMiss::NoRelief;
    ++frame_.parallax_miss[static_cast<size_t>(miss)];
    cover(texture, entry ? entry->relief : fx::ReliefClass::None, static_cast<uint8_t>(miss));
}

void Lighting::cover(const shim::TexStore &texture, fx::ReliefClass relief, uint8_t status)
{
    if (texture.name.empty())
        return; // not a game file (render targets, the renderer's own textures)
    auto [it, added] = parallax_coverage_.try_emplace(texture.serial);
    Coverage &c = it->second;
    if (added)
        c.name = texture.name;
    // A texture that drew with the parallax in the window counts with it (its other draws may be clamped, decals...).
    const bool parallax = status == kCoverageParallax;
    if (parallax)
        c.parallax_frame = census_frame_;
    c.seen |= 1u << (parallax ? 31 : status);
    if ((added || c.relief != relief) && relief == fx::ReliefClass::None)
        c.rule = fx::ClassifyTextureName(c.name).rule;
    c.relief = relief;
    c.status = !parallax && c.parallax_frame && census_frame_ - c.parallax_frame < kCoverageFrames ? kCoverageParallax
                                                                                                   : status;
    c.frame = census_frame_;
}

// One line per texture whose set of reasons changed since its last line (a texture drawn with two reasons in a window
// is one line, not one per draw).
void Lighting::log_texture(Coverage &c)
{
    std::string reasons;
    if (c.seen & (1u << 31))
        reasons = "parallax";
    for (size_t i = 0; i < kParallaxMisses; ++i)
        if (c.seen & (1u << i))
            reasons += std::string(reasons.empty() ? "" : ", ") + "no parallax: " + ParallaxMissName(static_cast<ParallaxMiss>(i));
    shim::logf("effects parallax: texture %s (%s): %s%s%s", c.name.c_str(), fx::ReliefClassName(c.relief), reasons.c_str(),
               c.rule && c.relief == fx::ReliefClass::None ? ", rule " : "", c.rule && c.relief == fx::ReliefClass::None ? c.rule : "");
    c.logged = c.seen;
}

void Lighting::log_coverage()
{
    // Per class: textures with the parallax, without it, and why (by reason; a class without relief by its rules).
    struct Row
    {
        uint32_t with = 0, without = 0;
        std::vector<std::pair<std::string, uint32_t>> reasons;
    };
    std::array<Row, static_cast<size_t>(fx::ReliefClass::Prop) + 1> rows{};
    for (auto it = parallax_coverage_.begin(); it != parallax_coverage_.end();)
    {
        Coverage &c = it->second;
        if (census_frame_ - c.frame >= 10 * kCoverageFrames)
        {
            it = parallax_coverage_.erase(it); // not drawn for a long while (another location)
            continue;
        }
        if (census_frame_ - c.frame < kCoverageFrames)
        {
            if (c.seen != c.logged)
                log_texture(c);
            Row &row = rows[static_cast<size_t>(c.relief)];
            if (c.status == kCoverageParallax)
                ++row.with;
            else
            {
                ++row.without;
                const std::string reason =
                    c.relief == fx::ReliefClass::None && c.rule ? std::string("rule ") + c.rule
                                                                : ParallaxMissName(static_cast<ParallaxMiss>(c.status));
                const auto found = std::find_if(row.reasons.begin(), row.reasons.end(),
                                                [&](const auto &r) { return r.first == reason; });
                if (found == row.reasons.end())
                    row.reasons.emplace_back(reason, 1);
                else
                    ++found->second;
            }
        }
        c.seen = 0; // the next window's census starts empty
        ++it;
    }
    std::string table;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        Row &row = rows[i];
        if (!row.with && !row.without)
            continue;
        std::sort(row.reasons.begin(), row.reasons.end(), [](const auto &a, const auto &b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        std::string reasons;
        for (const auto &[reason, n] : row.reasons)
            reasons += (reasons.empty() ? "" : ", ") + reason + " " + std::to_string(n);
        table += std::string(table.empty() ? "" : "; ") + fx::ReliefClassName(static_cast<fx::ReliefClass>(i)) + " " +
                 std::to_string(row.with) + " with, " + std::to_string(row.without) + " without" +
                 (reasons.empty() ? "" : " (" + reasons + ")");
    }
    if (table.empty() || table == logged_coverage_)
        return;
    logged_coverage_ = table;
    shim::logf("effects parallax coverage, textures drawn in the last %u frames: %s", kCoverageFrames, table.c_str());
}

void Lighting::submit(const FfDraw &draw)
{
    caster_ = draw;
    if (!bgfx::isValid(u_coverage_))
    {
        u_coverage_ = bgfx::createUniform("u_fxCoverage", bgfx::UniformType::Vec4);
        u_ff_ = bgfx::createUniform("u_fxFf", bgfx::UniformType::Vec4);
        u_wet_ = bgfx::createUniform("u_fxWet", bgfx::UniformType::Vec4, 2);
    }
    bgfx::setUniform(u_coverage_, draw.coverage.data());
    if (!draw.lit)
        return;
    bgfx::setUniform(u_ff_, draw.ff.data());
    bgfx::setUniform(u_wet_, draw.wet.data(), 2);
    fx_.Apply(draw.light);
    fx_.Apply(draw.points);
    if (bgfx::isValid(draw.normal_map))
        fx_.BindNormalMap(draw.normal_map, draw.normal_map_flags);
    if (!bgfx::isValid(u_gloss_))
        u_gloss_ = bgfx::createUniform("u_fxGloss", bgfx::UniformType::Vec4);
    bgfx::setUniform(u_gloss_, draw.gloss.data());
    if (bgfx::isValid(draw.material))
        fx_.BindMaterial(draw.material, draw.material_flags);
    if (draw.parallax_variant)
        fx_.SetParallax(draw.parallax);
    if (draw.shadow)
        shadows_.bind(draw.view);
    bind_ao_volume(draw);
    bind_visibility(draw);
    bind_town_wall(draw);
    lamps_.bind(draw.lamps, draw.world_view, draw.room_light);
}

void Lighting::submit_caster(shim::Device &device, const D3DMATRIX &world, bool skinned)
{
    if (caster_.lamp_caster)
    {
        // The lamp faces are absolute; the draw's world matrix is camera-relative.
        D3DMATRIX absolute = world;
        absolute.m[3][0] += caster_.origin[0];
        absolute.m[3][1] += caster_.origin[1];
        absolute.m[3][2] += caster_.origin[2];
        lamps_.submit_caster(device, &absolute.m[0][0], skinned, caster_.sphere.center, caster_.sphere.radius, casters_,
                             caster_.caster_material);
        ++frame_.lamp_casters;
    }
    if (caster_.sun_caster)
    {
        shadows_.submit_caster(device, &world.m[0][0], skinned, casters_, caster_.caster_material);
        ++frame_.casters;
    }
    else
        bgfx::discard(BGFX_DISCARD_ALL);
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
            const uint8_t open[4] = {255, 255, 255, 255};
            ao_open_ = bgfx::createTexture3D(1, 1, 1, false, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_POINT, bgfx::copy(open, 4));
        }
        const float off[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_ao_params_, off);
        bgfx::setTexture(fx::kAoVolumeStage, s_ao_volume_, ao_open_);
        return;
    }
    if (draw.world_view != ao_view_)
    {
        float inverse_view[16];
        bx::mtxInverse(inverse_view, draw.world_view.data());
        bx::mtxMul(ao_view_to_uvw_.data(), inverse_view, ao_volume_->world_to_uvw.data());
        ao_view_ = draw.world_view;
    }
    const fx::AoVolumeLevel &level = fx::kAoVolumeLevel[std::min<uint8_t>(settings_.ao_volume, fx::kAoVolumeLevels)];
    const float params[4] = {level.strength, level.contrast, 0.5f * ao_volume_->cell, 1.0f / fx::kAoOpenSurface};
    bgfx::setUniform(u_ao_matrix_, ao_view_to_uvw_.data());
    bgfx::setUniform(u_ao_params_, params);
    bgfx::setTexture(fx::kAoVolumeStage, s_ao_volume_, ao_volume_->texture);
}

void Lighting::bind_visibility(const FfDraw &draw)
{
    if (!bgfx::isValid(u_visibility_params_))
    {
        s_visibility_ = bgfx::createUniform("s_fxVisibility", bgfx::UniformType::Sampler);
        u_visibility_matrix_ = bgfx::createUniform("u_fxVisibility", bgfx::UniformType::Mat4);
        u_visibility_params_ = bgfx::createUniform("u_fxVisibilityParams", bgfx::UniformType::Vec4);
    }
    // Like the AO volume: the 3D sampler needs a 3D texture on its unit whether or not this draw has a volume.
    if (!visibility_)
    {
        if (!bgfx::isValid(visibility_open_))
        {
            const uint8_t open[4] = {255, 255, 0, 0};
            visibility_open_ = bgfx::createTexture3D(1, 1, 1, false, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_POINT, bgfx::copy(open, 4));
        }
        const float off[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_visibility_params_, off);
        bgfx::setTexture(fx::kVisibilityStage, s_visibility_, visibility_open_);
        return;
    }
    // (The texture's layout changes when the bounce's block comes or goes.)
    if (draw.world_view != visibility_view_ || visibility_->world_to_uvw != visibility_world_to_uvw_)
    {
        float inverse_view[16];
        bx::mtxInverse(inverse_view, draw.world_view.data());
        bx::mtxMul(visibility_view_to_uvw_.data(), inverse_view, visibility_->world_to_uvw.data());
        visibility_view_ = draw.world_view;
        visibility_world_to_uvw_ = visibility_->world_to_uvw;
    }
    // x the mode (2 / 3: the debug views of the visibility / the bounce), y the receivers' offset along their normal,
    // one cell, z the bounce block's offset in v (0: none), w the lamps' share of the bounce's light.
    const float mode = settings_.visibility_debug ? 2.0f : settings_.bounce_debug ? 3.0f : 1.0f;
    const float params[4] = {mode, visibility_->cell, visibility_->block_offset, visibility_->lamp_share};
    bgfx::setUniform(u_visibility_matrix_, visibility_view_to_uvw_.data());
    bgfx::setUniform(u_visibility_params_, params);
    bgfx::setTexture(fx::kVisibilityStage, s_visibility_, visibility_->texture);
}

void Lighting::bind_town_wall(const FfDraw &draw)
{
    if (!bgfx::isValid(u_town_wall_))
        u_town_wall_ = bgfx::createUniform("u_fxTownWall", bgfx::UniformType::Vec4, 5);
    std::array<std::array<float, 4>, 5> u{};
    const fx::TownLightTone *tone = town_light_.wall_light();
    const fx::TownWallTone *wall = town_light_.wall_tone();
    if (draw.town_wall && tone && wall)
    {
        // Directions into the draw's view space (the device view's rotation; directions are the same absolute or
        // camera-relative).
        auto to_view = [&](const std::array<float, 3> &d, std::array<float, 4> &out) {
            for (int k = 0; k < 3; ++k)
                out[k] = d[0] * draw.view[0 * 4 + k] + d[1] * draw.view[1 * 4 + k] + d[2] * draw.view[2 * 4 + k];
        };
        u[0] = {tone->a, tone->b, tone->c, 1.0f};
        to_view(town_light_.live_sun(), u[1]);
        u[1][3] = town_light_.live_sun_luminance();
        to_view(town_light_.bake_sun(), u[2]);
        for (int i = 0; i < fx::TownWallTone::kSkyBins; ++i)
            u[3 + i / 4][i % 4] = wall->game_sky[i];
    }
    bgfx::setUniform(u_town_wall_, u.data(), 5);
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
    // World up in view space is the second row of the row-vector view matrix; the eye's height -t * R^T, in absolute
    // world (the device's view is camera-relative: its eye is at the origin's height).
    const float up[4] = {view.m[1][0], view.m[1][1], view.m[1][2], 0.0f};
    const float eye_height =
        origin_[1] - (view.m[3][0] * view.m[1][0] + view.m[3][1] * view.m[1][1] + view.m[3][2] * view.m[1][2]);
    haze = fx::ComputeAerialFogHaze(eye_height);
    bgfx::setUniform(u_aerial_up_, up);
    ++frame_.aerial;
    return true;
}

void Lighting::end_frame()
{
    last_frame_ = frame_;
    frame_ = {};
    // Every 30 frames, when it changed: the parallax census of the last frame.
    if (++census_frames_ >= 30)
    {
        census_frames_ = 0;
        uint32_t missed = 0;
        for (const uint32_t n : last_frame_.parallax_miss)
            missed += n;
        if ((last_frame_.parallax != logged_parallax_ || last_frame_.parallax_miss != logged_miss_) &&
            (last_frame_.parallax || missed))
        {
            logged_parallax_ = last_frame_.parallax;
            logged_miss_ = last_frame_.parallax_miss;
            std::string without;
            for (size_t i = 0; i < last_frame_.parallax_miss.size(); ++i)
                if (last_frame_.parallax_miss[i])
                    without += std::string(without.empty() ? "" : ", ") + ParallaxMissName(static_cast<ParallaxMiss>(i)) +
                               " " + std::to_string(last_frame_.parallax_miss[i]);
            shim::logf("effects parallax: %u textured draws drew with it, %u without (%s); stage 0's gloss yielded to a "
                       "stage 1 parallax in %u",
                       last_frame_.parallax, missed, without.empty() ? "none" : without.c_str(), last_frame_.gloss_yields);
        }
    }
    ++census_frame_;
    if (census_frame_ % kCoverageFrames == 0)
        log_coverage();
    town_light_.end_frame();
    shadows_.end_frame();
    normal_maps_.end_frame();
    lamps_.end_frame();
    if (!dynamic_lights_this_frame_)
        dynamic_lights_.clear(); // no location reported them: none exist
    dynamic_lights_this_frame_ = false;
}

void Lighting::shutdown()
{
    probe_.shutdown();
    fx_.Shutdown();
    shadows_.shutdown();
    town_light_.shutdown();
    lamps_.shutdown();
    casters_.shutdown();
    if (bgfx::isValid(ao_open_))
        bgfx::destroy(ao_open_);
    if (bgfx::isValid(visibility_open_))
        bgfx::destroy(visibility_open_);
    visibility_open_ = BGFX_INVALID_HANDLE;
    ao_open_ = BGFX_INVALID_HANDLE;
    for (bgfx::UniformHandle *u : {&u_coverage_, &u_ff_, &u_wet_, &u_gloss_, &u_aerial_up_, &s_ao_volume_, &u_ao_matrix_, &u_ao_params_, &s_visibility_,
                                   &u_visibility_matrix_, &u_visibility_params_, &u_town_wall_})
    {
        if (bgfx::isValid(*u))
            bgfx::destroy(*u);
        *u = BGFX_INVALID_HANDLE;
    }
}

void Lighting::on_device_lost()
{
    probe_.on_device_lost();
    fx_.OnDeviceLost();
    shadows_.on_device_lost();
    town_light_.on_device_lost();
    lamps_.on_device_lost();
    casters_.on_device_lost();
    u_coverage_ = u_ff_ = u_wet_ = u_gloss_ = u_aerial_up_ = s_ao_volume_ = u_ao_matrix_ = u_ao_params_ = BGFX_INVALID_HANDLE;
    s_visibility_ = u_visibility_matrix_ = u_visibility_params_ = u_town_wall_ = BGFX_INVALID_HANDLE;
    ao_view_ = {};
    ao_open_ = visibility_open_ = BGFX_INVALID_HANDLE;
}

} // namespace storm::effects
