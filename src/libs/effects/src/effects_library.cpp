#include "effects_library.hpp"

#include "effect_catalog.hpp"
#include "effects.hpp"
#include "effects_natives.hpp"

#include "core.h"
#include "s_import_func.h"
#include "shared/messages.h"
#include "storm_glm.h"
#include "v_data.h"
#include "v_s_stack.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace efk = storm::render::effekseer;
using storm::effects::Catalog;
using storm::effects::EffectRecord;

namespace
{
constexpr int32_t kInvalidHandle = -1; // INVALID_EFF_HANDLE of the scripts

VDATA *Pop(VS_STACK *stack)
{
    return static_cast<VDATA *>(stack->Pop());
}

void Return(VS_STACK *stack, int32_t value)
{
    static_cast<VDATA *>(stack->Push())->Set(value);
}

// The direction vector of CreateParticleSystemX as angles, the way the particles entity turns
// it into an emitter orientation (PS_CREATEX).
Effects::Vec3 DirectionToAngles(const Effects::Vec3 &direction)
{
    glm::vec3 normal(direction.x, direction.y, direction.z);
    const float length = glm::length(normal);
    if (length == 0.0f)
        return {};
    normal /= length;
    return {std::asin(std::clamp(-normal.y, -1.0f, 1.0f)), storm::math::angleY(normal), 0.0f};
}

int32_t CreateParticleSystem(const std::string &id, const std::string &file, const Effects::Vec3 &position,
                             const Effects::Vec3 &angles, int32_t lifetime, bool direct)
{
    const auto particles = core.GetEntityId("particles");
    if (!particles)
    {
        spdlog::error("CheckAndCreateEffect '{}': no 'particles' entity to create {} with", id, file);
        return kInvalidHandle;
    }
    core.Send_Message(particles, "lsffffffl", direct ? PS_CREATEX : PS_CREATE, file.c_str(), position.x,
                      position.y, position.z, angles.x, angles.y, angles.z, lifetime);
    return kInvalidHandle; // particle systems have no Effekseer handle
}

// The Effekseer effect of an id, or nullopt (logged) for unknown and particle system ids.
std::optional<EffectRecord> EffekseerRecord(const char *native, const std::string &id)
{
    auto record = Catalog().Find(id);
    if (!record)
        spdlog::error("{} '{}': no such id in effects.json", native, id);
    else if (record->kind != EffectRecord::Kind::effekseer)
    {
        spdlog::error("{} '{}': {} is a particle system, not an Effekseer effect", native, id, record->file);
        record.reset();
    }
    return record;
}

void Register(const char *name, uint32_t arguments, const char *result, SIMPORTFUNC function)
{
    IFUNCINFO info;
    info.pFuncName = name;
    info.nArguments = arguments;
    info.pReturnValueName = result;
    info.pFuncAddress = function;
    core.SetScriptFunction(&info);
}
} // namespace

namespace storm::effects::natives
{

// CheckAndCreateEffect(string id, float x, y, z, float rx, ry, rz, int lifetime, bool isDirect)
//   .efkefc ids: plays the effect, returns its handle (kInvalidHandle when it cannot play).
//   .xps ids:    creates the particle system through the "particles" entity, returns kInvalidHandle.
//   other ids:   legacy particle system names (TEHO scripts, CL quests), created as "<id>.xps".
uint32_t CheckAndCreateEffect(VS_STACK *stack)
{
    const bool direct = Pop(stack)->GetInt() != 0;
    const int32_t lifetime = Pop(stack)->GetInt();
    Effects::Vec3 angles;
    angles.z = Pop(stack)->GetFloat();
    angles.y = Pop(stack)->GetFloat();
    angles.x = Pop(stack)->GetFloat();
    Effects::Vec3 position;
    position.z = Pop(stack)->GetFloat();
    position.y = Pop(stack)->GetFloat();
    position.x = Pop(stack)->GetFloat();
    const std::string id = Pop(stack)->GetString();

    const auto record = Catalog().Find(id);
    if (!record)
    {
        static std::set<std::string> reported;
        if (reported.insert(id).second)
            spdlog::info("CheckAndCreateEffect '{}': not in effects.json, created as particle system '{}.xps'", id,
                         id);
        Return(stack, CreateParticleSystem(id, id, position, angles, lifetime, direct));
        return IFUNCRESULT_OK;
    }
    if (record->kind == EffectRecord::Kind::particle_system)
    {
        Return(stack, CreateParticleSystem(id, record->file, position, angles, lifetime, direct));
        return IFUNCRESULT_OK;
    }
    const auto effects_id = core.GetEntityId("Effects");
    auto *effects = effects_id ? static_cast<Effects *>(core.GetEntityPointerSafe(effects_id)) : nullptr;
    if (!effects)
    {
        spdlog::error("CheckAndCreateEffect '{}': no 'Effects' entity to play {} with", id, record->file);
        Return(stack, kInvalidHandle);
        return IFUNCRESULT_OK;
    }
    Return(stack, effects->Play(*record, position, direct ? DirectionToAngles(angles) : angles, lifetime));
    return IFUNCRESULT_OK;
}

// ReloadEffect(string id): 1 when the effect was (re)loaded from disk.
uint32_t ReloadEffect(VS_STACK *stack)
{
    const std::string id = Pop(stack)->GetString();
    bool ok = false;
    if (const auto record = EffekseerRecord("ReloadEffect", id))
    {
        const auto path = record->EffectPath();
        if (const auto effect = efk::FindEffect(path))
            ok = efk::ReloadEffect(effect);
        else
            ok = efk::LoadEffect(path) != 0;
    }
    Return(stack, ok ? 1 : 0);
    return IFUNCRESULT_OK;
}

// ReloadAllEffects(): re-reads effects.json and every loaded effect of it.
uint32_t ReloadAllEffects(VS_STACK *)
{
    Catalog().Reload();
    for (const auto &[id, record] : Catalog().Records())
    {
        if (record.kind != EffectRecord::Kind::effekseer)
            continue;
        if (const auto effect = efk::FindEffect(record.EffectPath()); effect && !efk::ReloadEffect(effect))
            spdlog::error("ReloadAllEffects: '{}' ({}) did not reload", id, record.file);
    }
    return IFUNCRESULT_OK;
}

// UnloadEffect(string id): 1 when the effect was loaded and is now released.
uint32_t UnloadEffect(VS_STACK *stack)
{
    const std::string id = Pop(stack)->GetString();
    bool unloaded = false;
    if (const auto record = EffekseerRecord("UnloadEffect", id))
    {
        if (const auto effect = efk::FindEffect(record->EffectPath()))
        {
            efk::UnloadEffect(effect);
            unloaded = true;
        }
    }
    Return(stack, unloaded ? 1 : 0);
    return IFUNCRESULT_OK;
}

uint32_t UnloadAllEffects(VS_STACK *)
{
    efk::UnloadAllEffects();
    return IFUNCRESULT_OK;
}

} // namespace storm::effects::natives

bool EFFECTS_LIBRARY::Init()
{
    using namespace storm::effects::natives;
    Register("CheckAndCreateEffect", 9, "int", CheckAndCreateEffect);
    Register("ReloadEffect", 1, "int", ReloadEffect);
    Register("ReloadAllEffects", 0, "void", ReloadAllEffects);
    Register("UnloadEffect", 1, "int", UnloadEffect);
    Register("UnloadAllEffects", 0, "void", UnloadAllEffects);
    return true;
}
