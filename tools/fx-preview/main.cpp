// fx_preview: renders the test scenes of the graphics effects package with
// bgfx on a headless OpenGL ES 3.0 context (EGL pbuffer; Mesa llvmpipe is
// enough) and writes PNG files for visual review. See README.md.
#include "scene.hpp"

#include "storm/fx/capabilities.hpp"
#include "storm/fx/color_lut.hpp"
#include "storm/fx/contact_shadows.hpp"
#include "storm/fx/lighting.hpp"
#include "storm/fx/log.hpp"
#include "storm/fx/noise_bake.hpp"
#include "storm/fx/post_chain.hpp"
#include "storm/fx/shadows.hpp"
#include "storm/fx/sky.hpp"

#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bx/file.h>
#include <bx/math.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <array>
#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

#include "preview_shaders/includes.inc"

using namespace storm;

namespace
{
constexpr uint16_t kWidth = 1280;
constexpr uint16_t kHeight = 720;
constexpr float kNear = 0.5f;
constexpr float kFar = 4000.0f;
constexpr bgfx::ViewId kShadowView = 0;
constexpr bgfx::ViewId kSceneView = 1;
constexpr bgfx::ViewId kPostView = 10;
constexpr bgfx::ViewId kReadbackView = 40;

void WritePng(const std::string &path, uint32_t width, uint32_t height, uint32_t pitch, const void *data, bimg::TextureFormat::Enum format,
              bool y_flip)
{
    bx::FileWriter writer;
    bx::Error error;
    if (bx::open(&writer, bx::FilePath(path.c_str()), false, &error))
    {
        bimg::imageWritePng(&writer, width, height, pitch, data, format, y_flip, &error);
        bx::close(&writer);
    }
    if (!error.isOk())
        std::printf("cannot write %s\n", path.c_str());
}

struct GpuMesh
{
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    fx::Vec3 center{0.0f, 0.0f, 0.0f}; // bounding sphere, for light selection
    float radius = 0.0f;
    // Stage 1 of the game's materials: the height texture as a detail layer, and the normal map derived from it.
    bgfx::TextureHandle detail = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normal_map = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle base = BGFX_INVALID_HANDLE; // stage 0, RGBA; alpha tested when set
    float base_size = 1.0f;                         // its size in texels

    void Set(const preview::Mesh &mesh)
    {
        Reset();
        if (mesh.indices.empty())
            return;
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        fx::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        for (const preview::Vertex &v : mesh.vertices)
            for (int c = 0; c < 3; ++c)
            {
                lo[c] = std::min(lo[c], v.pos[c]);
                hi[c] = std::max(hi[c], v.pos[c]);
            }
        center = {0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2])};
        radius = 0.5f * std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
        vertices = bgfx::createVertexBuffer(
            bgfx::copy(mesh.vertices.data(), static_cast<uint32_t>(mesh.vertices.size() * sizeof(preview::Vertex))), layout);
        indices = bgfx::createIndexBuffer(bgfx::copy(mesh.indices.data(), static_cast<uint32_t>(mesh.indices.size() * 4)),
                                          BGFX_BUFFER_INDEX32);
    }
    void Reset()
    {
        if (bgfx::isValid(vertices))
            bgfx::destroy(vertices);
        if (bgfx::isValid(indices))
            bgfx::destroy(indices);
        vertices = BGFX_INVALID_HANDLE;
        indices = BGFX_INVALID_HANDLE;
    }
};

enum class Content
{
    Ocean,    // open sea with a ship and an island
    Shore,    // low camera next to the island's beach
    Deck,     // ship and a figure close up: lighting, shadows, ambient occlusion
    Harbour,  // night quay with torches, lanterns, a figure and a moored ship: point lights, bloom
    Broadside, // a ship firing its guns at dusk: flash lights
    Materials, // wall, boulder, plank deck and sail with height textures: derived normal maps
    Garden,    // palms, grass and rigging: alpha to coverage, wind sway
    Pattern,  // aliasing test chart
};

struct Shot
{
    std::string name;
    Content content = Content::Ocean;
    fx::Settings settings;
    preview::Weather weather = preview::Weather::Day;
    float render_scale = 1.0f;
    float sun_turn = 0.0f; // rotates the sun around the vertical axis, degrees
    float time = 12.5f;    // seconds: clouds, wind (the sea and the ships stay at kTime)
    float interior = 0.0f; // 1 = the scene counts as indoors for the scene grade
    bool blobs = false;    // contact shadow blobs under the quay's figure and objects
};

// Vertex + fragment pair of the fixed-function stand-in per lighting variant, keyed by fx::LightingDefines().
struct MeshVariant
{
    const char *defines;
    const uint8_t *vs;
    uint32_t vs_size;
    const uint8_t *fs;
    uint32_t fs_size;
};
constexpr MeshVariant kMeshVariants[] = {
#define PREVIEW_VARIANT(defines, vs, fs) {defines, vs, sizeof(vs), fs, sizeof(fs)},
#include "preview_shaders/variants.inc"
#undef PREVIEW_VARIANT
};

// Binary PGM (P5, 8 bit), the simplest container for a grey height map.
std::vector<uint8_t> ReadPgm(const std::string &path, int &width, int &height)
{
    std::vector<uint8_t> pixels;
    if (FILE *file = std::fopen(path.c_str(), "rb"))
    {
        int max_value = 0;
        if (std::fscanf(file, "P5 %d %d %d", &width, &height, &max_value) == 3 && max_value == 255 && width > 0 && height > 0)
        {
            std::fgetc(file);
            pixels.resize(static_cast<size_t>(width) * height);
            if (std::fread(pixels.data(), 1, pixels.size(), file) != pixels.size())
                pixels.clear();
        }
        std::fclose(file);
    }
    if (pixels.empty())
    {
        std::printf("cannot read height map %s (binary 8-bit PGM expected)\n", path.c_str());
        std::exit(1);
    }
    return pixels;
}

