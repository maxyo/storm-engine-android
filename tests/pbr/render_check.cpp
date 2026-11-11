// pbr_render_check: renders with the real PBR programs (renderer_bgfx/pbr) on a headless OpenGL ES 3.0 context
// (EGL, Mesa llvmpipe is enough) and writes PNG files.
//
//   pbr_render_check <out dir>                    synthetic: a 5 x 5 sphere grid (roughness x metallic) under a
//                                                 synthetic sky environment; no game data
//   pbr_render_check <out dir> --game <CL dir> --model <path below Resource/Models, no .gm> --preset <file in
//                    Resource/Lighting> --hour <0..23>
//                                                 one Caribbean Legend model loaded by the production .gm loader,
//                                                 its sidecar materials, the preset hour and its .hdr environment
//
// Fails (exit 1) on any PBR error (sidecar, material, environment, device), on an object of the model that cannot
// be drawn, or on an image without lit pixels. The game images are local evidence only: never commit or publish.
#include "geom.h"

#include "storm/pbr/environment.hpp"
#include "storm/pbr/material.hpp"
#include "storm/pbr/renderer.hpp"
#include "storm/render/texture_file.hpp"

#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/file.h>
#include <bx/math.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace storm;

namespace
{
constexpr uint16_t kWidth = 1280, kHeight = 720;
constexpr bgfx::ViewId kSceneView = 0, kReadbackView = 1;
int errors = 0;

void Error(const std::string &message)
{
    std::printf("ERROR: %s\n", message.c_str());
    ++errors;
}

double Seconds(std::chrono::steady_clock::time_point since)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

bx::DefaultAllocator allocator;

std::vector<uint8_t> ReadFile(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Case-insensitive lookup of a relative path (backslashes or slashes) below `root`.
std::map<std::string, fs::path> index_cache;
const fs::path *FindFile(const fs::path &root, std::string relative)
{
    if (index_cache.empty())
        for (const auto &entry : fs::recursive_directory_iterator(root))
            if (entry.is_regular_file())
            {
                std::string key = fs::relative(entry.path(), root).generic_string();
                std::ranges::transform(key, key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                index_cache.emplace(std::move(key), entry.path());
            }
    std::ranges::replace(relative, '\\', '/');
    std::ranges::transform(relative, relative.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const auto it = index_cache.find(relative);
    return it == index_cache.end() ? nullptr : &it->second;
}

// ---- GPU state shared by both scenes -----------------------------------------------------------

struct Gpu
{
    bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ff_proj{}, ff_vs{}, ff_clip{}, ff_fs{}, s_tex0{}, s_tex1{};
    pbr::Renderer pbr;
    bgfx::VertexLayout layout;

    bool Init()
    {
        std::string error;
        if (!pbr.Init(error))
        {
            Error("PBR renderer: " + error);
            return false;
        }
        const bgfx::TextureHandle attachments[2] = {
            bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT),
            bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY)};
        target = bgfx::createFrameBuffer(2, attachments, true);
        readback = bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8,
                                         BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        // The fixed-function uniforms the PBR programs share with the emulator (set by the shim in the game).
        ff_proj = bgfx::createUniform("u_ffProj", bgfx::UniformType::Mat4);
        ff_vs = bgfx::createUniform("u_ffVs", bgfx::UniformType::Vec4, 4);
        ff_clip = bgfx::createUniform("u_ffClipPlane", bgfx::UniformType::Vec4);
        ff_fs = bgfx::createUniform("u_ffFs", bgfx::UniformType::Vec4, 4);
        s_tex0 = bgfx::createUniform("s_tex0", bgfx::UniformType::Sampler);
        s_tex1 = bgfx::createUniform("s_tex1", bgfx::UniformType::Sampler);
        // Same attributes as the game's FVF vertices: position, normal, colour, uv0, uv1.
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
            .end();
        return true;
    }

    void BeginScene(const float view[16], const float proj[16], uint32_t clear)
    {
        bgfx::setViewName(kSceneView, "pbr scene");
        // Submission order, as the shim's game views: uniforms set once per frame reach every later draw.
        bgfx::setViewMode(kSceneView, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(kSceneView, target);
        bgfx::setViewRect(kSceneView, 0, 0, kWidth, kHeight);
        bgfx::setViewClear(kSceneView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clear, 1.0f, 0);
        bgfx::setViewTransform(kSceneView, nullptr, nullptr);
        bgfx::touch(kSceneView);
        bgfx::setUniform(ff_proj, proj);
        const float vs[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        bgfx::setUniform(ff_vs, vs, 4);
        const float clip[4] = {0, 0, 0, 0};
        bgfx::setUniform(ff_clip, clip);
        const float ffs[16] = {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // no fog, no alpha test
        bgfx::setUniform(ff_fs, ffs, 4);
        (void)view;
    }

    // Reads the target back and writes a PNG; returns the share of pixels that are neither black nor the clear colour.
    double Write(const std::string &path, uint32_t clear)
    {
        bgfx::setViewName(kReadbackView, "readback");
        bgfx::touch(kReadbackView);
        bgfx::TextureRegion from, to;
        from.init(bgfx::getTexture(target));
        to.init(readback);
        bgfx::blit(kReadbackView, to, from);
        std::vector<uint8_t> pixels(size_t(kWidth) * kHeight * 4);
        const uint32_t ready = bgfx::read(to, pixels.data());
        while (bgfx::frame() < ready)
        {
        }
        bx::FileWriter writer;
        bx::Error error;
        if (bx::open(&writer, bx::FilePath(path.c_str()), false, &error))
        {
            bimg::imageWritePng(&writer, kWidth, kHeight, kWidth * 4, pixels.data(), bimg::TextureFormat::RGBA8,
                                bgfx::getCaps()->originBottomLeft, &error);
            bx::close(&writer);
        }
        if (!error.isOk())
            Error("cannot write " + path);
        const uint8_t cr = uint8_t(clear >> 24), cg = uint8_t(clear >> 16), cb = uint8_t(clear >> 8);
        size_t lit = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            const bool background = pixels[i] == cr && pixels[i + 1] == cg && pixels[i + 2] == cb;
            lit += !background && (pixels[i] | pixels[i + 1] | pixels[i + 2]) > 8;
        }
        return double(lit) / double(size_t(kWidth) * kHeight);
    }
};

bgfx::TextureHandle SolidTexture(uint32_t abgr)
{
    return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&abgr, 4));
}

// Decodes a game texture (DDS / TX via bimg) to RGBA8 with mips.
bgfx::TextureHandle LoadTexture(const fs::path &textures, const std::string &name, std::string &error)
{
    const render::TextureFileCandidates candidates = render::GetTextureFileCandidates(name);
    std::vector<std::string> paths{candidates.primary};
    paths.insert(paths.end(), candidates.alternates.begin(), candidates.alternates.end());
    for (std::string path : paths)
    {
        constexpr std::string_view prefix = "resource\\textures\\";
        std::string lower = path;
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.rfind(prefix, 0) == 0)
            path = path.substr(prefix.size());
        const fs::path *file = FindFile(textures, path);
        if (!file)
            continue;
        const std::vector<uint8_t> bytes = ReadFile(*file);
        bx::Error err;
        bimg::ImageContainer *image = bimg::imageParse(&allocator, bytes.data(), uint32_t(bytes.size()), bimg::TextureFormat::RGBA8, &err);
        if (!image)
        {
            // The engine's own .tx container is read by the renderer, not by bimg: this check takes the next
            // candidate the engine's name rule lists (e.g. base_RMA.tga.tx -> base_RMA.dds) and says so.
            std::printf("note: %s is not readable here (engine .tx); trying the next candidate\n", file->string().c_str());
            error = "cannot decode " + file->string();
            continue;
        }
        bimg::ImageContainer *mipped = image->m_numMips > 1 ? image : bimg::imageGenerateMips(&allocator, *image, false);
        const bgfx::TextureHandle handle =
            bgfx::createTexture2D(uint16_t(mipped->m_width), uint16_t(mipped->m_height), mipped->m_numMips > 1, 1,
                                  bgfx::TextureFormat::RGBA8, 0, bgfx::copy(mipped->m_data, mipped->m_size));
        if (mipped != image)
            bimg::imageFree(mipped);
        bimg::imageFree(image);
        return handle;
    }
    if (error.empty())
        error = "texture not found: " + name;
    return BGFX_INVALID_HANDLE;
}

struct Vertex
{
    float pos[3], nrm[3];
    uint32_t color;
    float uv0[2], uv1[2];
};

// ---- scene 1: synthetic sphere grid ------------------------------------------------------------------

bool SynthEnvironment(pbr::Environment &env)
{
    // 6:1 strip, 64 per face: blue sky above, brown ground below, a bright sun on +Z / +Y.
    const uint32_t face = 64;
    std::vector<float> strip(size_t(face) * 6 * face * 4);
    for (uint32_t f = 0; f < 6; ++f)
        for (uint32_t y = 0; y < face; ++y)
            for (uint32_t x = 0; x < face; ++x)
            {
                const pbr::Vec3 d = pbr::CubeTexelDirection(f, x, y, face);
                float *p = &strip[(size_t(y) * face * 6 + f * face + x) * 4];
                const float up = d[1];
                pbr::Vec3 c = up > 0.0f ? pbr::Vec3{0.35f + 0.25f * (1 - up), 0.55f + 0.2f * (1 - up), 0.95f}
                                        : pbr::Vec3{0.25f, 0.2f, 0.15f};
                const float sun = d[0] * 0.0f + d[1] * 0.6f + d[2] * 0.8f;
                if (sun > 0.995f)
                    c = {40.0f, 38.0f, 34.0f};
                p[0] = c[0], p[1] = c[1], p[2] = c[2], p[3] = 1.0f;
            }
    std::string error;
    if (!pbr::BakeEnvironmentFromStrip(strip.data(), face * 6, face, {.face_size = 64}, env, error))
    {
        Error("synthetic environment: " + error);
        return false;
    }
    return true;
}

void SphereMesh(std::vector<Vertex> &vertices, std::vector<uint16_t> &indices)
{
    const int rings = 32, segments = 48;
    for (int r = 0; r <= rings; ++r)
        for (int s = 0; s <= segments; ++s)
        {
            const float v = float(r) / rings, u = float(s) / segments;
            const float theta = v * 3.14159265f, phi = u * 6.2831853f;
            Vertex vx{};
            vx.nrm[0] = std::sin(theta) * std::cos(phi);
            vx.nrm[1] = std::cos(theta);
            vx.nrm[2] = std::sin(theta) * std::sin(phi);
            for (int i = 0; i < 3; ++i)
                vx.pos[i] = vx.nrm[i];
            vx.color = 0xffffffffu;
            vx.uv0[0] = vx.uv1[0] = u * 4.0f;
            vx.uv0[1] = vx.uv1[1] = v * 2.0f;
            vertices.push_back(vx);
        }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s)
        {
            const uint16_t a = uint16_t(r * (segments + 1) + s), b = uint16_t(a + segments + 1);
            indices.insert(indices.end(), {a, uint16_t(a + 1), b, uint16_t(a + 1), uint16_t(b + 1), b});
        }
}

