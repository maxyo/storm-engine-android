// FIRE_CAMERA: Caribbean Legend's first-person aiming camera at a ship's side.
//
// Script contract (Program/sea_ai/AICameras.c of the game):
//   created next to the other sea cameras, put on SEA_EXECUTE and added to SEA_CAMERAS with
//   AI_CAMERAS_ADD_CAMERA; switched on with AI_CAMERAS_SET_CAMERA. Before the switch the script
//   sets `Bort` to the side it aims at ("l", "r", "f", "b": the last letter picks the ship model
//   locator "cam_<letter>" and the look direction; any other value is logged and aims right).
//   Attributes: Perspective, SensivityHeightAngle, SensivityAzimuthAngle, MaxAngleX, MinAngleX
//   (pitch limits), RockingX, RockingZ (how much of the ship's pitch / roll the view follows),
//   MouseSpeedAy (azimuth speed factor).
//   MSG_FIRE_CAMERA_SET_AIM_PITCH "lf": follow the aim (non-zero) at this pitch in radians;
//   0 gives the pitch back to the player.
// The view turns with the controls FireCamera_Turn_H / FireCamera_Turn_V within the side's
// sector (sides 120 degrees, bow and stern 60 degrees, as the script picks the side).
//
// Not implemented: the reticle, target lock and focus messages (MSG_FIRE_CAMERA_SET_RETICLE,
// _SET_TARGET, _GET_TARGET_LOCK, _GET_FOCUS_STATE, _SET_CAMERA_AY_LIMITS,
// _GET_CAMERA_POS_AND_ANG) belong to CL's engine-side cannon aiming, which our sea_ai does not
// have; no script sends them. Any message other than SET_AIM_PITCH is logged as not handled.
#pragma once

#include "common_camera.h"
#include "dx9render.h"

#include <string>

class FIRE_CAMERA : public COMMON_CAMERA
{
  public:
    bool Init() override;
    void ProcessStage(Stage stage, uint32_t delta) override;
    uint32_t AttributeChanged(ATTRIBUTES *attribute) override;
    uint64_t ProcessMessage(MESSAGE &message) override;
    void SetCharacter(ATTRIBUTES *character) override;

    void Save(CSaveLoad *pSL) override;
    void Load(CSaveLoad *pSL) override;

    // Base yaw (radians, relative to the ship's heading) and half-width of a side's sector.
    struct Sector
    {
        float yaw;
        float half_width;
    };
    static bool SideSector(char side, Sector &sector);

  private:
    void Execute(uint32_t delta_ms);
    bool FindLocator();

    VDX9RENDER *renderer_ = nullptr;
    float sensivity_height_ = 0.0015f, sensivity_azimuth_ = 0.0015f;
    float max_angle_x_ = 0.3f, min_angle_x_ = -0.3f;
    float rocking_x_ = 0.0f, rocking_z_ = 0.0f;
    float mouse_speed_ay_ = 1.0f;

    char side_ = 'r';
    Sector sector_{};
    float yaw_ = 0.0f, pitch_ = 0.0f; // relative to the ship
    bool follow_aim_ = false;
    float aim_pitch_ = 0.0f;
    glm::vec3 locator_{}; // in ship space
    bool locator_found_ = false;
    std::string reported_; // last locator problem logged, to log it once per ship and side
};
