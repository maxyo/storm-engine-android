// fx_preview: renders the test scenes of the graphics effects package with
// bgfx on a headless OpenGL ES 3.0 context (EGL pbuffer; Mesa llvmpipe is
// enough) and writes PNG files for visual review. See README.md.
#include "scene.hpp"

#include "storm/fx/color_lut.hpp"
#include "storm/fx/lighting.hpp"
#include "storm/fx/noise_bake.hpp"
#include "storm/fx/post_chain.hpp"
#include "storm/fx/sky.hpp"
#include "storm/fx/water.hpp"

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
#include <string>
#include <vector>

#include "preview_shaders/includes.inc"

using namespace storm;

namespace
{
constexpr uint16_t kWidth = 1280;
constexpr uint16_t kHeight = 720;
constexpr float kNear = 0.5f;
constexpr float kFar = 4000.0f;
constexpr bgfx::ViewId kSceneView = 0;
constexpr bgfx::ViewId kCaptureView = 1;
constexpr bgfx::ViewId kWaterView = 2;
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
            .end();
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
    Deck,     // ship and a figure close up: lighting
    Harbour,  // night scene with lanterns: bloom
    Pattern,  // aliasing test chart
};

enum class WaterSource
{
    Field,
    Mesh,
    Gerstner
};

struct Shot
{
    std::string name;
    Content content = Content::Ocean;
    fx::Settings settings;
    preview::Weather weather = preview::Weather::Day;
    float render_scale = 1.0f;
    WaterSource source = WaterSource::Field;
    bool env_cube = false;
};

bgfx::ProgramHandle LoadPreviewProgram(const uint8_t *vs, uint32_t vs_size, const uint8_t *fs, uint32_t fs_size)
{
    return bgfx::createProgram(bgfx::createShader(bgfx::makeRef(vs, vs_size)), bgfx::createShader(bgfx::makeRef(fs, fs_size)), true);
}

