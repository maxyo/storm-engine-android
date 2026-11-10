// "Effects": the Caribbean Legend Effekseer entity (see ../README.md for the script contract).
#pragma once

#include "effect_catalog.hpp"

#include "entity.h"
#include "storm/render/effekseer.hpp"

#include <cstdint>
#include <vector>

class VDX9RENDER;

class Effects final : public Entity
{
  public:
    using InstanceId = storm::render::effekseer::InstanceId;
    using Vec3 = storm::render::effekseer::Vec3;
    static constexpr InstanceId kInvalid = storm::render::effekseer::kInvalidInstance;

    ~Effects() override;

    bool Init() override;
    void ProcessStage(Stage stage, uint32_t delta) override;
    uint64_t ProcessMessage(MESSAGE &message) override;

    // Plays an Effekseer effect of the table: applies its speed, the Euler angles (radians) and
    // stops spawning after `lifetime_ms` (0 = until the effect ends by itself). kInvalid on failure.
    InstanceId Play(const storm::effects::EffectRecord &record, const Vec3 &position, const Vec3 &angles,
                    int32_t lifetime_ms);

  private:
    struct Lifetime
    {
        InstanceId instance;
        int32_t remaining_ms;
    };

    InstanceId PlayFile(const std::string &path, const Vec3 &position);
    InstanceId PlayId(MESSAGE &message);
    void Execute(uint32_t delta_ms);
    void Realize();

    std::vector<Lifetime> lifetimes_;
    VDX9RENDER *renderer_ = nullptr;
};