class Preview
{
  public:
    // `height_map_dir`: optional folder with bump_houses.pgm, rockBump.pgm, woodBump.pgm and sailBumpU1.pgm
    // (binary PGM) converted from the player's game for local look-dev; empty = procedural stand-ins.
    Preview(std::string out_dir, const std::string &height_map_dir) : out_dir_(std::move(out_dir))
    {
        for (const MeshVariant &variant : kMeshVariants)
            programs_.push_back(bgfx::createProgram(bgfx::createShader(bgfx::makeRef(variant.vs, variant.vs_size)),
                                                    bgfx::createShader(bgfx::makeRef(variant.fs, variant.fs_size)), true));
        u_point_ = bgfx::createUniform("u_previewPoint", bgfx::UniformType::Vec4, fx::kMaxPointLights * 4);
        u_point_count_ = bgfx::createUniform("u_previewPointCount", bgfx::UniformType::Vec4);
        s_detail_ = bgfx::createUniform("s_previewDetail", bgfx::UniformType::Sampler);
        s_base_ = bgfx::createUniform("s_previewBase", bgfx::UniformType::Sampler);
        u_light_ = bgfx::createUniform("u_previewLight", bgfx::UniformType::Vec4, 3);
        // bgfx without a window has no back buffer: the final image goes to a frame buffer and is read back.
        output_fb_ = bgfx::createFrameBuffer(kWidth, kHeight, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        readback_ = bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8,
                                          BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        const float ship_y = preview::SeaHeight(30.0f, 95.0f, kTime) - 0.4f;
        const preview::Ship ship = preview::MakeShip({30.0f, ship_y, 95.0f}, 0.6f);
        ship_hull_.Set(ship.hull);
        ship_sails_.Set(ship.sails);
        island_.Set(preview::MakeIsland({-150.0f, 1.2f, 210.0f}, 130.0f, 38.0f));
        // Deck scene: a quay with a figure, the ship moored behind it.
        preview::Mesh quay = preview::MakeBox({0.0f, 2.0f, 6.0f}, {14.0f, 0.4f, 5.0f}, {0.50f, 0.46f, 0.40f});
        quay.Append(preview::MakeBox({-6.0f, 3.0f, 8.0f}, {0.9f, 0.6f, 0.9f}, {0.45f, 0.30f, 0.16f}));
        quay.Append(preview::MakeSphere({4.5f, 3.0f, 7.5f}, 0.6f, {0.40f, 0.42f, 0.45f}));
        quay_.Set(quay);
        figure_.Set(preview::MakeFigure({-0.6f, 2.4f, 4.2f}));
        const preview::Ship moored = preview::MakeShip({6.0f, ship_y, 34.0f}, 1.25f);
        moored_hull_.Set(moored.hull);
        moored_sails_.Set(moored.sails);
        // Harbour at night: baked town, a figure under a torch, a ship moored at the quay.
        preview::Harbour harbour = preview::MakeHarbour();
        town_.Set(harbour.town);
        town_glow_.Set(harbour.glow);
        town_lights_ = harbour.lights;
        // The engine flickers each torch every frame (Lights::Execute); freeze one such state.
        for (size_t i = 0; i < town_lights_.size(); ++i)
        {
            const float flicker = 1.0f + 0.16f * std::sin(7.3f * static_cast<float>(i) + 1.1f);
            for (float &channel : town_lights_[i].color)
                channel *= flicker;
        }
        harbour_figure_.Set(preview::MakeFigure({-9.6f, 2.4f, 20.6f}));
        const preview::Ship harbour_ship = preview::MakeShip({-24.0f, ship_y, 2.6f}, 0.0f);
        harbour_hull_.Set(harbour_ship.hull);
        harbour_sails_.Set(harbour_ship.sails);
        // Broadside: the ship lies across the view and fires the guns of the side that faces the camera.
        const preview::Ship firing = preview::MakeShip({4.0f, ship_y, 46.0f}, 0.0f);
        firing_hull_.Set(firing.hull);
        firing_sails_.Set(firing.sails);
        preview::Mesh fire;
        for (int gun = 0; gun < 6; ++gun)
        {
            const fx::Vec3 muzzle{-7.0f + 4.4f * gun, ship_y + 2.3f, 46.0f - 5.6f};
            // The guns fire in a ripple: let the frame fall 20 to 170 ms after each shot.
            flashes_.Add(muzzle, {3.4f, 2.3f, 1.0f}, 26.0f, 0.22f);
            flashes_.Update(0.03f);
            const float left = 1.0f - 0.03f * static_cast<float>(6 - gun) / 0.22f;
            fire.Append(preview::MakeSphere({muzzle[0], muzzle[1], muzzle[2] - 0.6f}, 0.25f + 0.75f * left, {1.0f, 0.86f, 0.55f}, 8));
        }
        muzzle_fire_.Set(fire);
        pattern_.Set(preview::MakeAliasingPattern());

        // Height textures and the normal maps derived from them.
        const uint8_t neutral = 128; // 2x multiply by 0.5 changes nothing
        neutral_detail_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R8, BGFX_SAMPLER_NONE, bgfx::copy(&neutral, 1));
        flat_normal_map_ = lighting_.CreateNormalMap(fx::DeriveNormalMap({&neutral, 1}, 1, 1));
        const std::pair<preview::Relief, const char *> reliefs[] = {{preview::Relief::Wall, "bump_houses"},
                                                                    {preview::Relief::Rock, "rockBump"},
                                                                    {preview::Relief::Deck, "woodBump"},
                                                                    {preview::Relief::Sail, "sailBumpU1"}};
        const uint8_t white[4] = {255, 255, 255, 255};
        white_base_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, bgfx::copy(white, 4));
        // With a full mip chain, like the game's textures: thin strands fade to partial alpha in the distance.
        const auto texture = [](std::vector<uint8_t> rgba, int size) {
            size_t source = 0;
            for (int s = size; s > 1; s /= 2)
            {
                const size_t target = rgba.size();
                rgba.resize(target + static_cast<size_t>(s / 2) * (s / 2) * 4);
                for (int y = 0; y < s / 2; ++y)
                    for (int x = 0; x < s / 2; ++x)
                        for (int c = 0; c < 4; ++c)
                        {
                            const auto at = [&](int px, int py) { return rgba[source + (static_cast<size_t>(py) * s + px) * 4 + c]; };
                            rgba[target + (static_cast<size_t>(y) * (s / 2) + x) * 4 + c] = static_cast<uint8_t>(
                                (at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                        }
                source = target;
            }
            return bgfx::createTexture2D(static_cast<uint16_t>(size), static_cast<uint16_t>(size), true, 1, bgfx::TextureFormat::RGBA8,
                                         BGFX_SAMPLER_NONE, bgfx::copy(rgba.data(), static_cast<uint32_t>(rgba.size())));
        };
        frond_texture_ = texture(preview::MakeFrondTexture(128), 128);
        rope_texture_ = texture(preview::MakeRopeTexture(32), 32);
        const preview::Garden garden = preview::MakeGarden();
        garden_ground_.Set(garden.ground);
        garden_trunks_.Set(garden.trunks);
        garden_foliage_.Set(garden.foliage);
        garden_foliage_.base = frond_texture_;
        garden_foliage_.base_size = 128.0f;
        garden_ropes_.Set(garden.ropes);
        garden_ropes_.base = rope_texture_;
        garden_ropes_.base_size = 32.0f;
        for (size_t i = 0; i < 4; ++i)
        {
            int width = 256, height = 256;
            std::vector<uint8_t> pixels;
            if (height_map_dir.empty())
                pixels = preview::MakeHeightMap(reliefs[i].first, width);
            else
                pixels = ReadPgm(height_map_dir + "/" + reliefs[i].second + ".pgm", width, height);
            relief_cpu_[i] = preview::MakeReliefMesh(reliefs[i].first);
            relief_detail_[i] = bgfx::createTexture2D(static_cast<uint16_t>(width), static_cast<uint16_t>(height), false, 1,
                                                      bgfx::TextureFormat::R8, BGFX_SAMPLER_NONE,
                                                      bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size())));
            relief_normal_[i] = lighting_.CreateNormalMap(fx::DeriveNormalMap(pixels, width, height));
        }
    }

    ~Preview()
    {
        for (GpuMesh *mesh : {&ship_hull_, &ship_sails_, &island_, &quay_, &figure_, &moored_hull_, &moored_sails_, &town_, &town_glow_,
                              &harbour_figure_, &harbour_hull_, &harbour_sails_, &firing_hull_, &firing_sails_, &muzzle_fire_, &pattern_,
                              &legacy_sky_, &legacy_sea_})
            mesh->Reset();
        for (bgfx::ProgramHandle program : programs_)
            bgfx::destroy(program);
        bgfx::destroy(u_light_);
        bgfx::destroy(u_point_);
        bgfx::destroy(u_point_count_);
        bgfx::destroy(s_detail_);
        bgfx::destroy(neutral_detail_);
        bgfx::destroy(white_base_);
        bgfx::destroy(frond_texture_);
        bgfx::destroy(rope_texture_);
        bgfx::destroy(s_base_);
        for (GpuMesh *mesh : {&garden_ground_, &garden_trunks_, &garden_foliage_, &garden_ropes_})
            mesh->Reset();
        bgfx::destroy(flat_normal_map_);
        for (bgfx::TextureHandle texture : relief_detail_)
            bgfx::destroy(texture);
        for (bgfx::TextureHandle texture : relief_normal_)
            bgfx::destroy(texture);
        for (GpuMesh &mesh : relief_)
            mesh.Reset();
        bgfx::destroy(output_fb_);
        bgfx::destroy(readback_);
        if (bgfx::isValid(scene_fb_))
            bgfx::destroy(scene_fb_);
    }

    // Renders one shot to <out_dir>/<name>.png.
    void Render(const Shot &shot)
    {
        const fx::Settings &settings = shot.settings;
        const auto scene_w = static_cast<uint16_t>(std::lround(kWidth * shot.render_scale));
        const auto scene_h = static_cast<uint16_t>(std::lround(kHeight * shot.render_scale));
        // This tool is the renderer: it applies what fx asks of one.
        const fx::RendererState renderer = fx::RendererStateFor(settings);
        EnsureSceneTarget(scene_w, scene_h, renderer.msaa_samples);

        // Camera.
        bx::Vec3 eye{0.0f, 7.5f, -20.0f}, at{12.0f, 5.0f, 100.0f};
        float fov = 55.0f;
        switch (shot.content)
        {
        case Content::Shore:
            eye = {-30.0f, 5.0f, 150.0f};
            at = {-110.0f, 0.0f, 205.0f};
            break;
        case Content::Deck:
            eye = {-3.2f, 4.1f, -1.2f};
            at = {2.0f, 6.5f, 20.0f};
            fov = 62.0f;
            break;
        case Content::Harbour:
            eye = {-3.0f, 4.3f, 14.5f};
            at = {-17.0f, 3.9f, 15.5f};
            fov = 66.0f;
            break;
        case Content::Broadside:
            eye = {-7.0f, 4.6f, 24.0f};
            at = {4.0f, 6.0f, 46.0f};
            fov = 62.0f;
            break;
        case Content::Materials:
            eye = {-0.5f, 2.6f, -6.0f};
            at = {-0.5f, 1.9f, 5.0f};
            fov = 60.0f;
            break;
        case Content::Garden:
            eye = {1.0f, 2.2f, -3.5f};
            at = {0.5f, 3.2f, 10.0f};
            fov = 62.0f;
            break;
        case Content::Pattern:
            eye = {0.0f, 0.0f, -11.0f};
            at = {0.0f, 0.0f, 0.0f};
            fov = 52.0f;
            break;
        default:
            break;
        }
        float view[16], proj[16];
        bx::mtxLookAt(view, eye, at);
        bx::mtxProj(proj, fov, static_cast<float>(kWidth) / kHeight, kNear, kFar, bgfx::getCaps()->homogeneousDepth);

        const fx::WeatherInput weather = preview::MakeWeather(shot.weather, shot.sun_turn);
        sky_.Update(weather, shot.time, eye.y);
        const fx::SkyState &sky_state = sky_.State();

        const bool night = shot.weather == preview::Weather::Night;
        // The game lights the scene with the moon at night; the weather tables give it a direction above the horizon.
        const fx::Vec3 light_dir = night ? fx::Vec3{-0.45f, 0.62f, -0.64f} : weather.sun_dir;
        const fx::Vec3 fog = weather.fog_color;
        const auto rgba = [](fx::Vec3 c) {
            const auto byte = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            return (byte(c[0]) << 24) | (byte(c[1]) << 16) | (byte(c[2]) << 8) | 0xffu;
        };
        bgfx::setViewName(kSceneView, "scene");
        bgfx::setViewMode(kSceneView, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(kSceneView, scene_fb_);
        bgfx::setViewRect(kSceneView, 0, 0, scene_w, scene_h);
        bgfx::setViewClear(kSceneView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           shot.content == Content::Pattern ? 0x101216ffu : rgba(fog), 1.0f, 0);
        bgfx::setViewTransform(kSceneView, view, proj);
        bgfx::touch(kSceneView);

        fx::LightingInput lighting;
        lighting.sun_dir = light_dir;
        lighting.sun_diffuse = weather.sun_color;
        lighting.ambient = weather.ambient_color;
        std::copy_n(view, 16, lighting.view.begin());
        // This frame's point lights, as the engine would hand them to SetLight.
        std::vector<fx::PointLight> lights;
        if (shot.content == Content::Harbour)
            lights = town_lights_;
        if (shot.content == Content::Broadside)
            flashes_.AppendTo(lights);
        if (shot.content == Content::Materials && night)
        {
            fx::PointLight torch; // a torch held between the wall and the boulder
            torch.position = {-2.6f, 2.3f, 3.6f};
            torch.color = {1.3f, 0.8f, 0.36f};
            torch.range = 14.0f;
            torch.att0 = 0.35f;
            torch.att1 = 0.10f;
            torch.att2 = 0.035f;
            lights.push_back(torch);
        }

        // Lighting variant of the fixed-function stand-in for these settings.
        const std::string defines = fx::LightingDefines(settings);
        size_t variant = 0;
        while (variant < std::size(kMeshVariants) && defines != kMeshVariants[variant].defines)
            ++variant;
        if (variant == std::size(kMeshVariants))
        {
            // The variant table (preview_variants in CMakeLists.txt) no longer matches fx::LightingDefines().
            std::printf("%s: no shader variant for \"%s\" (add it to preview_variants in CMakeLists.txt)\n", shot.name.c_str(),
                        defines.c_str());
            std::exit(1);
        }
        const bool per_pixel_points = (settings.lighting_flags & fx::kLightingPoint) != 0;

        // Sun shadows: the lit meshes of the scene are the casters, as ships and characters are in the game.
        std::vector<const GpuMesh *> casters;
        fx::Vec3 focus{eye.x, eye.y, eye.z};
        if (shot.content == Content::Ocean || shot.content == Content::Shore)
            casters = {&ship_hull_, &ship_sails_};
        else if (shot.content == Content::Deck)
            casters = {&quay_, &figure_, &moored_hull_, &moored_sails_}, focus = {2.0f, 3.0f, 16.0f};
        else if (shot.content == Content::Broadside)
            casters = {&firing_hull_, &firing_sails_}, focus = firing_hull_.center;
        else if (shot.content == Content::Garden)
            casters = {&garden_trunks_}, focus = {0.0f, 2.0f, 10.0f};
        else if (shot.content == Content::Materials)
        {
            // The wall is static location geometry: its sun and ambient light are baked into the vertex colours.
            preview::Mesh wall = relief_cpu_[0];
            for (preview::Vertex &v : wall.vertices)
            {
                const float ndl = std::max(v.normal[0] * light_dir[0] + v.normal[1] * light_dir[1] + v.normal[2] * light_dir[2], 0.0f);
                uint32_t packed = 0xff000000u;
                for (int c = 0; c < 3; ++c)
                    packed |= static_cast<uint32_t>(std::lround(std::clamp(weather.ambient_color[c] + weather.sun_color[c] * ndl, 0.0f, 1.0f) * 255.0f)) << (8 * c);
                v.baked = packed;
            }
            for (size_t i = 0; i < 4; ++i)
            {
                relief_[i].Set(i == 0 ? wall : relief_cpu_[i]);
                relief_[i].detail = relief_detail_[i];
                relief_[i].normal_map = relief_normal_[i];
            }
            casters = {&relief_[1], &relief_[3]};
            focus = {0.0f, 1.0f, 3.0f};
        }
        fx::ShadowFrame shadow_frame;
        shadow_frame.view = kShadowView;
        shadow_frame.sun_dir = light_dir;
        shadow_frame.focus = focus;
        shadow_frame.radius = shot.content == Content::Deck ? 34.0f : shot.content == Content::Materials ? 16.0f : 60.0f;
        if (shadows_.Begin(settings, shadow_frame))
            for (const GpuMesh *mesh : casters)
            {
                bgfx::setVertexBuffer(0, mesh->vertices);
                bgfx::setIndexBuffer(mesh->indices);
                bgfx::setState(fx::Shadows::kCasterState);
                bgfx::submit(kShadowView, shadows_.CasterProgram());
            }

        // material == nullptr: geometry with baked vertex colours (D3DRS_LIGHTING off).
        const auto draw = [&](const GpuMesh &mesh, const fx::LightingMaterial *material) {
            if (!bgfx::isValid(mesh.vertices))
                return;
            // Baked geometry has no material of its own; its relief strength is that of stone.
            const fx::LightingUniforms uniforms =
                fx::ComputeLighting(lighting, material ? *material : fx::LightingMaterial::Stone(), &sky_state, settings);
            const float light[3][4] = {
                {uniforms.u_fxLight[0][0], uniforms.u_fxLight[0][1], uniforms.u_fxLight[0][2], material ? 1.0f : 0.0f},
                {lighting.sun_diffuse[0], lighting.sun_diffuse[1], lighting.sun_diffuse[2], mesh.base_size},
                {lighting.ambient[0], lighting.ambient[1], lighting.ambient[2], renderer.alpha_to_coverage ? 1.0f : 0.0f}};
            bgfx::setUniform(u_light_, light, 3);
            lighting_.Apply(uniforms);
            if (settings.lighting_flags & fx::kLightingAerial)
                sky_.BindAerial(lighting.view);
            if (settings.lighting_flags & fx::kLightingWind)
                sky_.BindWind();
            if (per_pixel_points)
            {
                // Lit geometry: the strongest lights around it. Baked geometry: only lights that are not baked in.
                const fx::LightSelection selection =
                    fx::SelectLights(lights, mesh.center, mesh.radius, fx::PointLightBudget(settings), material != nullptr);
                lighting_.Apply(fx::PackPointLights(selection, lighting.view));
            }
            else
            {
                // Legacy: lit geometry gets the engine's seven nearest lights per vertex, baked geometry none.
                const fx::LightSelection selection =
                    material ? fx::SelectLights(lights, mesh.center, mesh.radius, 7) : fx::LightSelection{};
                const fx::PointLightUniforms packed = fx::PackPointLights(selection, lighting.view);
                bgfx::setUniform(u_point_, packed.u_fxPoint.data(), fx::kMaxPointLights * 4);
                bgfx::setUniform(u_point_count_, packed.u_fxPointCount.data());
            }
            if (settings.shadows != fx::ShadowQuality::Off && (settings.lighting_flags & fx::kLightingPerPixel))
                shadows_.Bind(lighting.view);
            // Renderer state from fx: anisotropic filtering on the world textures, alpha to coverage on alpha-tested ones.
            const uint32_t sampler = renderer.max_anisotropy > 1 ? BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC : BGFX_SAMPLER_NONE;
            bgfx::setTexture(0, s_base_, bgfx::isValid(mesh.base) ? mesh.base : white_base_, sampler);
            bgfx::setTexture(1, s_detail_, bgfx::isValid(mesh.detail) ? mesh.detail : neutral_detail_, sampler);
            // The game selects a normal-mapped variant only for materials with a height texture; this tool has
            // one variant per shot and gives the other meshes a flat map instead.
            if (settings.lighting_flags & fx::kLightingNormalMap)
                lighting_.BindNormalMap(bgfx::isValid(mesh.normal_map) ? mesh.normal_map : flat_normal_map_, sampler);
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                           (renderer.alpha_to_coverage && bgfx::isValid(mesh.base) ? BGFX_STATE_BLEND_ALPHA_TO_COVERAGE : 0));
            bgfx::submit(kSceneView, programs_[variant]);
        };
        const fx::LightingMaterial hull = fx::LightingMaterial::Hull(), sail = fx::LightingMaterial::Sail(),
                                   character = fx::LightingMaterial::Character(), stone = fx::LightingMaterial::Stone(), matte{};

        const bool has_sea = shot.content != Content::Pattern && shot.content != Content::Materials && shot.content != Content::Garden;
        if (shot.content == Content::Ocean || shot.content == Content::Shore)
        {
            draw(island_, &matte);
            draw(ship_hull_, &hull);
            draw(ship_sails_, &sail);
        }
        else if (shot.content == Content::Deck)
        {
            draw(quay_, &matte);
            draw(figure_, &character);
            draw(moored_hull_, &hull);
            draw(moored_sails_, &sail);
        }
        else if (shot.content == Content::Harbour)
        {
            draw(town_, nullptr);
            draw(town_glow_, nullptr);
            draw(harbour_figure_, &character);
            draw(harbour_hull_, &hull);
            draw(harbour_sails_, &sail);
        }
        else if (shot.content == Content::Garden)
        {
            draw(garden_ground_, &stone);
            draw(garden_trunks_, &hull);
            draw(garden_foliage_, &sail);
            draw(garden_ropes_, &matte);
        }
        else if (shot.content == Content::Materials)
        {
            draw(relief_[0], nullptr);
            draw(relief_[1], &stone);
            draw(relief_[2], &hull);
            draw(relief_[3], &sail);
        }
        else if (shot.content == Content::Broadside)
        {
            draw(firing_hull_, &hull);
            draw(firing_sails_, &sail);
            draw(muzzle_fire_, nullptr);
        }
        else
        {
            draw(pattern_, nullptr);
        }

        if (shot.blobs)
        {
            // Under the quay's figure, crate and ball; the renderer would add one per character and object.
            contact_shadows_.Add({-0.6f, 2.4f, 4.2f}, 0.5f, 0.5f);
            contact_shadows_.Add({-6.0f, 2.4f, 8.0f}, 1.5f, 0.45f);
            contact_shadows_.Add({4.5f, 2.4f, 7.5f}, 0.9f, 0.5f);
        }
        contact_shadows_.Submit(kSceneView, settings);

        if (shot.content != Content::Pattern)
        {
            if (settings.sky_atmosphere)
            {
                sky_.Submit(kSceneView, settings, view, proj);
            }
            else
            {
                BuildLegacySky(weather, eye);
                draw(legacy_sky_, nullptr);
            }
        }

        if (has_sea)
        {
            fx::Vec3 water_color = shot.weather == preview::Weather::Storm ? fx::Vec3{0.24f, 0.28f, 0.31f} : fx::Vec3{0.04f, 0.22f, 0.39f};
            if (night)
                water_color = {0.02f, 0.06f, 0.12f};
            BuildLegacySea(water_color, weather, eye);
            draw(legacy_sea_, nullptr);
        }

        // Post chain into the back buffer.
        fx::PostChain::Frame post;
        post.scene_color = bgfx::getTexture(scene_fb_, 0);
        post.scene_width = scene_w;
        post.scene_height = scene_h;
        post.target = output_fb_;
        post.scene_depth = bgfx::getTexture(scene_fb_, 1);
        post.near_plane = kNear;
        post.far_plane = kFar;
        post.projection_scale = {proj[0], proj[5]};
        post.sun_color = weather.sun_color;
        post.scene_grade = fx::SceneGrade(sky_state, weather.overcast, shot.interior);
        post.scene_cut = true; // every shot is a new scene
        post.target_width = kWidth;
        post.target_height = kHeight;
        post.first_view = kPostView;
        float view_proj[16];
        bx::mtxMul(view_proj, view, proj);
        const bx::Vec3 sun_clip = bx::mulH({eye.x + weather.sun_dir[0] * 1000.0f, eye.y + weather.sun_dir[1] * 1000.0f,
                                            eye.z + weather.sun_dir[2] * 1000.0f},
                                           view_proj);
        const bx::Vec3 forward = bx::normalize(bx::sub(at, eye));
        const float facing = forward.x * weather.sun_dir[0] + forward.y * weather.sun_dir[1] + forward.z * weather.sun_dir[2];
        post.sun_uv = {0.5f + 0.5f * sun_clip.x, 0.5f - 0.5f * sun_clip.y};
        post.sun_visibility = shot.content == Content::Pattern || night
                                  ? 0.0f
                                  : std::clamp(facing * 2.0f, 0.0f, 1.0f) * std::clamp(weather.sun_dir[1] * 12.0f + 0.5f, 0.0f, 1.0f) *
                                        (1.0f - weather.overcast);
        const uint16_t used = post_.Submit(settings, post);
        bgfx::setViewName(kReadbackView, "readback");
        bgfx::setViewMode(kReadbackView, bgfx::ViewMode::Sequential);
        bgfx::touch(kReadbackView);
        bgfx::TextureRegion from, to;
        from.init(bgfx::getTexture(output_fb_));
        to.init(readback_);
        bgfx::blit(kReadbackView, to, from);
        std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4);
        const uint32_t ready = bgfx::read(to, pixels.data());
        while (bgfx::frame() < ready)
        {
        }
        // Read-back rows start at the texture origin: the bottom row on GL.
        const bool bottom_up = bgfx::getCaps()->originBottomLeft;
        WritePng(out_dir_ + "/" + shot.name + ".png", kWidth, kHeight, kWidth * 4, pixels.data(), bimg::TextureFormat::RGBA8, bottom_up);
        std::printf("%-34s %ux%u -> %ux%u, %u post views, %d point lights over budget\n", shot.name.c_str(), scene_w, scene_h, kWidth,
                    kHeight, used, lighting_.TakeDroppedLights());
    }

  private:
    static constexpr float kTime = 12.5f;

    void EnsureSceneTarget(uint16_t width, uint16_t height, uint8_t samples)
    {
        if (bgfx::isValid(scene_fb_) && width == scene_w_ && height == scene_h_ && samples == scene_samples_)
            return;
        if (bgfx::isValid(scene_fb_))
            bgfx::destroy(scene_fb_);
        // Multisampled targets resolve into these textures when the frame buffer is left.
        const uint64_t msaa = samples >= 4 ? BGFX_TEXTURE_RT_MSAA_X4 : samples >= 2 ? BGFX_TEXTURE_RT_MSAA_X2 : BGFX_TEXTURE_RT;
        const uint64_t flags = msaa | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        const bgfx::TextureHandle attachments[2] = {
            bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA8, flags),
            bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D24, flags | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT)};
        scene_fb_ = bgfx::createFrameBuffer(2, attachments, true);
        scene_w_ = width;
        scene_h_ = height;
        scene_samples_ = samples;
    }

    // Stand-in for the legacy Sea2 look (no fx): per-vertex Fresnel blend of water and sky colour, vertex fog.
    void BuildLegacySea(const fx::Vec3 &water_color, const fx::WeatherInput &weather, bx::Vec3 eye)
    {
        constexpr float kReflection = 0.7f; // Sea2.Reflection
        preview::Mesh mesh = preview::MakeSea({eye.x, eye.z}, kTime);
        const fx::Vec3 sky{0.62f * weather.sun_color[0] + 0.1f, 0.74f * weather.sun_color[1] + 0.1f, 0.86f * weather.sun_color[2] + 0.1f};
        for (preview::Vertex &v : mesh.vertices)
        {
            const float to_eye[3] = {eye.x - v.pos[0], eye.y - v.pos[1], eye.z - v.pos[2]};
            const float dist = std::sqrt(to_eye[0] * to_eye[0] + to_eye[1] * to_eye[1] + to_eye[2] * to_eye[2]);
            const float ndv = std::max((v.normal[0] * to_eye[0] + v.normal[1] * to_eye[1] + v.normal[2] * to_eye[2]) / dist, 0.0f);
            const float fresnel = std::clamp(std::pow(1.0f - ndv, 3.0f) * kReflection * 1.2f, 0.0f, 1.0f);
            const float fog = 1.0f - std::exp(-weather.fog_density * dist);
            uint32_t packed = 0xff000000u;
            for (int c = 0; c < 3; ++c)
            {
                float value = water_color[c] + (sky[c] - water_color[c]) * fresnel;
                value += (weather.fog_color[c] - value) * fog;
                packed |= static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)) << (8 * c);
            }
            v.abgr = packed;
        }
        legacy_sea_.Set(mesh);
    }

    // Stand-in for the legacy textured sky box: a plain vertical gradient towards the fog colour.
    void BuildLegacySky(const fx::WeatherInput &weather, bx::Vec3 eye)
    {
        preview::Mesh dome = preview::MakeSphere({eye.x, eye.y, eye.z}, 3000.0f, {1.0f, 1.0f, 1.0f}, 24);
        const fx::Vec3 zenith{0.30f * weather.sun_color[0] + 0.05f, 0.52f * weather.sun_color[1] + 0.06f, 0.86f * weather.sun_color[2] + 0.08f};
        for (preview::Vertex &v : dome.vertices)
        {
            const float up = std::clamp((v.pos[1] - eye.y) / 3000.0f, 0.0f, 1.0f);
            const float t = std::pow(up, 0.45f);
            uint32_t packed = 0xff000000u;
            for (int c = 0; c < 3; ++c)
            {
                const float value = weather.fog_color[c] + (zenith[c] - weather.fog_color[c]) * t;
                packed |= static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)) << (8 * c);
            }
            v.abgr = packed;
        }
        legacy_sky_.Set(dome);
    }

    std::string out_dir_;
    fx::PostChain post_;
    fx::Sky sky_;
    fx::Lighting lighting_;
    fx::Shadows shadows_;
    fx::ContactShadows contact_shadows_;
    fx::FlashLights flashes_;
    std::vector<fx::PointLight> town_lights_;
    std::vector<bgfx::ProgramHandle> programs_; // parallel to kMeshVariants
    bgfx::UniformHandle u_light_, u_point_, u_point_count_, s_detail_, s_base_;
    bgfx::TextureHandle white_base_ = BGFX_INVALID_HANDLE, frond_texture_ = BGFX_INVALID_HANDLE, rope_texture_ = BGFX_INVALID_HANDLE;
    GpuMesh garden_ground_, garden_trunks_, garden_foliage_, garden_ropes_;
    bgfx::TextureHandle neutral_detail_ = BGFX_INVALID_HANDLE, flat_normal_map_ = BGFX_INVALID_HANDLE;
    std::array<preview::Mesh, 4> relief_cpu_;
    std::array<GpuMesh, 4> relief_;
    std::array<bgfx::TextureHandle, 4> relief_detail_{}, relief_normal_{};
    bgfx::FrameBufferHandle scene_fb_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle output_fb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_ = BGFX_INVALID_HANDLE;
    uint16_t scene_w_ = 0, scene_h_ = 0;
    uint8_t scene_samples_ = 0;
    GpuMesh ship_hull_, ship_sails_, island_, quay_, figure_, moored_hull_, moored_sails_, town_, town_glow_, harbour_figure_,
        harbour_hull_, harbour_sails_, firing_hull_, firing_sails_, muzzle_fire_, pattern_, legacy_sky_, legacy_sea_;
};

