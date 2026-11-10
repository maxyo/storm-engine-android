#include "pbr/host.hpp"

#include "shim/device.hpp"
#include "storm/fx/light_set.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

namespace storm::pbr_host
{
namespace
{
// Version of the baked environment file; bump with any change of the bake (face size, filter, layout).
constexpr uint32_t kCacheVersion = 1;
constexpr char kCacheMagic[8] = {'S', 'P', 'B', 'R', 'I', 'B', 'L', '1'};
// Assumed bounding radius of a PBR object around its world origin for ranking point lights (the shim does not know
// the object's bounds; the engine already chose the lights near it).
constexpr float kObjectRadius = 10.0f;

std::string Key(std::string path)
{
    std::ranges::transform(path, path.begin(), [](unsigned char c) { return c == '/' ? '\\' : char(std::tolower(c)); });
    return path;
}

uint64_t Fnv1a(const std::vector<uint8_t> &bytes)
{
    uint64_t hash = 1469598103934665603ull;
    for (uint8_t b : bytes)
        hash = (hash ^ b) * 1099511628211ull;
    return hash;
}

std::filesystem::path CacheFile(const std::filesystem::path &directory, uint64_t hash)
{
    char name[48];
    snprintf(name, sizeof(name), "ibl-%016llx.bin", static_cast<unsigned long long>(hash));
    return directory / name;
}

struct CacheHeader
{
    char magic[8];
    uint32_t version, face_size, size, mips;
    uint64_t hash;
};

bool ReadCache(const std::filesystem::path &file, uint64_t hash, pbr::Environment &out)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    CacheHeader header{};
    in.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!in || std::memcmp(header.magic, kCacheMagic, 8) != 0 || header.version != kCacheVersion || header.hash != hash ||
        header.face_size != pbr::BakeOptions{}.face_size)
        return false;
    out.radiance.size = header.size;
    out.radiance.mips = header.mips;
    out.radiance.rgba.resize(out.radiance.MipOffset(header.mips, 0));
    in.read(reinterpret_cast<char *>(out.irradiance.c.data()), sizeof(out.irradiance.c));
    in.read(reinterpret_cast<char *>(out.radiance.rgba.data()), std::streamsize(out.radiance.rgba.size() * sizeof(float)));
    return bool(in);
}

void WriteCache(const std::filesystem::path &file, uint64_t hash, const pbr::Environment &env)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        CacheHeader header{};
        std::memcpy(header.magic, kCacheMagic, 8);
        header.version = kCacheVersion;
        header.face_size = pbr::BakeOptions{}.face_size;
        header.size = env.radiance.size;
        header.mips = env.radiance.mips;
        header.hash = hash;
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        out.write(reinterpret_cast<const char *>(env.irradiance.c.data()), sizeof(env.irradiance.c));
        out.write(reinterpret_cast<const char *>(env.radiance.rgba.data()), std::streamsize(env.radiance.rgba.size() * sizeof(float)));
        if (!out)
        {
            shim::logf("ERROR: pbr: cannot write the environment cache %s", temp.string().c_str());
            return;
        }
    }
    std::filesystem::rename(temp, file, ec);
    if (ec)
        shim::logf("ERROR: pbr: cannot write the environment cache %s: %s", file.string().c_str(), ec.message().c_str());
}

pbr::Vec3 ToView(const D3DMATRIX &v, const pbr::Vec3 &d)
{
    return {d[0] * v.m[0][0] + d[1] * v.m[1][0] + d[2] * v.m[2][0], d[0] * v.m[0][1] + d[1] * v.m[1][1] + d[2] * v.m[2][1],
            d[0] * v.m[0][2] + d[1] * v.m[1][2] + d[2] * v.m[2][2]};
}
} // namespace

Host &Host::instance()
{
    static Host host;
    return host;
}

void Host::report_once(const std::string &key, const std::string &message)
{
    if (reported_.insert(key).second)
        shim::logf("%s", message.c_str());
}

void Host::on_device_created()
{
    std::string error;
    device_ready_ = renderer_.Init(error);
    device_failed_ = !device_ready_;
    if (device_failed_)
        shim::logf("ERROR: pbr: disabled for this session: %s; Caribbean Legend PBR models use the legacy path", error.c_str());
}

void Host::shutdown()
{
    for (auto &[key, env] : environments_)
    {
        if (env.baking.valid())
            env.baking.wait();
        pbr::Renderer::Release(env.gpu);
    }
    environments_.clear();
    renderer_.Shutdown();
    device_ready_ = false;
    material_ = nullptr;
}

