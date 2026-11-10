#include "effects.hpp"

#include "core.h"
#include "dx9render.h"
#include "message.h"
#include "shared/layers.h"
#include "shared/messages.h"
#include "v_data.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>

namespace efk = storm::render::effekseer;

namespace
{
// After the opaque scene, like the game's particles (Program/particles.c puts them at 65536).
constexpr priority_t kRealizePriority = 65536;

uint64_t Result(Effects::InstanceId instance)
{
    // Script int: -1 (INVALID_EFF_HANDLE) on failure.
    return static_cast<uint64_t>(static_cast<int64_t>(instance));
}

Effects::Vec3 ReadVec3(MESSAGE &message)
{
    Effects::Vec3 v;
    v.x = message.Float();
    v.y = message.Float();
    v.z = message.Float();
    return v;
}

void SetFloat(VDATA *variable, float value, const char *what)
{
    if (!variable)
        throw std::runtime_error(std::string("Effects: ") + what + " needs a float variable reference");
    variable->Set(value);
}
} // namespace

Effects::~Effects()
{
    // The instances belong to this entity; the loaded effects stay cached for the next one.
    efk::StopAll();
}

bool Effects::Init()
{
    efk::Settings settings;
    settings.read_file = [](const std::string &path, std::vector<uint8_t> &data) {
        std::string bytes;
        if (!storm::effects::ReadGameFile(path, bytes))
            return false;
        data.assign(bytes.begin(), bytes.end());
        return true;
    };
    efk::Configure(settings);

    // Scripts create the entity without layers. Land and sea layers are never active together
    // (the scripts freeze one pair when they unfreeze the other), so each frame advances once.
    core.AddToLayer(EXECUTE, GetId(), 0);
    core.AddToLayer(SEA_EXECUTE, GetId(), 0);
    core.AddToLayer(REALIZE, GetId(), kRealizePriority);
    core.AddToLayer(SEA_REALIZE, GetId(), kRealizePriority);
    return true;
}

void Effects::ProcessStage(Stage stage, uint32_t delta)
{
    switch (stage)
    {
    case Stage::execute:
        Execute(delta);
        break;
    case Stage::realize:
        Realize();
        break;
    default:
        break;
    }
}

void Effects::Execute(uint32_t delta_ms)
{
    efk::Update(static_cast<float>(delta_ms) * 0.001f);
    const auto elapsed = static_cast<int32_t>(delta_ms);
    std::erase_if(lifetimes_, [elapsed](Lifetime &entry) {
        if (!efk::Exists(entry.instance))
            return true;
        entry.remaining_ms -= elapsed;
        if (entry.remaining_ms > 0)
            return false;
        efk::StopRoot(entry.instance);
        return true;
    });
}

void Effects::Realize()
{
    if (efk::GetStats().instances == 0)
        return;
    if (!renderer_)
    {
        renderer_ = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
        if (!renderer_)
            throw std::runtime_error("Effects: no dx9render service to draw the effects with");
    }
    D3DMATRIX view, projection;
    renderer_->GetTransform(D3DTS_VIEW, &view);
    renderer_->GetTransform(D3DTS_PROJECTION, &projection);
    efk::Draw(static_cast<IDirect3DDevice9 *>(renderer_->GetD3DDevice()), &view._11, &projection._11);
}

Effects::InstanceId Effects::PlayFile(const std::string &path, const Vec3 &position)
{
    const auto effect = efk::LoadEffect(path);
    if (effect == 0)
        return kInvalid; // LoadEffect logged why
    return efk::Play(effect, position);
}

Effects::InstanceId Effects::Play(const storm::effects::EffectRecord &record, const Vec3 &position,
                                  const Vec3 &angles, int32_t lifetime_ms)
{
    const auto instance = PlayFile(record.EffectPath(), position);
    if (instance == kInvalid)
        return kInvalid;
    efk::SetRotation(instance, angles);
    efk::SetSpeed(instance, record.speed);
    if (lifetime_ms > 0)
        lifetimes_.push_back({instance, lifetime_ms});
    return instance;
}

