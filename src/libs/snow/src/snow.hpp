// "snow": Caribbean Legend's snow and dust flakes around the camera (Program/weather/WhrSnow.c).
//
// Script contract: created with CreateEntity(&Snow, "snow") and put on REALIZE only (it animates
// in realize). Optional attributes, read at creation and whenever a script changes them:
//   flakeTexture      texture under Resource/Textures   (default "snowflakes.tga")
//   flakesMax         number of flakes                   (default 2048)
//   fallSpeedMin/Max  fall speed, m/s                    (default 0.8 / 1.6)
//   visibilityRadius  flakes live within this radius     (default 12 m)
//   visibilityHeight  ... and this height around the eye (default 8 m)
// The defaults are ours (CL's SnowData() sets none); the dust preset sets all of them. A value
// outside its range is logged as an error and replaced by the default.
// No messages: the script's SNOW_TEST is never sent; any message is logged as not handled.
#pragma once

#include "dx9render.h"
#include "entity.h"
#include "storm_glm.h" // GLM only through this header (its configuration and packing)

#include <cstdint>
#include <string>
#include <vector>


class SNOW final : public Entity
{
  public:
    ~SNOW() override;

    bool Init() override;
    void ProcessStage(Stage stage, uint32_t delta) override;
    uint64_t ProcessMessage(MESSAGE &message) override;
    uint32_t AttributeChanged(ATTRIBUTES *attribute) override;

    struct Settings
    {
        std::string texture = "snowflakes.tga";
        uint32_t flakes = 2048;
        float fall_min = 0.8f, fall_max = 1.6f;
        float radius = 12.0f, height = 8.0f;
    };
    struct Flake
    {
        glm::vec3 pos;
        float speed;
        float phase;
    };

    // Moves the flakes by `seconds` and keeps them in the box around `eye` (wraps them around).
    static void Advance(std::vector<Flake> &flakes, const Settings &settings, const glm::vec3 &eye, float seconds,
                        float time);

  private:
    void ReadSettings();
    void Respawn(const glm::vec3 &eye);
    void Realize(uint32_t delta_ms);

    VDX9RENDER *renderer_ = nullptr;
    Settings settings_;
    int32_t texture_ = -1;
    bool dirty_ = true;
    float time_ = 0.0f;
    std::vector<Flake> flakes_;
    std::vector<RS_RECT> rects_;
};