// CPU-side products as images: baked textures, sky panoramas, grading LUT strips.
void WriteCpuImages(const std::string &dir)
{
    const auto ms = [](auto &&work) {
        const auto start = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    fx::BakedTexture clouds;
    const double cloud_ms = ms([&] { clouds = fx::BakeCloudNoise(); });
    const auto channel = [&](const fx::BakedTexture &texture, int c, const char *name) {
        std::vector<uint8_t> grey(static_cast<size_t>(texture.size) * texture.size * 4);
        for (int i = 0; i < texture.size * texture.size; ++i)
        {
            grey[4 * i] = grey[4 * i + 1] = grey[4 * i + 2] = texture.rgba[4 * i + c];
            grey[4 * i + 3] = 255;
        }
        WritePng(dir + "/" + name + ".png", texture.size, texture.size, texture.size * 4, grey.data(), bimg::TextureFormat::RGBA8, false);
    };
    channel(clouds, 0, "tex_cloud_coverage");
    channel(clouds, 1, "tex_cloud_billows");

    double sky_ms = 0.0;
    const std::pair<preview::Weather, const char *> weathers[] = {{preview::Weather::Day, "day"},
                                                                  {preview::Weather::Sunset, "sunset"},
                                                                  {preview::Weather::Storm, "storm"},
                                                                  {preview::Weather::Night, "night"}};
    for (const auto &[weather, name] : weathers)
    {
        fx::SkyState sky;
        sky_ms = std::max(sky_ms, ms([&] { sky = fx::ComputeSky(preview::MakeWeather(weather)); }));
        // Row 0 of the panorama is the horizon: write it bottom-up.
        WritePng(dir + "/tex_sky_panorama_" + name + ".png", fx::kSkyLutWidth, fx::kSkyLutHeight, fx::kSkyLutWidth * 4, sky.lut.data(),
                 bimg::TextureFormat::RGBA8, true);
    }

    // Grading LUTs as the usual strip of blue slices.
    const std::pair<fx::GradePreset, const char *> grades[] = {{fx::GradePreset::Original, "original"},
                                                               {fx::GradePreset::Caribbean, "caribbean"},
                                                               {fx::GradePreset::Overcast, "overcast"}};
    double lut_ms = 0.0;
    for (const auto &[grade, name] : grades)
    {
        const int n = fx::kDefaultLutSize;
        std::vector<uint8_t> lut;
        lut_ms = std::max(lut_ms, ms([&] { lut = fx::BuildLutRgba8(fx::ColorGrade::Preset(grade), n); }));
        std::vector<uint8_t> strip(static_cast<size_t>(n) * n * n * 4);
        for (int b = 0; b < n; ++b)
            for (int g = 0; g < n; ++g)
                std::memcpy(&strip[(static_cast<size_t>(g) * n * n + static_cast<size_t>(b) * n) * 4],
                            &lut[(static_cast<size_t>(b) * n + g) * n * 4], static_cast<size_t>(n) * 4);
        WritePng(dir + "/lut_" + name + ".png", n * n, n, n * n * 4, strip.data(), bimg::TextureFormat::RGBA8, false);
    }
    std::printf("CPU cost on this host: cloud bake %.1f ms, sky panorama %.2f ms, grading LUT %.2f ms\n", cloud_ms, sky_ms, lut_ms);
}

std::vector<Shot> AllShots(const fx::Support &support, bool game_height_maps)
{
    using fx::QualityPreset;
    const fx::Settings original = fx::Settings::FromPreset(QualityPreset::Low);
    // The presets as this device can run them: what it lacks is taken out here, once, and said so.
    std::vector<std::string> changes;
    const fx::Settings medium = fx::ConstrainSettings(fx::Settings::FromPreset(QualityPreset::Medium), support, changes);
    for (const std::string &change : changes)
        std::printf("preset Medium on this device: %s\n", change.c_str());
    changes.clear();
    const fx::Settings high = fx::ConstrainSettings(fx::Settings::FromPreset(QualityPreset::High), support, changes);
    for (const std::string &change : changes)
        std::printf("preset High on this device: %s\n", change.c_str());
    // High with the scene depth kept readable instead of MSAA (FXAA takes over): the choice for depth-based
    // effects on renderers that cannot resolve the depth of a multisampled scene.
    fx::Settings depth_high = fx::Settings::FromPreset(QualityPreset::High);
    depth_high.msaa = 1;
    depth_high.alpha_to_coverage = false;
    depth_high.fxaa = true;
    // The High preset before the cheap upgrades: the "before" of their comparisons, one upgrade added at a time.
    fx::Settings basic = depth_high;
    basic.lighting_flags &= ~static_cast<uint32_t>(fx::kLightingRim | fx::kLightingLinear | fx::kLightingAerial | fx::kLightingWind);
    basic.msaa = 1;
    basic.alpha_to_coverage = false;
    basic.anisotropy = 1;
    basic.fxaa = true;
    basic.dither = basic.lens_flare = basic.scene_grading = basic.contact_shadows = false;
    std::vector<Shot> shots;
    const auto add = [&](std::string name, Content content, fx::Settings settings, preview::Weather weather = preview::Weather::Day) -> Shot & {
        shots.push_back({std::move(name), content, settings, weather});
        // The quay is lit from behind the camera's left shoulder; the sea scenes look towards the sun.
        shots.back().sun_turn = content == Content::Deck ? 215.0f : 0.0f;
        return shots.back();
    };

    // Quality presets on the main scene, in four weathers.
    const std::pair<preview::Weather, const char *> weathers[] = {{preview::Weather::Day, "day"},
                                                                  {preview::Weather::Sunset, "sunset"},
                                                                  {preview::Weather::Storm, "storm"},
                                                                  {preview::Weather::Night, "night"}};
    for (const auto &[weather, name] : weathers)
    {
        add(std::string("preset_") + name + "_1_original", Content::Ocean, original, weather);
        add(std::string("preset_") + name + "_2_medium", Content::Ocean, medium, weather);
        add(std::string("preset_") + name + "_3_high", Content::Ocean, high, weather);
    }

    // Colour grades on the High preset.
    fx::Settings graded = high;
    graded.grade = fx::GradePreset::Caribbean;
    add("grade_caribbean", Content::Ocean, graded);
    graded.grade = fx::GradePreset::Overcast;
    add("grade_overcast", Content::Ocean, graded);
    add("grade_overcast_storm", Content::Ocean, graded, preview::Weather::Storm);

    // Tone curve and bloom alone, on the legacy scene.
    fx::Settings one = original;
    one.tonemap = true;
    add("tonemap_on", Content::Ocean, one, preview::Weather::Sunset);
    one = original;
    one.bloom = true;
    add("bloom_on_legacy_scene", Content::Ocean, one, preview::Weather::Sunset);

    // Bloom and sun shafts.
    fx::Settings no_bloom = high;
    no_bloom.bloom = false;
    no_bloom.sun_shafts = false;
    add("bloom_sunset_off", Content::Ocean, no_bloom, preview::Weather::Sunset);
    fx::Settings no_shafts = high;
    no_shafts.sun_shafts = false;
    add("bloom_sunset_on", Content::Ocean, no_shafts, preview::Weather::Sunset);
    add("bloom_sunset_on_shafts", Content::Ocean, high, preview::Weather::Sunset);

    // Point lights: a night quay lit by torches and lanterns (baked town, dynamically lit figure and ship),
    // and a broadside at dusk lit by muzzle flashes.
    add("harbour_night_1_original", Content::Harbour, original, preview::Weather::Night);
    add("harbour_night_2_medium", Content::Harbour, medium, preview::Weather::Night);
    add("harbour_night_3_high", Content::Harbour, high, preview::Weather::Night);
    add("harbour_night_3_high_no_bloom", Content::Harbour, no_bloom, preview::Weather::Night);
    add("broadside_dusk_1_original", Content::Broadside, original, preview::Weather::Sunset);
    add("broadside_dusk_2_medium", Content::Broadside, medium, preview::Weather::Sunset);
    add("broadside_dusk_3_high", Content::Broadside, high, preview::Weather::Sunset);

    // Sky without clouds.
    fx::Settings clear = high;
    clear.sky_clouds = false;
    add("sky_clear", Content::Ocean, clear);

    // Sun lighting flags, without shadows and occlusion so only the shading changes.
    fx::Settings lit = basic;
    lit.shadows = fx::ShadowQuality::Off;
    lit.ssao = false;
    lit.lighting_flags = fx::kLightingNone;
    add("lighting_0_per_vertex", Content::Deck, lit);
    add("lighting_sunset_0_per_vertex", Content::Deck, lit, preview::Weather::Sunset);
    lit.lighting_flags = fx::kLightingPerPixel | fx::kLightingHemisphere;
    add("lighting_1_per_pixel_hemisphere", Content::Deck, lit);
    lit.lighting_flags |= fx::kLightingSpecular | fx::kLightingTranslucent;
    add("lighting_2_specular_translucent", Content::Deck, lit);
    add("lighting_sunset_2_full", Content::Deck, lit, preview::Weather::Sunset);

    // Sun shadows and ambient occlusion on the quay.
    fx::Settings shadowed = basic;
    shadowed.ssao = false;
    shadowed.shadows = fx::ShadowQuality::Off;
    add("shadows_0_off", Content::Deck, shadowed);
    shadowed.shadows = fx::ShadowQuality::Low;
    add("shadows_1_low", Content::Deck, shadowed);
    shadowed.shadows = fx::ShadowQuality::Soft;
    add("shadows_2_soft", Content::Deck, shadowed);
    add("ssao_0_off", Content::Deck, shadowed);
    add("ssao_1_on", Content::Deck, basic);

    // ---- the cheap upgrades, each against `basic` ----
    // Anisotropic filtering: the plank deck at a grazing angle.
    add("aniso_0_off", Content::Materials, basic).sun_turn = 240.0f;
    fx::Settings one_more = basic;
    one_more.anisotropy = 8;
    add("aniso_1_on", Content::Materials, one_more).sun_turn = 240.0f;
    // MSAA with alpha to coverage on foliage and rigging; wind sway at two moments.
    one_more = basic;
    one_more.fxaa = false;
    one_more.ssao = one_more.sea_soft_edges = false; // depth-based effects and MSAA exclude each other on this renderer
    add("a2c_0_no_aa_alpha_test", Content::Garden, one_more).sun_turn = 215.0f;
    one_more.msaa = 4;
    add("a2c_1_msaa_alpha_test", Content::Garden, one_more).sun_turn = 215.0f;
    one_more.alpha_to_coverage = true;
    add("a2c_2_msaa_alpha_to_coverage", Content::Garden, one_more).sun_turn = 215.0f;
    one_more = basic;
    one_more.lighting_flags |= fx::kLightingWind;
    add("wind_0_off", Content::Garden, basic).sun_turn = 215.0f;
    add("wind_1_on", Content::Garden, one_more).sun_turn = 215.0f;
    {
        Shot &later = add("wind_2_on_later", Content::Garden, one_more);
        later.sun_turn = 215.0f;
        later.time = 14.1f;
    }
    // Lights added in linear light: the torch-lit wall and the harbour.
    one_more = basic;
    one_more.lighting_flags |= fx::kLightingLinear;
    add("linear_torch_0_off", Content::Materials, basic, preview::Weather::Night);
    add("linear_torch_1_on", Content::Materials, one_more, preview::Weather::Night);
    add("linear_day_0_off", Content::Deck, basic);
    add("linear_day_1_on", Content::Deck, one_more);
    // Rim light: the quay seen against the sun.
    one_more = basic;
    one_more.lighting_flags |= fx::kLightingRim;
    add("rim_0_off", Content::Deck, basic).sun_turn = 20.0f;
    add("rim_1_on", Content::Deck, one_more).sun_turn = 20.0f;
    // Aerial perspective and height haze: the island across the water.
    one_more = basic;
    one_more.lighting_flags |= fx::kLightingAerial;
    add("aerial_0_off", Content::Ocean, basic);
    add("aerial_1_on", Content::Ocean, one_more);
    add("aerial_storm_0_off", Content::Ocean, basic, preview::Weather::Storm);
    add("aerial_storm_1_on", Content::Ocean, one_more, preview::Weather::Storm);
    // Contact shadows where there is no sun shadow: an overcast quay.
    one_more = basic;
    one_more.shadows = fx::ShadowQuality::Off;
    add("contact_0_off", Content::Deck, one_more, preview::Weather::Storm).blobs = true;
    one_more.contact_shadows = true;
    add("contact_1_on", Content::Deck, one_more, preview::Weather::Storm).blobs = true;
    // Scene grades by hour, weather and place.
    one_more = basic;
    one_more.scene_grading = true;
    for (const auto &[weather, name] : weathers)
    {
        add(std::string("scenegrade_") + name + "_0_off", Content::Ocean, basic, weather);
        add(std::string("scenegrade_") + name + "_1_on", Content::Ocean, one_more, weather);
    }
    add("scenegrade_interior_0_off", Content::Materials, basic, preview::Weather::Night);
    add("scenegrade_interior_1_on", Content::Materials, one_more, preview::Weather::Night).interior = 1.0f;
    // Sun glare and lens flare.
    one_more = basic;
    one_more.lens_flare = true;
    add("flare_0_off", Content::Ocean, basic, preview::Weather::Sunset);
    add("flare_1_on", Content::Ocean, one_more, preview::Weather::Sunset);
    add("flare_day_1_on", Content::Shore, one_more).sun_turn = 215.0f;
    // Dither against banding in the night sky.
    one_more = basic;
    one_more.dither = true;
    add("dither_0_off", Content::Ocean, basic, preview::Weather::Night);
    add("dither_1_on", Content::Ocean, one_more, preview::Weather::Night);

    // Normal maps derived from height textures: the sun in two positions, then a torch at night.
    fx::Settings flat = basic;
    flat.lighting_flags &= ~static_cast<uint32_t>(fx::kLightingNormalMap);
    const std::string suffix = game_height_maps ? "_game" : "";
    const std::tuple<const char *, preview::Weather, float> relief_lights[] = {
        {"sun_left", preview::Weather::Day, 240.0f}, {"sun_right", preview::Weather::Day, 130.0f}, {"torch", preview::Weather::Night, 0.0f}};
    for (const auto &[light, weather, turn] : relief_lights)
    {
        add(std::string("normalmap_") + light + "_0_off" + suffix, Content::Materials, flat, weather).sun_turn = turn;
        add(std::string("normalmap_") + light + "_1_medium" + suffix, Content::Materials, medium, weather).sun_turn = turn;
        add(std::string("normalmap_") + light + "_2_high" + suffix, Content::Materials, basic, weather).sun_turn = turn;
    }

    // Anti-aliasing, sharpening and a 75 % render scale on the test chart.
    add("aa_0_off", Content::Pattern, original);
    fx::Settings aa = original;
    aa.fxaa = true;
    add("aa_1_fxaa", Content::Pattern, aa);
    aa.sharpen = true;
    add("aa_2_fxaa_sharpen", Content::Pattern, aa);
    fx::Settings msaa = original;
    msaa.msaa = 4;
    add("aa_3_msaa4", Content::Pattern, msaa);
    add("aa_scale75_0_bilinear", Content::Pattern, original).render_scale = 0.75f;
    add("aa_scale75_1_fxaa_sharpen", Content::Pattern, aa).render_scale = 0.75f;
    add("dynres_day_scale75_high", Content::Ocean, high).render_scale = 0.75f;
    return shots;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: fx_preview <output directory> [name filter] [--height-maps=<folder with .pgm files>]\n");
        return 2;
    }
    const std::string out_dir = argv[1];
    std::string filter, height_map_dir;
    for (int i = 2; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument.rfind("--height-maps=", 0) == 0)
            height_map_dir = argument.substr(14);
        else
            filter = argument;
    }
    std::filesystem::create_directories(out_dir);

    bgfx::Init init;
    init.type = bgfx::RendererType::OpenGLES;
    // No native window and no back buffer size: bgfx creates a 1x1 EGL pbuffer.
    init.swapChain.width = 0;
    init.swapChain.height = 0;
    init.reset = BGFX_RESET_MAXANISOTROPY; // anisotropic samplers use the device's maximum level
    init.fallback = false; // never fall back to the no-op renderer: black images would look like a pass
    if (!bgfx::init(init))
    {
        std::printf("bgfx::init failed (is an EGL implementation available?)\n");
        return 1;
    }
    // Every error the package reports fails the run: the shots must render exactly what their settings say.
    static int errors = 0;
    fx::SetLogSink([](fx::LogLevel level, const char *message) {
        std::printf("[fx] %s%s\n", level == fx::LogLevel::Error ? "ERROR: " : "", message);
        errors += level == fx::LogLevel::Error;
    });
    // What this tool, as the renderer, provides: a sampleable D24 scene depth, but not from a multisampled scene
    // (bgfx resolves colour only), and anisotropic samplers.
    const fx::Support support = fx::DetectSupport({true, false, true});
    if (filter.empty())
        WriteCpuImages(out_dir);
    {
        Preview preview(out_dir, height_map_dir);
        for (Shot shot : AllShots(support, !height_map_dir.empty()))
        {
            if (!filter.empty() && shot.name.find(filter) == std::string::npos)
                continue;
            std::vector<std::string> changes;
            shot.settings = fx::ConstrainSettings(shot.settings, support, changes);
            for (const std::string &change : changes)
            {
                std::printf("%s: %s\n", shot.name.c_str(), change.c_str());
                ++errors; // on this host nothing should have to change
            }
            preview.Render(shot);
        }
    }
    bgfx::shutdown();
    if (errors)
        std::printf("%d error(s)\n", errors);
    return errors ? 1 : 0;
}