Effects::InstanceId Effects::PlayId(MESSAGE &message)
{
    // "s" id, "fff" position, optional "fff" angles, "fff" scale, "l" start frame.
    const std::string id = message.String();
    const auto position = ReadVec3(message);
    const auto params = message.GetFormat().size();
    const auto record = storm::effects::Catalog().Find(id);
    if (!record || record->kind != storm::effects::EffectRecord::Kind::effekseer)
    {
        spdlog::error("Effects: MSG_EFFECT_PLAY '{}': {}", id,
                      record ? "the id names a particle system (.xps), not an Effekseer effect"
                             : "no such id in effects.json");
        return kInvalid;
    }
    const auto instance = Play(*record, position, {}, 0);
    if (params >= 8)
    {
        const auto angles = ReadVec3(message);
        if (instance != kInvalid)
            efk::SetRotation(instance, angles);
    }
    if (params >= 11)
    {
        const auto scale = ReadVec3(message);
        if (instance != kInvalid)
            efk::SetScale(instance, scale);
    }
    if (params >= 12)
    {
        // No script of CL 2.0 passes a start frame; the engine API cannot start mid-effect.
        if (const auto frame = message.Long(); frame != 0)
            spdlog::error("Effects: MSG_EFFECT_PLAY '{}': start frame {} is not supported, the effect starts at 0",
                          id, frame);
    }
    return instance;
}

uint64_t Effects::ProcessMessage(MESSAGE &message)
{
    const auto code = message.Long();
    switch (code)
    {
    case MSG_EFFECT_PLAY:
        return Result(PlayId(message));
    case MSG_EFFECT_PLAY_FILE: {
        const std::string path = message.String();
        return Result(PlayFile(path, ReadVec3(message)));
    }
    case MSG_EFFECT_DELETE: {
        const auto instance = message.Long();
        efk::Stop(instance);
        std::erase_if(lifetimes_, [instance](const Lifetime &entry) { return entry.instance == instance; });
        return 0;
    }
    case MSG_EFFECT_DELETE_ALL:
        efk::StopAll();
        lifetimes_.clear();
        return 0;
    case MSG_EFFECT_STOP_ROOT:
        efk::StopRoot(message.Long());
        return 0;
    case MSG_EFFECT_SET_PARAMETER: {
        const auto instance = message.Long();
        const auto index = message.Long();
        efk::SetDynamicInput(instance, index, message.Float());
        return 0;
    }
    case MSG_EFFECT_GET_PARAMETER: {
        const auto instance = message.Long();
        const auto index = message.Long();
        SetFloat(message.ScriptVariablePointer(), efk::GetDynamicInput(instance, index), "MSG_EFFECT_GET_PARAMETER");
        return 0;
    }
    case MSG_EFFECT_SEND_TRIGGER: {
        const auto instance = message.Long();
        efk::SendTrigger(instance, message.Long());
        return 0;
    }
    case MSG_EFFECT_IS_PLAYING:
        return efk::Exists(message.Long()) ? 1 : 0;
    case MSG_EFFECT_PAUSE: {
        const auto instance = message.Long();
        efk::SetPaused(instance, message.Long() != 0);
        return 0;
    }
    case MSG_EFFECT_UPDATE_POS: {
        const auto instance = message.Long();
        efk::SetLocation(instance, ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_ADD_SHIFT: {
        const auto instance = message.Long();
        efk::AddLocation(instance, ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_GET_POS: {
        const auto position = efk::GetLocation(message.Long());
        SetFloat(message.ScriptVariablePointer(), position.x, "MSG_EFFECT_GET_POS");
        SetFloat(message.ScriptVariablePointer(), position.y, "MSG_EFFECT_GET_POS");
        SetFloat(message.ScriptVariablePointer(), position.z, "MSG_EFFECT_GET_POS");
        return 0;
    }
    case MSG_EFFECT_UPDATE_ROT: {
        const auto instance = message.Long();
        efk::SetRotation(instance, ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_SET_TARGET_POS: {
        const auto instance = message.Long();
        efk::SetTargetLocation(instance, ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_SET_SCALE: {
        const auto instance = message.Long();
        efk::SetScale(instance, ReadVec3(message));
        return 0;
    }
    case MSG_EFFECT_SET_SPEED: {
        const auto instance = message.Long();
        efk::SetSpeed(instance, message.Float());
        return 0;
    }
    case MSG_EFFECT_GET_SPEED: {
        const auto speed = efk::GetSpeed(message.Long());
        SetFloat(message.ScriptVariablePointer(), speed, "MSG_EFFECT_GET_SPEED");
        return 0;
    }
    case MSG_EFFECT_SET_COLOR: {
        const auto instance = message.Long();
        const auto argb = static_cast<uint32_t>(message.Long());
        efk::SetColor(instance, static_cast<uint8_t>(argb >> 16), static_cast<uint8_t>(argb >> 8),
                      static_cast<uint8_t>(argb), static_cast<uint8_t>(argb >> 24));
        return 0;
    }
    default:
        spdlog::error("Effects: message {} is not handled", code);
        return 0;
    }
}