void SceneSpheres(Gpu &gpu, const fs::path &out)
{
    pbr::Environment env;
    if (!SynthEnvironment(env))
        return;
    pbr::GpuEnvironment genv;
    std::string error;
    if (!gpu.pbr.Upload(env, genv, error))
        return Error("upload: " + error);
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    SphereMesh(vertices, indices);
    const bgfx::VertexBufferHandle vb = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(Vertex))), gpu.layout);
    const bgfx::IndexBufferHandle ib = bgfx::createIndexBuffer(bgfx::copy(indices.data(), uint32_t(indices.size() * 2)));
    const bgfx::TextureHandle albedo = SolidTexture(0xff3a6fc8u); // ABGR: a warm orange
    // A gentle bump pattern for the normal-mapped row checks: 16 x 16 tangent-space normals.
    std::vector<uint32_t> bumps(16 * 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
        {
            const float nx = 0.35f * std::sin(x * 0.785f), ny = 0.35f * std::sin(y * 0.785f);
            const float nz = std::sqrt(1.0f - nx * nx - ny * ny);
            const auto enc = [](float c) { return uint32_t(std::lround((c * 0.5f + 0.5f) * 255.0f)); };
            bumps[size_t(y) * 16 + x] = 0xff000000u | enc(nz) << 16 | enc(ny) << 8 | enc(nx);
        }
    const bgfx::TextureHandle normal =
        bgfx::createTexture2D(16, 16, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(bumps.data(), uint32_t(bumps.size() * 4)));

    float view[16], proj[16];
    const bx::Vec3 eye{0.0f, 0.0f, -13.0f}, at{0.0f, 0.0f, 0.0f};
    bx::mtxLookAt(view, eye, at);
    bx::mtxProj(proj, 50.0f, float(kWidth) / kHeight, 0.5f, 100.0f, bgfx::getCaps()->homogeneousDepth);
    const uint32_t clear = 0x303030ffu;
    gpu.BeginScene(view, proj, clear);
    pbr::FrameConstants frame;
    frame.environment = &genv;
    const pbr::Vec3 sun_world{0.0f, 0.6f, 0.8f};
    for (int i = 0; i < 3; ++i) // view space = A * world, A[i][j] = view[j * 4 + i]
        frame.sun_view[size_t(i)] = view[0 * 4 + i] * sun_world[0] + view[1 * 4 + i] * sun_world[1] + view[2 * 4 + i] * sun_world[2];
    frame.sun = true;
    frame.sun_color = {1.6f, 1.55f, 1.45f};
    frame.ambient = {1.0f, 1.0f, 1.0f};
    frame.exposure = 1.0f;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            frame.view_to_world[size_t(r * 3 + c)] = view[r * 4 + c];
    gpu.pbr.SetFrame(frame);
    std::vector<pbr::MaterialDesc> materials(25);
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 5; ++col)
        {
            pbr::MaterialDesc &m = materials[size_t(row * 5 + col)];
            m.variant = row == 4 ? pbr::Variant::Standard : pbr::Variant::Flat;
            m.roughness = 0.05f + 0.95f * float(col) / 4.0f;
            m.metallic = row == 0 ? 1.0f : row == 1 ? 0.5f : 0.0f;
            m.needs_white = true;
            float world[16];
            bx::mtxTranslate(world, (col - 2) * 2.3f, (2 - row) * 2.3f, 0.0f);
            float model_view[16];
            bx::mtxMul(model_view, world, view);
            bgfx::setTransform(model_view);
            bgfx::setVertexBuffer(0, vb);
            bgfx::setIndexBuffer(ib);
            bgfx::setTexture(0, gpu.s_tex0, albedo);
            bgfx::setTexture(1, gpu.s_tex1, gpu.pbr.White());
            pbr::DrawConstants draw;
            draw.material = &m;
            if (m.variant == pbr::Variant::Standard)
                draw.textures[pbr::kUnitNormal] = normal;
            // Row 3: a red point light in front of the spheres.
            const pbr::PointLight point{{0.0f, 0.0f, 9.0f}, 12.0f, {3.0f, 0.4f, 0.3f}, {0.0f, 0.0f, 0.15f}};
            if (row == 3)
                draw.points = {&point, 1};
            gpu.pbr.BindDraw(draw);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
            bgfx::submit(kSceneView, gpu.pbr.Program(m.variant));
        }
    const double lit = gpu.Write((out / "pbr_spheres.png").string(), clear);
    std::printf("pbr_spheres.png: %.1f %% lit pixels\n", lit * 100.0);
    if (lit < 0.2)
        Error("sphere grid: too few lit pixels");
    bgfx::destroy(vb), bgfx::destroy(ib), bgfx::destroy(albedo), bgfx::destroy(normal);
    pbr::Renderer::Release(genv);
}

