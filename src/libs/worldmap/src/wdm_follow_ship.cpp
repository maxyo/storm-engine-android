//============================================================================================
//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    WdmFollowShip
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#include "wdm_follow_ship.h"
#include "wdm_islands.h"

// ============================================================================================
// Construction, destruction
// ============================================================================================

WdmFollowShip::WdmFollowShip()
{
    isEnemy = true;
    shipType = wdmest_follow;
    storm::math::pos(mtx).x = wdmObjects->worldSizeX;
    storm::math::pos(mtx).z = wdmObjects->worldSizeZ;
}

WdmFollowShip::~WdmFollowShip()
{
}

// Setting parameters
bool WdmFollowShip::Load(const char *modelName)
{
    if (!WdmShip::Load(modelName))
        return false;
    // Looking for a position
    float x, z, ay = 0.0f;
    killMe = !GeneratePosition(1.2f * modelRadius, 6.28f, x, z);
    if (wdmObjects->playerShip)
    {
        // Determine the angle
        const double sn = storm::math::pos(wdmObjects->playerShip->mtx).x - storm::math::pos(mtx).x;
        double cs = storm::math::pos(wdmObjects->playerShip->mtx).z - storm::math::pos(mtx).z;
        const auto l = sn * sn + cs * cs;
        if (l > 0.0)
            cs /= sqrt(l);
        if (cs > 1.0)
            cs = 1.0;
        if (cs < -1.0)
            cs = -1.0;
        cs = acos(cs);
        if (sn < 0.0)
            cs = -cs;
        ay = static_cast<float>(cs);
    }
    // put the boat
    Teleport(x, z, ay);
    return true;
}

// Find the pulling force in the desired direction
void WdmFollowShip::FindMoveForce()
{
    glm::vec3 dir;
    if (wdmObjects->islands)
    {
        wdmObjects->islands->FindDirection(storm::math::pos(mtx), storm::math::pos(static_cast<WdmShip *>(wdmObjects->playerShip)->mtx), dir);
    }
    else
    {
        // Player position
        dir.x = storm::math::pos(static_cast<WdmShip *>(wdmObjects->playerShip)->mtx).x;
        dir.z = storm::math::pos(static_cast<WdmShip *>(wdmObjects->playerShip)->mtx).z;
        // Direction to player
        dir.x -= storm::math::pos(mtx).x;
        dir.z -= storm::math::pos(mtx).z;
        auto dl = dir.x * dir.x + dir.z * dir.z;
        if (dl > 1.0f)
        {
            dl = 1.0f / sqrtf(dl);
            dir.x *= dl;
            dir.z *= dl;
        }
    }
    mx = dir.x;
    mz = dir.z;
}