class Preview
{
  public:
    explicit Preview(std::string out_dir) : out_dir_(std::move(out_dir))
    {
        legacy_ = LoadPreviewProgram(vs_preview_mesh, sizeof(vs_preview_mesh), fs_preview_mesh, sizeof(fs_preview_mesh));
        per_pixel_ = LoadPreviewProgram(vs_preview_mesh_pp, sizeof(vs_preview_mesh_pp), fs_preview_mesh_pp, sizeof(fs_preview_mesh_pp));
        full_ = LoadPreviewProgram(vs_preview_mesh_pp, sizeof(vs_preview_mesh_pp), fs_preview_mesh_full, sizeof(fs_preview_mesh_full));
        u_light_ = bgfx::createUniform("u_previewLight", bgfx::UniformType::Vec4, 3);
        // bgfx without a window has no back buffer: the final image goes to a frame buffer and is read back.
        output_fb_ = bgfx::createFrameBuffer(kWidth, kHeight, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        readback_ = bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8,
                                          BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        preview::FillWaveField(field_, kTime);
        sea_ = preview::MakeSeaState(kTime);
        const float ship_y = field_.Height(sea_, 30.0f, 95.0f) - 0.4f;
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
        // Harbour: dark buildings with lit windows and lanterns.
        preview::Mesh town = preview::MakeGround(120.0f, 0.0f, {0.20f, 0.19f, 0.18f});
        preview::Mesh lights;
        for (int i = 0; i < 7; ++i)
        {
            const float x = -42.0f + 14.0f * i, depth = 34.0f + 6.0f * (i % 3), height = 5.0f + 2.5f * (i % 4);
            town.Append(preview::MakeBox({x, height, depth}, {5.0f, height, 4.0f}, {0.33f, 0.30f, 0.27f}));
            for (int w = 0; w < 2 + i % 2; ++w)
                lights.Append(preview::MakeBox({x - 2.4f + 2.4f * w, 3.0f + 3.2f * (w % 2), depth - 4.02f}, {0.55f, 0.8f, 0.04f},
                                               {1.0f, 0.80f, 0.42f}));
            lights.Append(preview::MakeSphere({x + 6.5f, 4.2f, depth - 9.0f}, 0.28f, {1.0f, 0.90f, 0.62f}, 8));
            town.Append(preview::MakeBox({x + 6.5f, 2.0f, depth - 9.0f}, {0.08f, 2.0f, 0.08f}, {0.12f, 0.11f, 0.10f}));
        }
        town_.Set(town);
        town_lights_.Set(lights);
        pattern_.Set(preview::MakeAliasingPattern());
    }

    ~Preview()
    {
        for (GpuMesh *mesh : {&ship_hull_, &ship_sails_, &island_, &quay_, &figure_, &moored_hull_, &moored_sails_, &town_, &town_lights_,
                              &pattern_, &legacy_sky_, &legacy_sea_})
            mesh->Reset();
        for (bgfx::ProgramHandle program : {legacy_, per_pixel_, full_})
            bgfx::destroy(program);
        bgfx::destroy(u_light_);
        bgfx::destroy(output_fb_);
        bgfx::destroy(readback_);
        if (bgfx::isValid(scene_fb_))
            bgfx::destroy(scene_fb_);
        if (bgfx::isValid(sea_vertices_))
            bgfx::destroy(sea_vertices_);
        if (bgfx::isValid(sea_indices_))
            bgfx::destroy(sea_indices_);
        if (bgfx::isValid(env_cube_))
            bgfx::destroy(env_cube_);
    }

    // Renders one shot to <out_dir>/<name>.png and returns its pixels (RGBA).
    std::vector<uint8_t> Render(const Shot &shot)
    {
        const fx::Settings &settings = shot.settings;
        const auto scene_w = static_cast<uint16_t>(std::lround(kWidth * shot.render_scale));
        const auto scene_h = static_cast<uint16_t>(std::lround(kHeight * shot.render_scale));
        EnsureSceneTarget(scene_w, scene_h);

        // The lighting scene is lit from behind the camera's left shoulder; the sea scenes look towards the sun.
        const fx::WeatherInput weather = preview::MakeWeather(shot.weather, shot.content == Content::Deck ? 215.0f : 0.0f);
        sky_.Update(weather, kTime);
        const fx::SkyState &sky_state = sky_.State();

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
            eye = {0.0f, 2.2f, -8.0f};
            at = {0.0f, 5.0f, 40.0f};
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
        const auto draw = [&](const GpuMesh &mesh, const fx::LightingMaterial *material) {
            if (!bgfx::isValid(mesh.vertices))
                return;
            const uint32_t flags = material ? settings.lighting_flags : 0u;
            bgfx::ProgramHandle program = legacy_;
            if (flags & fx::kLightingPerPixel)
                program = flags & (fx::kLightingSpecular | fx::kLightingTranslucent) ? full_ : per_pixel_;
            const fx::LightingUniforms uniforms = fx::ComputeLighting(lighting, material ? *material : fx::LightingMaterial{}, &sky_state);
            const float light[3][4] = {
                {uniforms.u_fxLight[0][0], uniforms.u_fxLight[0][1], uniforms.u_fxLight[0][2], material ? 1.0f : 0.0f},
                {lighting.sun_diffuse[0], lighting.sun_diffuse[1], lighting.sun_diffuse[2], 0.0f},
                {lighting.ambient[0], lighting.ambient[1], lighting.ambient[2], 0.0f}};
            bgfx::setUniform(u_light_, light, 3);
            lighting_.Apply(uniforms);
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
            bgfx::submit(kSceneView, program);
        };
        const fx::LightingMaterial hull = fx::LightingMaterial::Hull(), sail = fx::LightingMaterial::Sail(),
                                   character = fx::LightingMaterial::Character(), matte{};

        const bool has_sea = shot.content == Content::Ocean || shot.content == Content::Shore || shot.content == Content::Deck;
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
            draw(town_, &matte);
            draw(town_lights_, nullptr);
        }
        else
        {
            draw(pattern_, nullptr);
        }

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
            fx::WaterShading shading;
            shading.water_color = shot.weather == preview::Weather::Storm ? fx::Vec3{0.24f, 0.28f, 0.31f} : fx::Vec3{0.04f, 0.22f, 0.39f};
            if (night)
                shading.water_color = {0.02f, 0.06f, 0.12f};
            shading.foam = true;
            shading.foam_v = 2.35f;
            shading.foam_k = 1.1f;
            fx::WaterFrame frame;
            frame.view = kWaterView;
            frame.camera = {eye.x, eye.y, eye.z};
            frame.time = kTime;
            frame.crest_height = sea_.layers[0].amp + sea_.layers[1].amp;
            frame.far_plane = kFar;
            if (settings.water == fx::WaterQuality::Legacy)
            {
                BuildLegacySea(shading, weather, eye);
                draw(legacy_sea_, nullptr);
            }
            else
            {
                if (shot.env_cube)
                {
                    BuildEnvCube(sky_state);
                    frame.env_cube = env_cube_;
                }
                if (settings.water == fx::WaterQuality::High)
                    water_.CaptureScene(kCaptureView, bgfx::getTexture(scene_fb_, 0), bgfx::getTexture(scene_fb_, 1), scene_w, scene_h,
                                        kNear, kFar);
                bgfx::setViewName(kWaterView, "water");
                bgfx::setViewMode(kWaterView, bgfx::ViewMode::Sequential);
                bgfx::setViewFrameBuffer(kWaterView, scene_fb_);
                bgfx::setViewRect(kWaterView, 0, 0, scene_w, scene_h);
                bgfx::setViewClear(kWaterView, BGFX_CLEAR_NONE);
                bgfx::setViewTransform(kWaterView, view, proj);
                if (shot.source == WaterSource::Field)
                {
                    water_.UpdateField(field_);
                    water_.SubmitField(settings, frame, shading, sky_, sea_);
                }
                else if (shot.source == WaterSource::Gerstner)
                {
                    fx::GerstnerSource source;
                    source.amp = {sea_.layers[0].amp, sea_.layers[1].amp};
                    source.scale = {sea_.layers[0].scale, sea_.layers[1].scale};
                    source.move_speed = {{{4.0f, 0.0f}, {0.0f, 1.5f}}};
                    source.pos_shift = sea_.pos_shift;
                    water_.SubmitGerstner(settings, frame, shading, sky_, fx::GerstnerFromEngine(source), sea_.bump_scale);
                }
                else
                {
                    BuildEngineSea(eye);
                    const bgfx::ProgramHandle program = water_.PrepareMesh(settings, frame, shading, sky_, {eye.x, eye.z});
                    bgfx::setVertexBuffer(0, sea_vertices_);
                    bgfx::setIndexBuffer(sea_indices_);
                    bgfx::submit(kWaterView, program);
                }
            }
        }