// ---- scene 2: a Caribbean Legend model ---------------------------------------------------------------

class ModelService final : public GEOM_SERVICE
{
  public:
    std::vector<std::string> textures;
    struct Buffer
    {
        int32_t type = -1; // vertex type, -1 for the index buffer
        std::vector<uint8_t> bytes;
    };
    std::vector<Buffer> buffers;

    std::fstream OpenFile(const char *fname) override { return std::fstream(fname, std::ios::in | std::ios::binary); }
    bool ReadFile(std::fstream &file, void *data, int32_t bytes) override
    {
        file.read(static_cast<char *>(data), bytes);
        return file.gcount() == bytes;
    }
    int FileSize(const char *) override { return 0; }
    void CloseFile(std::fstream &file) override { file.close(); }
    void *malloc(int32_t bytes) override { return ::malloc(bytes > 0 ? size_t(bytes) : 1); }
    void free(void *ptr) override { ::free(ptr); }
    GEOS::ID CreateTexture(const char *fname) override
    {
        textures.emplace_back(fname);
        return GEOS::ID(textures.size() - 1);
    }
    void ReleaseTexture(GEOS::ID) override {}
    void SetMaterial(const GEOS::MATERIAL &) override {}
    void SetPbrMaterial(const render::PbrMaterial *) override {}
    GEOS::ID CreateVertexBuffer(int32_t type, int32_t size) override
    {
        buffers.push_back({type, std::vector<uint8_t>(size_t(size))});
        return GEOS::ID(buffers.size() - 1);
    }
    void *LockVertexBuffer(GEOS::ID vb) override { return buffers[size_t(vb)].bytes.data(); }
    void UnlockVertexBuffer(GEOS::ID) override {}
    void ReleaseVertexBuffer(GEOS::ID) override {}
    GEOS::ID CreateIndexBuffer(int32_t size) override
    {
        buffers.push_back({-1, std::vector<uint8_t>(size_t(size))});
        return GEOS::ID(buffers.size() - 1);
    }
    void *LockIndexBuffer(GEOS::ID ib) override { return buffers[size_t(ib)].bytes.data(); }
    void UnlockIndexBuffer(GEOS::ID) override {}
    void ReleaseIndexBuffer(GEOS::ID) override {}
    void SetIndexBuffer(GEOS::ID) override {}
    void SetVertexBuffer(int32_t, GEOS::ID) override {}
    void DrawIndexedPrimitive(int32_t, int32_t, int32_t, int32_t, int32_t) override {}
    GEOS::ID CreateLight(GEOS::LIGHT) override { return 0; }
    void ActivateLight(GEOS::ID) override {}
    void SetCausticMode(bool) override {}
};