void Host::set_lighting(const render::PbrLighting &lighting)
{
    lighting_ = lighting;
    has_lighting_ = true;
    if (lighting.hour.hdr_texture.empty())
    {
        report_once("nohdr:" + lighting.scene, "ERROR: pbr: scene " + lighting.scene + ": the preset hour names no hdr_texture; image-based light off");
        environment_key_.clear();
        return;
    }
    const std::string key = Key(lighting.hour.hdr_texture);
    if (!environments_.contains(key))
        start_bake(key);
    environment_key_ = key;
}

bool Host::point_group(const char *group, std::array<float, 3> &rgb) const
{
    if (!has_lighting_ || !group)
        return false;
    const std::string key = Key(group);
    for (const auto &[name, color] : lighting_.hour.point_groups)
        if (name == key)
        {
            rgb = color;
            return true;
        }
    return false;
}

void Host::start_bake(const std::string &hdr)
{
    Environment &env = environments_[hdr];
    std::vector<uint8_t> bytes = config_.read_file ? config_.read_file(hdr) : std::vector<uint8_t>{};
    if (bytes.empty())
    {
        env.failed = true;
        shim::logf("ERROR: pbr: cannot read the environment %s; image-based light off while it is in use", hdr.c_str());
        return;
    }
    const uint64_t hash = Fnv1a(bytes);
    const std::filesystem::path cache = config_.cache_directory.empty() ? std::filesystem::path() : CacheFile(config_.cache_directory, hash);
    auto cached = std::make_unique<pbr::Environment>();
    if (!cache.empty() && ReadCache(cache, hash, *cached))
    {
        std::promise<std::unique_ptr<pbr::Environment>> ready;
        ready.set_value(std::move(cached));
        env.baking = ready.get_future();
        shim::logf("pbr: environment %s from the cache", hdr.c_str());
        return;
    }
    shim::logf("pbr: baking the environment %s (worker thread)", hdr.c_str());
    env.baking = std::async(std::launch::async, [bytes = std::move(bytes), hdr, hash, cache]() -> std::unique_ptr<pbr::Environment> {
        const auto start = std::chrono::steady_clock::now();
        auto out = std::make_unique<pbr::Environment>();
        std::string error;
        if (!pbr::BakeEnvironment(bytes, {}, *out, error))
        {
            shim::logf("ERROR: pbr: environment %s: %s; image-based light off while it is in use", hdr.c_str(), error.c_str());
            return nullptr;
        }
        shim::logf("pbr: environment %s baked in %.0f ms", hdr.c_str(),
                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        if (!cache.empty())
            WriteCache(cache, hash, *out);
        return out;
    });
}

const pbr::GpuEnvironment *Host::current_environment()
{
    const auto it = environments_.find(environment_key_);
    if (it == environments_.end())
        return nullptr;
    Environment &env = it->second;
    if (env.baking.valid() && env.baking.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        const std::unique_ptr<pbr::Environment> baked = env.baking.get();
        std::string error;
        if (!baked || !renderer_.Upload(*baked, env.gpu, error))
        {
            env.failed = true;
            if (baked)
                shim::logf("ERROR: pbr: environment %s: %s", environment_key_.c_str(), error.c_str());
        }
    }
    if (env.gpu.valid())
        return &env.gpu;
    if (!env.failed)
        report_once("baking:" + environment_key_,
                    "pbr: environment " + environment_key_ + " is still baking: image-based light off until it is ready");
    return nullptr;
}

Draw Host::prepare(const shim::Device &device, const shim::LayoutInfo &layout, bool fixed_function)
{
    if (!material_)
        return {};
    if (!device_ready_)
    {
        if (!device_failed_)
            report_once("device", "ERROR: pbr: draw before the device was created");
        return {};
    }
    if (!fixed_function)
    {
        report_once("ports:" + material_->name, "ERROR: pbr: " + material_->name + ": the technique sets shader ports; drawn with the legacy path");
        return {};
    }
    if (layout.rhw || !layout.has_normal)
    {
        report_once("layout:" + material_->name, "ERROR: pbr: " + material_->name + ": vertices without normals; drawn with the legacy path");
        return {};
    }
    if (!has_lighting_)
    {
        report_once("nolighting", "ERROR: pbr: PBR draws before any lighting preset was set (SetPbrLighting): skipped");
        return {Draw::Kind::Skip};
    }
    (void)device;
    return {Draw::Kind::Pbr, renderer_.Program(material_->desc.variant)};
}

void Host::submit(const shim::Device &device, const Draw &draw)
{
    if (draw.kind != Draw::Kind::Pbr)
        return;
    const shim::DeviceState &s = device.s;
    // Frame constants: once per frame and whenever the camera changes (reflection renders use their own).
    const uint64_t frame = shim::Context::instance().frame_index();
    if (frame != frame_set_ || std::memcmp(&frame_view_, &s.view, sizeof(D3DMATRIX)) != 0)
    {
        frame_set_ = frame;
        frame_view_ = s.view;
        report_once("unshadowed", "pbr: PBR sun unshadowed: fx shadow map not integrated");
        pbr::FrameConstants constants;
        constants.environment = current_environment();
        const pbr::HourLighting &hour = lighting_.hour;
        constants.sun = hour.sun_direction_known;
        if (!constants.sun)
            report_once("nosun:" + lighting_.scene,
                        "ERROR: pbr: scene " + lighting_.scene + ": the preset has no sun_azimuth / sun_height; sun light off");
        pbr::Vec3 sun = ToView(s.view, hour.sun_direction);
        const float length = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
        if (length > 0.0f)
            sun = {sun[0] / length, sun[1] / length, sun[2] / length};
        constants.sun_view = sun;
        constants.sun_color = hour.sun;
        constants.ambient = hour.ambient;
        constants.exposure = hour.exposure;
        constants.emission = hour.emission;
        // view -> world rotation = rows of the D3D view matrix's 3x3 (orthonormal camera).
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                constants.view_to_world[size_t(r * 3 + c)] = s.view.m[r][c];
        constants.grade = lighting_.grade;
        renderer_.SetFrame(constants);
    }

    pbr::DrawConstants constants;
    constants.material = &material_->desc;
    for (uint8_t unit = 2; unit < pbr::kUnitCount; ++unit)
        if (IDirect3DBaseTexture9 *texture = material_->textures[unit])
            if (shim::TexStore *store = shim::store_of(texture))
            {
                store->sync();
                constants.textures[unit] = store->handle;
            }
    if (const shim::TexStore *albedo = shim::store_of(s.textures[0]))
        constants.sampler_flags = device.sampler_flags(0, *albedo);
    constants.has_ao_map = s.textures[1] && s.tss[1][D3DTSS_COLOROP] != D3DTOP_DISABLE && shim::store_of(s.textures[1]);
    if (!constants.has_ao_map)
        bgfx::setTexture(pbr::kUnitAo, shim::Context::instance().sampler(pbr::kUnitAo), renderer_.White());
    if (s.rs[D3DRS_ALPHABLENDENABLE])
        constants.alpha_factor = float((s.rs[D3DRS_TEXTUREFACTOR] >> 24) & 0xffu) / 255.0f;

    // Point lights: the enabled D3D point / spot lights the engine set near this object, strongest kMaxPointLights.
    std::vector<fx::PointLight> lights;
    for (const shim::Light &light : device.lights)
        if (light.enabled && (light.light.Type == D3DLIGHT_POINT || light.light.Type == D3DLIGHT_SPOT))
        {
            const D3DLIGHT9 &l = light.light;
            fx::PointLight p;
            p.position = {l.Position.x, l.Position.y, l.Position.z};
            p.color = {l.Diffuse.r, l.Diffuse.g, l.Diffuse.b};
            p.range = l.Range;
            p.att0 = l.Attenuation0, p.att1 = l.Attenuation1, p.att2 = l.Attenuation2;
            lights.push_back(p);
        }
    std::array<pbr::PointLight, pbr::kMaxPointLights> points{};
    size_t count = 0;
    if (!lights.empty())
    {
        const fx::Vec3 center{s.world.m[3][0], s.world.m[3][1], s.world.m[3][2]};
        const fx::LightSelection selection = fx::SelectLights(lights, center, kObjectRadius, pbr::kMaxPointLights);
        if (selection.dropped > 0)
            report_once("budget", "pbr: more than 4 point lights reach a PBR object: the weakest are left out (designed limit)");
        for (int i = 0; i < selection.count; ++i)
        {
            const fx::PointLight &p = selection.lights[size_t(i)];
            const D3DMATRIX &v = s.view;
            pbr::PointLight &out = points[count++];
            for (int c = 0; c < 3; ++c)
                out.position_view[size_t(c)] =
                    p.position[0] * v.m[0][c] + p.position[1] * v.m[1][c] + p.position[2] * v.m[2][c] + v.m[3][c];
            out.range = p.range;
            out.color = {p.color[0], p.color[1], p.color[2]};
            out.attenuation = {p.att0, p.att1, p.att2};
        }
    }
    constants.points = {points.data(), count};
    renderer_.BindDraw(constants);
}

} // namespace storm::pbr_host

namespace storm::render
{

void ConfigurePbr(const PbrConfig &config)
{
    pbr_host::Host::instance().configure(config);
}

void SetPbrMaterial(const PbrMaterial *material)
{
    pbr_host::Host::instance().set_material(material);
}

void SetPbrLighting(const PbrLighting &lighting)
{
    pbr_host::Host::instance().set_lighting(lighting);
}

bool PbrPointLightGroup(const char *group, std::array<float, 3> &rgb)
{
    return pbr_host::Host::instance().point_group(group, rgb);
}

} // namespace storm::render