        // Post chain into the back buffer.
        fx::PostChain::Frame post;
        post.scene_color = bgfx::getTexture(scene_fb_, 0);
        post.scene_width = scene_w;
        post.scene_height = scene_h;
        post.target = output_fb_;
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
        post.sun_visibility = shot.content == Content::Pattern || shot.content == Content::Harbour
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
        std::printf("%-34s %ux%u -> %ux%u, %u post views\n", shot.name.c_str(), scene_w, scene_h, kWidth, kHeight, used);
        return pixels;
    }

  private:
    static constexpr float kTime = 12.5f;

    void EnsureSceneTarget(uint16_t width, uint16_t height)
    {
        if (bgfx::isValid(scene_fb_) && width == scene_w_ && height == scene_h_)
            return;
        if (bgfx::isValid(scene_fb_))
            bgfx::destroy(scene_fb_);
        const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        const bgfx::TextureHandle attachments[2] = {
            bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA8, flags),
            bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D24, flags | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT)};
        scene_fb_ = bgfx::createFrameBuffer(2, attachments, true);
        scene_w_ = width;
        scene_h_ = height;
    }

    // The sea as the SEA entity builds it on the CPU: every vertex through EngineWaveField::Displace.
    void BuildEngineSea(bx::Vec3 eye)
    {
        const fx::WaterGridMesh grid = fx::BuildWaterGrid(96);
        const std::vector<fx::WaterGridLevel> levels = fx::PlaceWaterGrid(eye.x, eye.z, 0.5f, 9);
        std::vector<fx::SeaVertex> vertices;
        std::vector<uint32_t> indices;
        for (const fx::WaterGridLevel &level : levels)
        {
            const auto base = static_cast<uint32_t>(vertices.size());
            for (size_t i = 0; i < grid.vertices.size(); i += 3)
                vertices.push_back(field_.Displace(sea_, level.origin[0] + grid.vertices[i] * level.step,
                                                   level.origin[1] + grid.vertices[i + 1] * level.step, {eye.x, eye.z}));
            for (uint16_t index : level.ring < 0 ? grid.full : grid.rings[level.ring])
                indices.push_back(base + index);
        }
        cpu_sea_vertices_ = vertices;
        cpu_sea_indices_ = indices;
        if (bgfx::isValid(sea_vertices_))
            bgfx::destroy(sea_vertices_);
        if (bgfx::isValid(sea_indices_))
            bgfx::destroy(sea_indices_);
        sea_vertices_ = bgfx::createVertexBuffer(
            bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(fx::SeaVertex))), fx::Water::MeshLayout());
        sea_indices_ = bgfx::createIndexBuffer(bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * 4)), BGFX_BUFFER_INDEX32);
    }

    // Stand-in for the legacy Sea2 look (no fx): per-vertex Fresnel blend of water and sky colour, vertex fog.
    void BuildLegacySea(const fx::WaterShading &shading, const fx::WeatherInput &weather, bx::Vec3 eye)
    {
        BuildEngineSea(eye);
        preview::Mesh mesh;
        mesh.indices = cpu_sea_indices_;
        mesh.vertices.reserve(cpu_sea_vertices_.size());
        const fx::Vec3 sky{0.62f * weather.sun_color[0] + 0.1f, 0.74f * weather.sun_color[1] + 0.1f, 0.86f * weather.sun_color[2] + 0.1f};
        for (const fx::SeaVertex &v : cpu_sea_vertices_)
        {
            const float to_eye[3] = {-v.pos[0], eye.y - v.pos[1], -v.pos[2]};
            const float dist = std::sqrt(to_eye[0] * to_eye[0] + to_eye[1] * to_eye[1] + to_eye[2] * to_eye[2]);
            const float ndv = std::max((v.normal[0] * to_eye[0] + v.normal[1] * to_eye[1] + v.normal[2] * to_eye[2]) / dist, 0.0f);
            const float fresnel = std::clamp(std::pow(1.0f - ndv, 3.0f) * shading.reflection * 1.2f, 0.0f, 1.0f);
            const float fog = 1.0f - std::exp(-weather.fog_density * dist);
            preview::Vertex out{};
            uint32_t packed = 0xff000000u;
            for (int c = 0; c < 3; ++c)
            {
                out.pos[c] = v.pos[c] + (c == 0 ? eye.x : c == 2 ? eye.z : 0.0f);
                out.normal[c] = v.normal[c];
                float value = shading.water_color[c] + (sky[c] - shading.water_color[c]) * fresnel;
                value += (weather.fog_color[c] - value) * fog;
                packed |= static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)) << (8 * c);
            }
            out.abgr = packed;
            mesh.vertices.push_back(out);
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

    // Stand-in for the SEA entity's environment cube: the analytic sky rendered into six small faces on the CPU.
    void BuildEnvCube(const fx::SkyState &sky)
    {
        constexpr int kSize = 64;
        std::vector<uint8_t> texels(static_cast<size_t>(kSize) * kSize * 4 * 6);
        size_t o = 0;
        for (int face = 0; face < 6; ++face)
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                {
                    const float s = (x + 0.5f) / kSize * 2.0f - 1.0f, t = (y + 0.5f) / kSize * 2.0f - 1.0f;
                    const float dirs[6][3] = {{1, -t, -s}, {-1, -t, s}, {s, 1, t}, {s, -1, -t}, {s, -t, 1}, {-s, -t, -1}};
                    const float len = std::sqrt(dirs[face][0] * dirs[face][0] + dirs[face][1] * dirs[face][1] + dirs[face][2] * dirs[face][2]);
                    const fx::Vec3 c = fx::SkyColor(sky, {dirs[face][0] / len, dirs[face][1] / len, dirs[face][2] / len});
                    for (int i = 0; i < 3; ++i)
                        texels[o++] = static_cast<uint8_t>(std::lround(std::sqrt(std::clamp(c[i], 0.0f, 1.0f)) * 255.0f));
                    texels[o++] = 255;
                }
        if (bgfx::isValid(env_cube_))
            bgfx::destroy(env_cube_);
        env_cube_ = bgfx::createTextureCube(kSize, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                            bgfx::copy(texels.data(), static_cast<uint32_t>(texels.size())));
    }

    std::string out_dir_;
    fx::PostChain post_;
    fx::Water water_;
    fx::Sky sky_;
    fx::Lighting lighting_;
    fx::EngineWaveField field_;
    fx::EngineSeaState sea_;
    bgfx::ProgramHandle legacy_, per_pixel_, full_;
    bgfx::UniformHandle u_light_;
    bgfx::FrameBufferHandle scene_fb_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle output_fb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_ = BGFX_INVALID_HANDLE;
    uint16_t scene_w_ = 0, scene_h_ = 0;
    GpuMesh ship_hull_, ship_sails_, island_, quay_, figure_, moored_hull_, moored_sails_, town_, town_lights_, pattern_, legacy_sky_,
        legacy_sea_;
    std::vector<fx::SeaVertex> cpu_sea_vertices_;
    std::vector<uint32_t> cpu_sea_indices_;
    bgfx::VertexBufferHandle sea_vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle sea_indices_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle env_cube_ = BGFX_INVALID_HANDLE;
};

