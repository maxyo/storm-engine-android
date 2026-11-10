// Effekseer manager, renderer and loaders on top of the vendored efkbgfx renderer.
#include "storm/render/effekseer.hpp"
#include "storm/render/effekseer_device.hpp"

#include "bgfxrenderer.h"
#include "textures.hpp"

#include <Effekseer.h>
#include <Effekseer/Effekseer.CurveLoader.h>
#include <Effekseer/Effekseer.DefaultEffectLoader.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace storm::render::effekseer
{
namespace
{
void logf(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "storm-effekseer", "%s", buffer);
#endif
    fprintf(stderr, "[storm-effekseer] %s\n", buffer);
}

std::string to_utf8(const char16_t *path)
{
    char buffer[2048];
    Effekseer::ConvertUtf16ToUtf8(buffer, sizeof(buffer), path);
    return buffer;
}

std::u16string to_utf16(std::string_view path)
{
    std::vector<char16_t> buffer(path.size() + 1);
    Effekseer::ConvertUtf8ToUtf16(buffer.data(), static_cast<int32_t>(buffer.size()), std::string(path).c_str());
    return buffer.data();
}

// Effect paths are compared case-insensitively with '/' separators, like the game's file service.
std::string cache_key(std::string_view path)
{
    std::string key(path);
    for (char &c : key)
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

class MemoryReader final : public Effekseer::FileReader
{
  public:
    explicit MemoryReader(std::vector<uint8_t> data) : data_(std::move(data))
    {
    }
    size_t Read(void *buffer, size_t size) override
    {
        const size_t count = std::min(size, data_.size() - position_);
        std::memcpy(buffer, data_.data() + position_, count);
        position_ += count;
        return count;
    }
    void Seek(int position) override
    {
        position_ = std::min(static_cast<size_t>(std::max(position, 0)), data_.size());
    }
    int GetPosition() const override
    {
        return static_cast<int>(position_);
    }
    size_t GetLength() const override
    {
        return data_.size();
    }

  private:
    std::vector<uint8_t> data_;
    size_t position_ = 0;
};

// Every effect, texture, model and curve read goes through the engine's file service.
class EngineFiles final : public Effekseer::FileInterface
{
  public:
    explicit EngineFiles(FileReader reader) : reader_(std::move(reader))
    {
    }
    Effekseer::FileReaderRef OpenRead(const char16_t *path) override
    {
        std::string name = to_utf8(path);
        std::replace(name.begin(), name.end(), '\\', '/');
        std::vector<uint8_t> data;
        if (!reader_ || !reader_(name, data))
        {
            logf("ERROR: cannot read %s", name.c_str());
            return nullptr;
        }
        return Effekseer::MakeRefPtr<MemoryReader>(std::move(data));
    }
    Effekseer::FileWriterRef OpenWrite(const char16_t *path) override
    {
        logf("ERROR: effects never write files (%s)", to_utf8(path).c_str());
        return nullptr;
    }

  private:
    FileReader reader_;
};

// CL ships no .efkmat files and efkbgfx has no material shader generator: refuse loudly.
class NoMaterials final : public Effekseer::MaterialLoader
{
  public:
    Effekseer::MaterialRef Load(const char16_t *path) override
    {
        logf("ERROR: Effekseer material %s is not supported (no material shaders); its nodes are not drawn",
             to_utf8(path).c_str());
        return nullptr;
    }
};

struct Effect
{
    std::string path;
    float magnification = 1.0f;
    Effekseer::EffectRef effect;
};

struct System
{
    Settings settings;
    bool configured = false;
    DeviceInfo device;
    bool device_attached = false;
    EffekseerRenderer::RendererRef renderer;
    Effekseer::ManagerRef manager;
    std::map<EffectId, Effect> effects;
    std::map<std::string, EffectId> by_path;
    EffectId next_id = 1;
    int32_t last_draw_calls = 0;
};

System &sys()
{
    static System system;
    return system;
}

void bgfx_log(const char *message)
{
    logf("%s", message);
}

// Creates the renderer and manager on first use (startup stays free for games without effects).
bool ensure_manager()
{
    System &s = sys();
    if (s.manager != nullptr)
        return true;
    if (!s.device_attached)
    {
        logf("ERROR: effects used before the bgfx device exists");
        return false;
    }
    if (!s.configured)
    {
        logf("ERROR: effects used before Configure() supplied a file reader");
        return false;
    }
    EffekseerRendererBGFX::InitArgs args{};
    args.squareMaxCount = s.settings.max_squares;
    args.bgfx = bgfx_get_interface(BGFX_API_VERSION);
    args.mirror_winding = s.device.flip_y;
    args.log = bgfx_log;
    s.renderer = EffekseerRendererBGFX::CreateRenderer(&args);
    if (s.renderer == nullptr)
    {
        logf("ERROR: cannot create the Effekseer bgfx renderer");
        return false;
    }
    s.manager = Effekseer::Manager::Create(s.settings.max_instances);
    s.manager->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);
    s.manager->SetSpriteRenderer(s.renderer->CreateSpriteRenderer());
    s.manager->SetRibbonRenderer(s.renderer->CreateRibbonRenderer());
    s.manager->SetRingRenderer(s.renderer->CreateRingRenderer());
    s.manager->SetTrackRenderer(s.renderer->CreateTrackRenderer());
    s.manager->SetModelRenderer(EffekseerRendererBGFX::CreateModelRenderer(s.renderer, &args));
    const auto files = Effekseer::MakeRefPtr<EngineFiles>(s.settings.read_file);
    s.manager->SetEffectLoader(Effekseer::MakeRefPtr<Effekseer::DefaultEffectLoader>(files));
    s.manager->SetTextureLoader(CreateCachedTextureLoader(s.renderer, files, logf));
    s.manager->SetModelLoader(s.renderer->CreateModelLoader(files));
    s.manager->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>(files));
    s.manager->SetMaterialLoader(Effekseer::MakeRefPtr<NoMaterials>());
    // No sound loader: effect sounds stay silent (reported per effect in LoadEffect).
    logf("Effekseer %s ready: %d instances, %d squares per batch", "1.80h", s.settings.max_instances,
         s.settings.max_squares);
    return true;
}

