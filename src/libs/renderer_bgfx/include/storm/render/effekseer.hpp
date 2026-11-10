// Effekseer particle effects (.efkefc) rendered through bgfx; see renderer_bgfx/effekseer/README.md.
//
// Main thread only, like every other bgfx call. The system is created lazily on the first call
// that needs it once the bgfx device exists, and is torn down with the device. Effects keep
// their GPU resources across Android surface loss (bgfx keeps the context).
//
// Coordinates and matrices follow the engine: left-handed, D3D row-vector matrices
// (glm::mat4 has the same memory layout), angles in radians.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct IDirect3DDevice9;

namespace storm::render::effekseer
{

// Reads a whole file through the engine's file service, which resolves the case-insensitive game
// paths. Returns false when the file is missing or unreadable.
using FileReader = std::function<bool(const std::string &path, std::vector<uint8_t> &data)>;

struct Settings
{
    FileReader read_file;
    int32_t max_instances = 4096; // Effekseer instance (particle) budget of the manager
    int32_t max_squares = 8192;   // sprites per batch in the transient vertex buffers
};

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

using EffectId = uint32_t;                        // 0 = invalid
using InstanceId = int32_t;                       // Effekseer::Handle
inline constexpr InstanceId kInvalidInstance = -1;

// Must be called before effects are loaded. Calling it again keeps loaded effects.
void Configure(const Settings &settings);

// Loads (or returns the cached) effect. `path` is the effect file as the game names it, e.g.
// "resource/effects/CLTorchFire.efkefc"; textures and models resolve relative to it. Returns 0
// and logs the reason on failure. `magnification` scales the whole effect (Effekseer units).
EffectId LoadEffect(std::string_view path, float magnification = 1.0f);
// Re-reads the effect and its resources from disk; running instances keep playing.
bool ReloadEffect(EffectId effect);
// Stops its instances and releases it.
void UnloadEffect(EffectId effect);
void UnloadAllEffects();
EffectId FindEffect(std::string_view path);

InstanceId Play(EffectId effect, const Vec3 &position);
bool Exists(InstanceId instance);
void Stop(InstanceId instance);     // removes the instance immediately
void StopRoot(InstanceId instance); // stops spawning; live particles finish
void StopAll();
void SetLocation(InstanceId instance, const Vec3 &position);
Vec3 GetLocation(InstanceId instance);
void AddLocation(InstanceId instance, const Vec3 &offset);
void SetRotation(InstanceId instance, const Vec3 &angles); // Euler angles, Effekseer's XYZ order
void SetScale(InstanceId instance, const Vec3 &scale);
void SetTargetLocation(InstanceId instance, const Vec3 &target);
void SetSpeed(InstanceId instance, float speed);
float GetSpeed(InstanceId instance);
void SetPaused(InstanceId instance, bool paused);
void SetColor(InstanceId instance, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void SetDynamicInput(InstanceId instance, int32_t index, float value);
float GetDynamicInput(InstanceId instance, int32_t index);
void SendTrigger(InstanceId instance, int32_t index);

// Advances every instance by `delta_seconds` (Effekseer runs at 60 ticks per second).
void Update(float delta_seconds);

// Draws all instances into the device's current render target and viewport (the game scene),
// as a separate pass after everything submitted so far. Call it after the opaque scene and
// before the interface. `view` and `projection` are the scene camera (D3D row-vector matrices).
void Draw(IDirect3DDevice9 *device, const float view[16], const float projection[16]);

// What an effect uses and what of it this renderer cannot show (everything else is drawn).
struct EffectInfo
{
    int32_t nodes = 0;              // drawable nodes (sprite, ribbon, ring, model, track)
    int32_t textures = 0;           // color + normal + distortion images referenced
    int32_t missing_textures = 0;   // referenced but not loaded
    int32_t models = 0;
    int32_t missing_models = 0;
    int32_t sounds = 0;             // not played: no Effekseer sound backend
    int32_t distortion_nodes = 0;   // not drawn: no copy of the scene background is provided
    int32_t soft_particle_nodes = 0; // drawn without the soft edge (no scene depth copy)
    int32_t material_nodes = 0;     // not drawn: .efkmat materials are unsupported
};
bool GetEffectInfo(EffectId effect, EffectInfo &info);

struct Stats
{
    uint32_t effects = 0;
    int32_t instances = 0;
    int32_t draw_calls = 0; // in the last Draw
};
Stats GetStats();

} // namespace storm::render::effekseer