// Mean absolute difference of two RGBA images, 0..255.
double MeanDifference(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    if (a.size() != b.size() || a.empty())
        return 255.0;
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
    return sum / static_cast<double>(a.size());
}

// CPU-side products as images: baked textures, sky panoramas, grading LUT strips.
void WriteCpuImages(const std::string &dir)
{
    const auto ms = [](auto &&work) {
        const auto start = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    fx::BakedTexture detail, clouds;
    const double detail_ms = ms([&] { detail = fx::BakeWaterDetail(); });
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
    std::vector<uint8_t> normals(detail.rgba.begin(), detail.rgba.begin() + static_cast<size_t>(detail.size) * detail.size * 4);
    for (size_t i = 0; i < normals.size(); i += 4)
    {
        normals[i + 2] = 255;
        normals[i + 3] = 255;
    }
    WritePng(dir + "/tex_water_normal.png", detail.size, detail.size, detail.size * 4, normals.data(), bimg::TextureFormat::RGBA8, false);
    channel(detail, 2, "tex_water_foam");
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
    fx::EngineWaveField field;
    preview::FillWaveField(field, 0.0f);
    const double pack_ms = ms([&] { field.PackLayerHalf(0); field.PackLayerHalf(1); });
    std::printf("CPU cost on this host: water detail bake %.1f ms, cloud bake %.1f ms, sky panorama %.2f ms, grading LUT %.2f ms, "
                "two wave layers packed %.3f ms\n",
                detail_ms, cloud_ms, sky_ms, lut_ms, pack_ms);
}

std::vector<Shot> AllShots()
{
    using fx::QualityPreset;
    const fx::Settings original = fx::Settings::FromPreset(QualityPreset::Original);
    const fx::Settings medium = fx::Settings::FromPreset(QualityPreset::Medium);
    const fx::Settings high = fx::Settings::FromPreset(QualityPreset::High);
    std::vector<Shot> shots;
    const auto add = [&](std::string name, Content content, fx::Settings settings, preview::Weather weather = preview::Weather::Day) -> Shot & {
        shots.push_back({std::move(name), content, settings, weather});
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
    add("bloom_harbour_off", Content::Harbour, no_bloom, preview::Weather::Night);
    add("bloom_harbour_quarter", Content::Harbour, medium, preview::Weather::Night);
    add("bloom_harbour_half", Content::Harbour, high, preview::Weather::Night);
    add("bloom_sunset_off", Content::Ocean, no_bloom, preview::Weather::Sunset);
    fx::Settings no_shafts = high;
    no_shafts.sun_shafts = false;
    add("bloom_sunset_on", Content::Ocean, no_shafts, preview::Weather::Sunset);
    add("bloom_sunset_on_shafts", Content::Ocean, high, preview::Weather::Sunset);

    // Water quality levels at the shore, and the geometry sources.
    fx::Settings water = high;
    water.water = fx::WaterQuality::Legacy;
    add("water_shore_0_legacy", Content::Shore, water);
    water.water = fx::WaterQuality::Low;
    add("water_shore_1_low", Content::Shore, water);
    water.water = fx::WaterQuality::Medium;
    add("water_shore_2_medium", Content::Shore, water);
    add("water_shore_2_medium_envcube", Content::Shore, water).env_cube = true;
    add("water_shore_3_high", Content::Shore, high);
    fx::Settings no_refraction = high;
    no_refraction.water_refraction = false;
    add("water_shore_3_high_no_refraction", Content::Shore, no_refraction);
    add("water_shore_3_high_envcube", Content::Shore, high).env_cube = true;
    add("water_source_field", Content::Ocean, medium);
    add("water_source_mesh", Content::Ocean, medium).source = WaterSource::Mesh;
    add("water_source_gerstner", Content::Ocean, medium).source = WaterSource::Gerstner;

    // Sky without clouds.
    fx::Settings clear = high;
    clear.sky_clouds = false;
    add("sky_clear", Content::Ocean, clear);

    // Lighting flags.
    fx::Settings lit = high;
    lit.lighting_flags = fx::kLightingNone;
    add("lighting_0_per_vertex", Content::Deck, lit);
    lit.lighting_flags = fx::kLightingPerPixel | fx::kLightingHemisphere;
    add("lighting_1_per_pixel_hemisphere", Content::Deck, lit);
    add("lighting_2_specular_translucent", Content::Deck, high);
    add("lighting_sunset_0_per_vertex", Content::Deck, [&] {
            fx::Settings s = high;
            s.lighting_flags = fx::kLightingNone;
            return s;
        }(), preview::Weather::Sunset);
    add("lighting_sunset_2_full", Content::Deck, high, preview::Weather::Sunset);

    // Anti-aliasing, sharpening and dynamic resolution on the test chart.
    add("aa_0_off", Content::Pattern, original);
    fx::Settings aa = original;
    aa.fxaa = true;
    add("aa_1_fxaa", Content::Pattern, aa);
    aa.sharpen = true;
    add("aa_2_fxaa_sharpen", Content::Pattern, aa);
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
        std::printf("usage: fx_preview <output directory> [name filter]\n");
        return 2;
    }
    const std::string out_dir = argv[1];
    const std::string filter = argc > 2 ? argv[2] : "";
    std::filesystem::create_directories(out_dir);

    bgfx::Init init;
    init.type = bgfx::RendererType::OpenGLES;
    // No native window and no back buffer size: bgfx creates a 1x1 EGL pbuffer.
    init.swapChain.width = 0;
    init.swapChain.height = 0;
    init.fallback = false; // never fall back to the no-op renderer: black images would look like a pass
    if (!bgfx::init(init))
    {
        std::printf("bgfx::init failed (is an EGL implementation available?)\n");
        return 1;
    }
    std::printf("renderer: %s\n", bgfx::getRendererName(bgfx::getRendererType()));
    if (filter.empty())
        WriteCpuImages(out_dir);
    {
        Preview preview(out_dir);
        std::vector<uint8_t> field, mesh;
        for (const Shot &shot : AllShots())
        {
            if (!filter.empty() && shot.name.find(filter) == std::string::npos)
                continue;
            const std::vector<uint8_t> pixels = preview.Render(shot);
            if (shot.name == "water_source_field")
                field = pixels;
            if (shot.name == "water_source_mesh")
                mesh = pixels;
        }
        if (!field.empty() && !mesh.empty())
            std::printf("height-field (GPU) vs engine mesh (CPU) sea: mean difference %.2f / 255\n", MeanDifference(field, mesh));
    }
    bgfx::shutdown();
    return 0;
}