struct Options
{
    fs::path game;
    std::string model, preset;
    std::string texture_path; // what the scripts pass to GEOMETRY::SetTexturePath for this model ("Characters\\")
    int hour = 12;
    float side = -1.0f; // camera on the -Z side of the model (--front: +Z, where characters face)
};

void SceneModel(Gpu &gpu, const fs::path &out, const Options &options)
{
    const fs::path resource = options.game / "Resource";
    const fs::path textures_dir = resource / "Textures";
    const fs::path *gm_found = FindFile(resource, "Models/" + options.model + ".gm");
    if (!gm_found)
        return Error("model not found: " + options.model);
    const fs::path gm_path = *gm_found;
    const fs::path *gm = &gm_path;
    // Lighting preset and environment.
    const fs::path preset_path = resource / "Lighting" / options.preset;
    scene_data::Report report;
    const std::vector<uint8_t> preset_bytes = ReadFile(preset_path);
    const scene_data::LightingPreset preset =
        scene_data::ParseLightingPreset({reinterpret_cast<const char *>(preset_bytes.data()), preset_bytes.size()}, report);
    for (const auto &e : report.errors)
        Error(preset_path.string() + ": " + e);
    pbr::HourLighting hour;
    std::string error;
    if (!pbr::HourConstants(preset, options.hour, hour, error))
        return Error(error);
    if (!hour.sun_direction_known)
        Error("preset has no sun_azimuth / sun_height");
    std::string hdr = hour.hdr_texture;
    if (hdr.rfind("resource/", 0) == 0 || hdr.rfind("Resource/", 0) == 0)
        hdr = hdr.substr(9);
    const fs::path *hdr_found = FindFile(resource, hdr);
    if (!hdr_found)
        return Error("hdr not found: " + hour.hdr_texture);
    const fs::path hdr_path = *hdr_found;
    const fs::path *hdr_file = &hdr_path;
    index_cache.clear(); // FindFile indexes one root at a time: the textures next
    auto start = std::chrono::steady_clock::now();
    pbr::Environment env;
    if (!pbr::BakeEnvironment(ReadFile(*hdr_file), {}, env, error))
        return Error(hdr_file->string() + ": " + error);
    std::printf("environment %s baked in %.2f s (face %u, %u mips)\n", hdr.c_str(), Seconds(start), env.radiance.size, env.radiance.mips);
    pbr::GpuEnvironment genv;
    if (!gpu.pbr.Upload(env, genv, error))
        return Error("upload: " + error);

    // Model + sidecar.
    start = std::chrono::steady_clock::now();
    ModelService service;
    std::unique_ptr<GEOS> geo;
    try
    {
        geo.reset(CreateGeometry(gm->string().c_str(), nullptr, service, 0));
    }
    catch (const std::exception &e)
    {
        return Error(gm->string() + ": " + e.what());
    }
    fs::path sidecar_path = *gm;
    sidecar_path.replace_extension(".json");
    const std::vector<uint8_t> sidecar_bytes = ReadFile(sidecar_path);
    report = {};
    const scene_data::ModelSidecar sidecar =
        scene_data::ParseModelSidecar({reinterpret_cast<const char *>(sidecar_bytes.data()), sidecar_bytes.size()}, report);
    for (const auto &e : report.errors)
        Error(sidecar_path.string() + ": " + e);
    for (const auto &k : report.unknown_keys)
        std::printf("note: %s: key not modelled: %s\n", sidecar_path.filename().c_str(), k.c_str());

    std::map<std::string, bgfx::TextureHandle> texture_cache;
    const auto texture = [&](const std::string &name) {
        if (const auto it = texture_cache.find(name); it != texture_cache.end())
            return it->second;
        std::string e;
        const bgfx::TextureHandle h = LoadTexture(textures_dir, name, e);
        if (!bgfx::isValid(h))
            Error(e);
        return texture_cache[name] = h;
    };

    GEOS::INFO info;
    geo->GetInfo(info);
    std::map<int32_t, bgfx::VertexBufferHandle> vbs;
    const ModelService::Buffer *index_buffer = &service.buffers[size_t(geo->GetIndexBuffer())];
    const bgfx::IndexBufferHandle ib = bgfx::createIndexBuffer(bgfx::copy(index_buffer->bytes.data(), uint32_t(index_buffer->bytes.size())));

    float view[16], proj[16];
    const float radius = std::max({info.boxsize.x, info.boxsize.y, info.boxsize.z}) * 0.6f;
    const bx::Vec3 center{info.boxcenter.x, info.boxcenter.y, info.boxcenter.z};
    const bx::Vec3 eye{center.x + radius * 1.3f, center.y + radius * 0.8f, center.z + options.side * radius * 1.8f};
    bx::mtxLookAt(view, eye, center);
    bx::mtxProj(proj, 45.0f, float(kWidth) / kHeight, radius * 0.05f, radius * 20.0f, bgfx::getCaps()->homogeneousDepth);
    const uint32_t clear = 0x101418ffu;
    gpu.BeginScene(view, proj, clear);
    pbr::FrameConstants frame;
    frame.environment = &genv;
    for (int i = 0; i < 3; ++i)
        frame.sun_view[size_t(i)] = view[0 * 4 + i] * hour.sun_direction[0] + view[1 * 4 + i] * hour.sun_direction[1] +
                                    view[2 * 4 + i] * hour.sun_direction[2];
    frame.sun = hour.sun_direction_known;
    frame.sun_color = hour.sun;
    frame.ambient = hour.ambient;
    frame.exposure = hour.exposure;
    frame.emission = hour.emission;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            frame.view_to_world[size_t(r * 3 + c)] = view[r * 4 + c];
    // The game's camera grade.
    const fs::path tone_path = resource / "INI" / "aces.json";
    const std::vector<uint8_t> tone_bytes = ReadFile(tone_path);
    report = {};
    const scene_data::ToneMapping tone =
        scene_data::ParseToneMapping({reinterpret_cast<const char *>(tone_bytes.data()), tone_bytes.size()}, report);
    for (const auto &e : report.errors)
        Error(tone_path.string() + ": " + e);
    if (!pbr::ToGrade(tone, frame.grade, error))
        Error(tone_path.string() + ": " + error);
    gpu.pbr.SetFrame(frame);

    int drawn = 0, legacy = 0;
    std::vector<pbr::MaterialDesc> materials(size_t(info.nobjects));
    for (int32_t o = 0; o < info.nobjects; ++o)
    {
        GEOS::OBJECT object;
        geo->GetObj(o, object);
        if (!(object.flags & GEOS::VISIBLE) || object.ntriangles == 0)
            continue;
        const auto entry = sidecar.models.find(object.name);
        if (entry == sidecar.models.end())
        {
            std::printf("object %s: no sidecar entry (legacy material)\n", object.name);
            ++legacy;
            continue;
        }
        const pbr::ResolveResult resolved = pbr::ResolveMaterial(entry->second);
        if (resolved.resolution != pbr::Resolution::Pbr)
        {
            for (const auto &e : resolved.errors)
                Error(std::string("object ") + object.name + ": " + e);
            if (resolved.resolution == pbr::Resolution::NotPbr)
                ++legacy;
            continue;
        }
        materials[size_t(o)] = resolved.material;
        const ModelService::Buffer &vb_source = service.buffers[size_t(object.vertex_buff)];
        // Static vertices (GEOS::VERTEX0..3) or animated ones (GEOS::AVERTEX0, drawn in the bind pose).
        const bool animated = (vb_source.type & 4) != 0;
        const uint32_t stride = animated ? uint32_t(sizeof(GEOS::AVERTEX0)) : 36 + 8 * uint32_t(vb_source.type & 3);
        auto vb = vbs.find(object.vertex_buff);
        if (vb == vbs.end())
        {
            // Repack to the check's layout (uv1 = uv0 when the model has one uv set).
            const size_t count = vb_source.bytes.size() / stride;
            std::vector<Vertex> vertices(count);
            for (size_t i = 0; i < count; ++i)
            {
                const uint8_t *src = &vb_source.bytes[i * stride];
                Vertex &v = vertices[i];
                if (animated)
                {
                    GEOS::AVERTEX0 a;
                    std::memcpy(&a, src, sizeof(a));
                    std::memcpy(v.pos, &a.pos, 12);
                    std::memcpy(v.nrm, &a.nrm, 12);
                    v.color = uint32_t(a.color);
                    v.uv0[0] = v.uv1[0] = a.tu0;
                    v.uv0[1] = v.uv1[1] = a.tv0;
                }
                else
                {
                    std::memcpy(&v, src, 36);
                    std::memcpy(v.uv1, stride > 36 ? src + 36 : src + 28, 8);
                }
            }
            vb = vbs.emplace(object.vertex_buff,
                             bgfx::createVertexBuffer(bgfx::copy(vertices.data(), uint32_t(count * sizeof(Vertex))), gpu.layout))
                     .first;
        }
        GEOS::MATERIAL material;
        geo->GetMaterial(object.material, material);
        if (material.texture_type[0] == GEOS::TEXTURE_NONE)
        {
            Error(std::string("object ") + object.name + ": no albedo texture");
            continue;
        }
        // The model's own textures are relative to the scripts' texture path (GEOMETRY::CreateTexture).
        const bgfx::TextureHandle albedo = texture(options.texture_path + service.textures[size_t(material.texture[0])]);
        const bool has_ao = material.texture_type[1] != GEOS::TEXTURE_NONE;
        const bgfx::TextureHandle ao =
            has_ao ? texture(options.texture_path + service.textures[size_t(material.texture[1])]) : gpu.pbr.White();
        pbr::DrawConstants draw;
        draw.material = &materials[size_t(o)];
        draw.has_ao_map = has_ao;
        bool complete = bgfx::isValid(albedo) && bgfx::isValid(ao);
        for (const pbr::TextureRef &ref : draw.material->textures)
        {
            draw.textures[ref.unit] = texture(ref.path);
            complete = complete && bgfx::isValid(draw.textures[ref.unit]);
        }
        if (!complete)
            continue;
        float model_view[16];
        std::memcpy(model_view, view, sizeof(view));
        bgfx::setTransform(model_view);
        bgfx::setVertexBuffer(0, vb->second, uint32_t(object.start_vertex), uint32_t(object.num_vertices));
        bgfx::setIndexBuffer(ib, uint32_t(object.striangle * 3), uint32_t(object.ntriangles * 3));
        bgfx::setTexture(0, gpu.s_tex0, albedo);
        bgfx::setTexture(1, gpu.s_tex1, ao);
        gpu.pbr.BindDraw(draw);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(kSceneView, gpu.pbr.Program(draw.material->variant));
        ++drawn;
        std::printf("object %-24s %-8s %zu textures\n", object.name, pbr::VariantName(draw.material->variant), draw.material->textures.size());
    }
    std::string name = options.model.substr(options.model.find_last_of('/') + 1) + "_h" + std::to_string(options.hour) + ".png";
    const double lit = gpu.Write((out / name).string(), clear);
    std::printf("%s: %d PBR objects, %d legacy, %.1f %% lit pixels, %.2f s\n", name.c_str(), drawn, legacy, lit * 100.0, Seconds(start));
    if (drawn == 0 || lit < 0.01)
        Error("model: nothing drawn");
    for (auto &[k, h] : texture_cache)
        if (bgfx::isValid(h))
            bgfx::destroy(h);
    for (auto &[k, h] : vbs)
        bgfx::destroy(h);
    bgfx::destroy(ib);
    pbr::Renderer::Release(genv);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: pbr_render_check <out dir> [--game <dir> --model <path> --preset <file> --hour <h>]\n");
        return 2;
    }
    const fs::path out = argv[1];
    Options options;
    for (int i = 2; i < argc; i += 2)
    {
        const std::string key = argv[i];
        if (key == "--front")
        {
            options.side = 1.0f;
            --i;
            continue;
        }
        if (i + 1 >= argc)
        {
            std::printf("option %s needs a value\n", key.c_str());
            return 2;
        }
        if (key == "--game")
            options.game = argv[i + 1];
        else if (key == "--model")
            options.model = argv[i + 1];
        else if (key == "--preset")
            options.preset = argv[i + 1];
        else if (key == "--texture-path")
            options.texture_path = argv[i + 1];
        else if (key == "--hour")
            options.hour = std::atoi(argv[i + 1]);
        else
        {
            std::printf("unknown option %s\n", key.c_str());
            return 2;
        }
    }
    fs::create_directories(out);
    bgfx::Init init;
    init.type = bgfx::RendererType::OpenGLES;
    init.swapChain.width = 0; // no window: bgfx creates a 1x1 EGL pbuffer
    init.swapChain.height = 0;
    init.fallback = false;
    if (!bgfx::init(init))
    {
        std::printf("bgfx::init failed (is an EGL implementation available?)\n");
        return 1;
    }
    {
        Gpu gpu;
        if (gpu.Init())
        {
            const auto start = std::chrono::steady_clock::now();
            SceneSpheres(gpu, out);
            std::printf("spheres %.2f s\n", Seconds(start));
            if (!options.game.empty())
                SceneModel(gpu, out, options);
            gpu.pbr.Shutdown();
            bgfx::destroy(gpu.target), bgfx::destroy(gpu.readback);
            for (auto u : {gpu.ff_proj, gpu.ff_vs, gpu.ff_clip, gpu.ff_fs, gpu.s_tex0, gpu.s_tex1})
                bgfx::destroy(u);
        }
    }
    bgfx::shutdown();
    std::printf("%s (%d errors)\n", errors ? "FAILED" : "PASSED", errors);
    return errors ? 1 : 0;
}
