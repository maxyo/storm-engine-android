#include "fire_camera.h"

#include "controls.h"
#include "geos.h"
#include "math_inlines.h"
#include "model.h"
#include "save_load.h"
#include "shared/messages.h"
#include "string_compare.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
constexpr float kPi = 3.14159265358979f;
} // namespace

bool FIRE_CAMERA::SideSector(char side, Sector &sector)
{
    switch (side)
    {
    case 'f':
        sector = {0.0f, kPi / 6.0f};
        return true;
    case 'r':
        sector = {kPi / 2.0f, kPi / 3.0f};
        return true;
    case 'b':
        sector = {kPi, kPi / 6.0f};
        return true;
    case 'l':
        sector = {-kPi / 2.0f, kPi / 3.0f};
        return true;
    default:
        return false;
    }
}

bool FIRE_CAMERA::Init()
{
    renderer_ = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
    if (!renderer_)
        throw std::runtime_error("FIRE_CAMERA: no dx9render service");
    return true;
}

void FIRE_CAMERA::SetCharacter(ATTRIBUTES *character)
{
    pACharacter = character;
    locator_found_ = false;
    follow_aim_ = false;
    const char *bort = AttributesPointer ? static_cast<const char *>(AttributesPointer->GetAttribute("Bort")) : nullptr;
    const char side = bort && *bort ? bort[std::char_traits<char>::length(bort) - 1] : '\0';
    if (!SideSector(side, sector_))
    {
        spdlog::error("FIRE_CAMERA: Bort '{}' is not a side (l, r, f, b); aiming to the right", bort ? bort : "");
        SideSector('r', sector_);
        side_ = 'r';
    }
    else
        side_ = side;
    yaw_ = sector_.yaw;
    pitch_ = 0.0f;
}

uint32_t FIRE_CAMERA::AttributeChanged(ATTRIBUTES *attribute)
{
    const auto value = attribute->GetAttributeAsFloat();
    if (*attribute == "SensivityHeightAngle")
        sensivity_height_ = value;
    else if (*attribute == "SensivityAzimuthAngle")
        sensivity_azimuth_ = value;
    else if (*attribute == "MaxAngleX")
        max_angle_x_ = value;
    else if (*attribute == "MinAngleX")
        min_angle_x_ = value;
    else if (*attribute == "RockingX")
        rocking_x_ = value;
    else if (*attribute == "RockingZ")
        rocking_z_ = value;
    else if (*attribute == "MouseSpeedAy")
        mouse_speed_ay_ = value;
    // Perspective is read every frame; Bort and LastCamera are read on the switch (script data).
    return 0;
}

uint64_t FIRE_CAMERA::ProcessMessage(MESSAGE &message)
{
    const auto code = message.Long();
    if (code == MSG_FIRE_CAMERA_SET_AIM_PITCH)
    {
        follow_aim_ = message.Long() != 0;
        aim_pitch_ = message.Float();
        return 0;
    }
    spdlog::error("FIRE_CAMERA: message {} is not handled", code);
    return 0;
}

bool FIRE_CAMERA::FindLocator()
{
    auto *model = GetModelPointer();
    if (!model)
        return false;
    const std::string wanted = std::string("cam_") + side_;
    NODE *node;
    for (uint32_t index = 0; (node = model->GetNode(index)); ++index)
    {
        GEOS::INFO info;
        node->geo->GetInfo(info);
        for (int32_t i = 0; i < info.nlabels; ++i)
        {
            GEOS::LABEL label;
            node->geo->GetLabel(i, label);
            if (label.name && storm::iEquals(wanted, label.name))
            {
                locator_ = glm::vec3(label.m[3][0], label.m[3][1], label.m[3][2]);
                return true;
            }
        }
    }
    const std::string problem = std::to_string(GetEID()) + wanted;
    if (reported_ != problem)
    {
        reported_ = problem;
        spdlog::error("FIRE_CAMERA: the ship model has no locator '{}'; the fire camera does not move", wanted);
    }
    return false;
}