void count_nodes(Effekseer::EffectNode *node, EffectInfo &info)
{
    const Effekseer::EffectNodeType type = node->GetType();
    if (type != Effekseer::EffectNodeType::Root && type != Effekseer::EffectNodeType::NoneType)
    {
        ++info.nodes;
        const Effekseer::EffectBasicRenderParameter param = node->GetBasicRenderParameter();
        if (param.MaterialType == Effekseer::RendererMaterialType::BackDistortion || param.Distortion)
            ++info.distortion_nodes;
        else if (param.MaterialType == Effekseer::RendererMaterialType::File)
            ++info.material_nodes;
        if (param.SoftParticleDistanceFar != 0.0f || param.SoftParticleDistanceNear != 0.0f ||
            param.SoftParticleDistanceNearOffset != 0.0f)
            ++info.soft_particle_nodes;
    }
    for (int i = 0; i < node->GetChildrenCount(); ++i)
        count_nodes(node->GetChild(i), info);
}

EffectInfo inspect(const Effekseer::EffectRef &e)
{
    EffectInfo info;
    info.textures = e->GetColorImageCount() + e->GetNormalImageCount() + e->GetDistortionImageCount();
    for (int32_t i = 0; i < e->GetColorImageCount(); ++i)
        info.missing_textures += e->GetColorImage(i) == nullptr;
    for (int32_t i = 0; i < e->GetNormalImageCount(); ++i)
        info.missing_textures += e->GetNormalImage(i) == nullptr;
    for (int32_t i = 0; i < e->GetDistortionImageCount(); ++i)
        info.missing_textures += e->GetDistortionImage(i) == nullptr;
    info.models = e->GetModelCount();
    for (int32_t i = 0; i < e->GetModelCount(); ++i)
        info.missing_models += e->GetModel(i) == nullptr;
    info.sounds = e->GetWaveCount();
    count_nodes(e->GetRoot(), info);
    return info;
}

// Says what of the effect will not be shown; the effect still plays without it.
void report(const Effect &entry)
{
    const Effekseer::EffectRef &e = entry.effect;
    const char *path = entry.path.c_str();
    for (int32_t i = 0; i < e->GetColorImageCount(); ++i)
        if (e->GetColorImage(i) == nullptr)
            logf("ERROR: %s: texture %s is missing", path, to_utf8(e->GetColorImagePath(i)).c_str());
    for (int32_t i = 0; i < e->GetNormalImageCount(); ++i)
        if (e->GetNormalImage(i) == nullptr)
            logf("ERROR: %s: normal map %s is missing", path, to_utf8(e->GetNormalImagePath(i)).c_str());
    for (int32_t i = 0; i < e->GetDistortionImageCount(); ++i)
        if (e->GetDistortionImage(i) == nullptr)
            logf("ERROR: %s: distortion texture %s is missing", path, to_utf8(e->GetDistortionImagePath(i)).c_str());
    for (int32_t i = 0; i < e->GetModelCount(); ++i)
        if (e->GetModel(i) == nullptr)
            logf("ERROR: %s: model %s is missing", path, to_utf8(e->GetModelPath(i)).c_str());
    const EffectInfo info = inspect(e);
    if (info.sounds > 0)
        logf("%s: %d sound(s) are not played (no Effekseer sound backend)", path, info.sounds);
    if (info.distortion_nodes > 0)
        logf("%s: %d distortion node(s) are not drawn (no scene background copy)", path, info.distortion_nodes);
    if (info.soft_particle_nodes > 0)
        logf("%s: %d soft-particle node(s) are drawn without the soft edge (no scene depth copy)", path,
             info.soft_particle_nodes);
    if (info.material_nodes > 0)
        logf("%s: %d material node(s) are not drawn (.efkmat unsupported)", path, info.material_nodes);
}