void FIRE_CAMERA::ProcessStage(Stage stage, uint32_t delta)
{
    if (stage == Stage::execute)
        Execute(delta);
}

void FIRE_CAMERA::Execute(uint32_t delta_ms)
{
    if (!isOn() || !pACharacter || !FindShip())
        return;
    SetPerspective(AttributesPointer->GetAttributeAsFloat("Perspective", GetPerspective()));
    if (!isActive())
        return;
    if (!locator_found_ && !(locator_found_ = FindLocator()))
        return;

    CONTROL_STATE cs;
    core.Controls->GetControlState("FireCamera_Turn_H", cs);
    yaw_ += sensivity_azimuth_ * 3.0f * mouse_speed_ay_ * cs.fValue;
    core.Controls->GetControlState("FireCamera_Turn_V", cs);
    pitch_ -= sensivity_height_ * 3.0f * cs.fValue;
    yaw_ = std::clamp(yaw_, sector_.yaw - sector_.half_width, sector_.yaw + sector_.half_width);
    pitch_ = std::clamp(follow_aim_ ? aim_pitch_ : pitch_, min_angle_x_, max_angle_x_);

    const glm::mat4 &ship = *GetAIObj()->GetMatrix();
    const glm::vec3 forward = storm::math::vz(ship);
    const glm::vec3 right = storm::math::vx(ship);
    const float ship_yaw = std::atan2(forward.x, forward.z);
    const float ship_pitch = std::atan2(-forward.y, std::sqrt(forward.x * forward.x + forward.z * forward.z));
    const float ship_roll = std::atan2(right.y, std::sqrt(right.x * right.x + right.z * right.z));

    const glm::vec3 position = storm::math::transformPoint(ship, locator_);
    const glm::vec3 angles(pitch_ + ship_pitch * rocking_x_, ship_yaw + yaw_, ship_roll * rocking_z_);
    renderer_->SetCamera(position, angles, GetPerspective());
}

void FIRE_CAMERA::Save(CSaveLoad *pSL)
{
    pSL->SaveFloat(sensivity_height_);
    pSL->SaveFloat(sensivity_azimuth_);
    pSL->SaveFloat(max_angle_x_);
    pSL->SaveFloat(min_angle_x_);
    pSL->SaveFloat(rocking_x_);
    pSL->SaveFloat(rocking_z_);
    pSL->SaveFloat(mouse_speed_ay_);
    pSL->SaveDword(static_cast<uint32_t>(side_));
    pSL->SaveFloat(yaw_);
    pSL->SaveFloat(pitch_);
    pSL->SaveDword(follow_aim_);
    pSL->SaveFloat(aim_pitch_);
    pSL->SaveDword(isOn());
    pSL->SaveDword(isActive());
    pSL->SaveFloat(GetPerspective());
    pSL->SaveAPointer("character", pACharacter);
}

void FIRE_CAMERA::Load(CSaveLoad *pSL)
{
    sensivity_height_ = pSL->LoadFloat();
    sensivity_azimuth_ = pSL->LoadFloat();
    max_angle_x_ = pSL->LoadFloat();
    min_angle_x_ = pSL->LoadFloat();
    rocking_x_ = pSL->LoadFloat();
    rocking_z_ = pSL->LoadFloat();
    mouse_speed_ay_ = pSL->LoadFloat();
    side_ = static_cast<char>(pSL->LoadDword());
    if (!SideSector(side_, sector_))
        throw std::runtime_error("FIRE_CAMERA: saved side is not l, r, f or b");
    yaw_ = pSL->LoadFloat();
    pitch_ = pSL->LoadFloat();
    follow_aim_ = pSL->LoadDword() != 0;
    aim_pitch_ = pSL->LoadFloat();
    SetOn(pSL->LoadDword() != 0);
    SetActive(pSL->LoadDword() != 0);
    SetPerspective(pSL->LoadFloat());
    pACharacter = pSL->LoadAPointer("character");
    locator_found_ = false;
}