Effect *find(EffectId id)
{
    auto it = sys().effects.find(id);
    return it == sys().effects.end() ? nullptr : &it->second;
}

Effekseer::Vector3D v3(const Vec3 &v)
{
    return {v.x, v.y, v.z};
}

Effekseer::Matrix44 matrix(const float m[16])
{
    Effekseer::Matrix44 out;
    std::memcpy(out.Values, m, sizeof(out.Values));
    return out;
}
} // namespace

void Configure(const Settings &settings)
{
    System &s = sys();
    if (s.manager != nullptr && (settings.max_instances != s.settings.max_instances ||
                                 settings.max_squares != s.settings.max_squares))
        logf("ERROR: effect budgets cannot change after the first effect was loaded; keeping %d/%d",
             s.settings.max_instances, s.settings.max_squares);
    const Settings previous = s.settings;
    s.settings = settings;
    if (s.manager != nullptr)
    {
        s.settings.max_instances = previous.max_instances;
        s.settings.max_squares = previous.max_squares;
    }
    s.configured = static_cast<bool>(settings.read_file);
}

EffectId FindEffect(std::string_view path)
{
    const auto it = sys().by_path.find(cache_key(path));
    return it == sys().by_path.end() ? 0 : it->second;
}

EffectId LoadEffect(std::string_view path, float magnification)
{
    if (const EffectId cached = FindEffect(path))
        return cached;
    if (!ensure_manager())
        return 0;
    System &s = sys();
    const std::u16string path16 = to_utf16(path);
    Effect entry{std::string(path), magnification, Effekseer::Effect::Create(s.manager, path16.c_str(), magnification)};
    if (entry.effect == nullptr)
    {
        logf("ERROR: cannot load effect %s", entry.path.c_str());
        return 0;
    }
    report(entry);
    const EffectId id = s.next_id++;
    s.by_path[cache_key(path)] = id;
    s.effects.emplace(id, std::move(entry));
    return id;
}

bool ReloadEffect(EffectId effect)
{
    Effect *entry = find(effect);
    if (!entry)
        return false;
    const std::u16string path16 = to_utf16(entry->path);
    if (!entry->effect->Reload(&sys().manager, 1, path16.c_str()))
    {
        logf("ERROR: cannot reload effect %s", entry->path.c_str());
        return false;
    }
    report(*entry);
    return true;
}

void UnloadEffect(EffectId effect)
{
    Effect *entry = find(effect);
    if (!entry)
        return;
    System &s = sys();
    s.manager->StopRoot(entry->effect); // detach running instances from the effect first
    s.by_path.erase(cache_key(entry->path));
    s.effects.erase(effect);
}

void UnloadAllEffects()
{
    System &s = sys();
    if (s.manager != nullptr)
        s.manager->StopAllEffects();
    s.effects.clear();
    s.by_path.clear();
}

InstanceId Play(EffectId effect, const Vec3 &position)
{
    Effect *entry = find(effect);
    if (!entry)
    {
        logf("ERROR: Play: unknown effect id %u", effect);
        return kInvalidInstance;
    }
    return sys().manager->Play(entry->effect, v3(position));
}

bool Exists(InstanceId instance)
{
    return sys().manager != nullptr && instance >= 0 && sys().manager->Exists(instance);
}

#define STORM_EFK_INSTANCE_CALL(call)                                                                                  \
    if (sys().manager != nullptr && instance >= 0)                                                                     \
        sys().manager->call;

void Stop(InstanceId instance)
{
    STORM_EFK_INSTANCE_CALL(StopEffect(instance))
}
void StopRoot(InstanceId instance)
{
    STORM_EFK_INSTANCE_CALL(StopRoot(instance))
}
void StopAll()
{
    if (sys().manager != nullptr)
        sys().manager->StopAllEffects();
}
void SetLocation(InstanceId instance, const Vec3 &position)
{
    STORM_EFK_INSTANCE_CALL(SetLocation(instance, v3(position)))
}
Vec3 GetLocation(InstanceId instance)
{
    if (!Exists(instance))
        return {};
    const Effekseer::Vector3D v = sys().manager->GetLocation(instance);
    return {v.X, v.Y, v.Z};
}
void AddLocation(InstanceId instance, const Vec3 &offset)
{
    STORM_EFK_INSTANCE_CALL(AddLocation(instance, v3(offset)))
}
void SetRotation(InstanceId instance, const Vec3 &angles)
{
    STORM_EFK_INSTANCE_CALL(SetRotation(instance, angles.x, angles.y, angles.z))
}
void SetScale(InstanceId instance, const Vec3 &scale)
{
    STORM_EFK_INSTANCE_CALL(SetScale(instance, scale.x, scale.y, scale.z))
}
void SetTargetLocation(InstanceId instance, const Vec3 &target)
{
    STORM_EFK_INSTANCE_CALL(SetTargetLocation(instance, v3(target)))
}
void SetSpeed(InstanceId instance, float speed)
{
    STORM_EFK_INSTANCE_CALL(SetSpeed(instance, speed))
}
float GetSpeed(InstanceId instance)
{
    return Exists(instance) ? sys().manager->GetSpeed(instance) : 0.0f;
}
void SetPaused(InstanceId instance, bool paused)
{
    STORM_EFK_INSTANCE_CALL(SetPaused(instance, paused))
}
void SetColor(InstanceId instance, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    STORM_EFK_INSTANCE_CALL(SetAllColor(instance, Effekseer::Color(r, g, b, a)))
}
void SetDynamicInput(InstanceId instance, int32_t index, float value)
{
    STORM_EFK_INSTANCE_CALL(SetDynamicInput(instance, index, value))
}
float GetDynamicInput(InstanceId instance, int32_t index)
{
    return Exists(instance) ? sys().manager->GetDynamicInput(instance, index) : 0.0f;
}
void SendTrigger(InstanceId instance, int32_t index)
{
    STORM_EFK_INSTANCE_CALL(SendTrigger(instance, index))
}
#undef STORM_EFK_INSTANCE_CALL

void Update(float delta_seconds)
{
    if (sys().manager == nullptr)
        return;
    PollTextures();
    sys().manager->Update(std::max(0.0f, delta_seconds) * 60.0f);
}

bool GetEffectInfo(EffectId effect, EffectInfo &info)
{
    const Effect *entry = find(effect);
    if (!entry)
        return false;
    info = inspect(entry->effect);
    return true;
}

Stats GetStats()
{
    const System &s = sys();
    Stats stats;
    stats.effects = static_cast<uint32_t>(s.effects.size());
    stats.instances = s.manager != nullptr ? s.manager->GetTotalInstanceCount() : 0;
    stats.draw_calls = s.last_draw_calls;
    const TextureStats textures = GetTextureStats();
    stats.texture_cache_hits = textures.cache_hits;
    stats.textures_decoded = textures.decoded;
    stats.textures_transcoding = textures.transcoding;
    return stats;
}

void AttachDevice(const DeviceInfo &info)
{
    System &s = sys();
    s.device = info;
    s.device_attached = true;
}

void DetachDevice()
{
    System &s = sys();
    if (s.manager != nullptr)
        logf("device shutdown: releasing %zu effect(s)", s.effects.size());
    ReleasePendingTextures();
    s.effects.clear();
    s.by_path.clear();
    s.manager.Reset();
    s.renderer.Reset();
    s.device_attached = false;
}

void DrawToView(uint16_t view, const float view_matrix[16], const float projection[16])
{
    System &s = sys();
    s.last_draw_calls = 0;
    if (s.manager == nullptr || s.manager->GetTotalInstanceCount() == 0)
        return;
    // D3D clip space -> backend clip space, applied after the scene projection (row vectors):
    // flip Y for bottom-left origin render targets, z 0..1 -> -1..1 for homogeneous depth.
    float fix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (s.device.flip_y)
        fix[5] = -1.0f;
    if (s.device.homogeneous_depth)
    {
        fix[10] = 2.0f;
        fix[14] = -1.0f;
    }
    const Effekseer::Matrix44 camera = matrix(view_matrix);
    Effekseer::Matrix44 proj;
    Effekseer::Matrix44::Mul(proj, matrix(projection), matrix(fix));
    s.renderer->SetCameraMatrix(camera);
    s.renderer->SetProjectionMatrix(proj);
    EffekseerRendererBGFX::SetViewId(s.renderer, view);
    s.renderer->ResetDrawCallCount();
    s.renderer->BeginRendering();
    Effekseer::Manager::DrawParameter parameter;
    parameter.ViewProjectionMatrix = s.renderer->GetCameraProjectionMatrix();
    parameter.ZNear = 0.0f;
    parameter.ZFar = 1.0f;
    s.manager->Draw(parameter);
    s.renderer->EndRendering();
    s.last_draw_calls = s.renderer->GetDrawCallCount();
}

} // namespace storm::render::effekseer
