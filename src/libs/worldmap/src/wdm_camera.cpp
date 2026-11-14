//============================================================================================
//    Spirenkov Maxim, 2001
//--------------------------------------------------------------------------------------------
//    Sea Dogs II
//--------------------------------------------------------------------------------------------
//    WdmCamera
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#include "wdm_camera.h"
#include "wdm_player_ship.h"

#include <algorithm>

#ifdef __ANDROID__
#include <platform/android_camera_look.hpp>
#include <platform/android_camera_zoom.hpp>

#include <time.h>
#endif

//============================================================================================

#define WDM_CAMERA_HEIGHT_MIN 50.0f
#define WDM_CAMERA_HEIGHT_MAX 300.0f
#define WdmCamera_myPI 3.1415926535897932384626433832795f

// ============================================================================================
// Construction, destruction
// ============================================================================================

WdmCamera::WdmCamera()
{
    pos = glm::vec3(0.0f, 120.0f, 0.0f);
    ang = glm::vec3(1.0f, 0.0f, 0.0f);
    wdmObjects->camera = this;
    isInit = false;
    realHeight = 0.0f;
    elasticAy = 0.0f;
    lastAng = ang.y;
    isFreeMode = false;
    realHeight = pos.y;
    hgh = pos.y;
    kBlendPos = 1.0f;
    lock = false;
}

WdmCamera::~WdmCamera()
{
    wdmObjects->camera = nullptr;
}

void WdmCamera::Init(float defAy, float defHeight)
{
    const auto pi2 = 2.0f * 3.14159265f;
    const auto k = defAy / pi2;
    ang.y = (k - static_cast<int32_t>(k)) * pi2;
    if (defHeight < WDM_CAMERA_HEIGHT_MIN)
        defHeight = WDM_CAMERA_HEIGHT_MIN;
    if (defHeight > WDM_CAMERA_HEIGHT_MAX)
        defHeight = WDM_CAMERA_HEIGHT_MAX;
    pos.y = defHeight;
    realHeight = pos.y;
}

void WdmCamera::Move(float dltTime, VRenderer *rs)
{
    const auto pi = 3.14159265f;
    if (dltTime > 0.1f)
        dltTime = 0.1f;
    CtrlProcess(dltTime);
    auto ang = this->ang;
    auto k = dltTime * 2.0f;
    if (k > 1.0f)
        k = 1.0f;
    const auto shipAy = static_cast<WdmPlayerShip *>(wdmObjects->playerShip)->GetAy();
    const auto dltAng = shipAy - elasticAy;
    if (dltAng > pi)
        elasticAy += 2.0f * pi;
    if (dltAng < -pi)
        elasticAy -= 2.0f * pi;
    elasticAy += (shipAy - elasticAy) * k;

    if (!isInit)
        elasticAy = shipAy;

    const auto freeMode = CurrentFreeMode();
    if (isFreeMode != freeMode)
    {
        if (freeMode)
        {
            lastAng = this->ang.y;
            this->ang.y = ang.y + elasticAy;
        }
        else
        {
            this->ang.y = lastAng;
            kBlendPos = 0.0f;
            bndPos = pos;
        }
        isFreeMode = freeMode;
    }
    if (!freeMode)
        ang.y += elasticAy;

    if (lock)
        ang.y = 0.0f;

    const auto cs = cosf(ang.y);
    const auto sn = sinf(ang.y);
    const auto csX = cosf(ang.x);
    const auto snX = sinf(ang.x);
    const auto movelrspd = 50.0f * MoveLeftRight(dltTime);
    const auto moveudspd = 50.0f * MoveUpDown(dltTime);
    auto rotspd = 1.0f * RotLeftRight(dltTime);
    auto zoomspd = 30.0f * ZoomInOut(dltTime);
#ifdef __ANDROID__
    // The look finger (platform/android_camera_look.hpp) moves the camera by its travel at this
    // frame's time, 1:1 while it is down and coasting on after a release. It replaces the PC's
    // mouse (a speed per frame, not the finger's travel): the camera that follows the ship turns
    // with the near ground under the finger (the mouse with the rotate key held); the free camera
    // is panned with the ground under the finger (the mouse alone, MoveLeftRight / MoveUpDown).
    storm::android::LookStep look;
    D3DMATRIX projection{};
    {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        look = storm::android::cameraLook.take(now.tv_sec * 1000.0 + now.tv_nsec / 1e6);
        if (look.moved())
            rs->GetTransform(D3DTS_PROJECTION, &projection);
        if (look.moved() && !freeMode)
            rotspd += storm::android::turnAngle(look.dx / look.width, ang.x, projection._11, projection._22);
    }
    // The pinch of the camera look (platform/android_camera_zoom.hpp) moves the height the wheel
    // moves (WMapForward / WMapBackward), along the same line of sight and within the same
    // limits, as many times lower as the fingers spread. The free camera has no zoom
    // (GetHightHeight), so a pinch there reaches no camera and is reported.
    if (float freeHeight; !GetHightHeight(freeHeight))
        if (const float scale = storm::android::cameraZoom.take(); scale != 1.0f)
            zoomspd += (pos.y - storm::android::pinchDistance(pos.y, scale, WDM_CAMERA_HEIGHT_MIN, WDM_CAMERA_HEIGHT_MAX)) / snX;
#endif
    // The point at height 0 at which we are looking relative to the current camera position
    const auto cx = sn * pos.y * csX / snX;
    const auto cz = cs * pos.y * csX / snX;
    auto r = sqrtf(cx * cx + cz * cz);
    // Moving free camera
    if (CurrentFreeMode() && isInit)
    {
        pos.x += moveudspd * sn - movelrspd * cs;
        pos.z += moveudspd * cs + movelrspd * sn;
#ifdef __ANDROID__
        // The camera as it is drawn (realHeight: the eye; ang: the angles SetCamera gets)
        // moves along the ground by the finger's travel.
        if (float shiftX, shiftZ; look.moved() &&
            storm::android::panShift({realHeight, ang.x, ang.y, projection._11, projection._22}, look, shiftX, shiftZ))
        {
            pos.x += shiftX;
            pos.z += shiftZ;
        }
#endif
        // The point it looks at (pos + (cx, cz)) stays over the world.
        pos.x = std::clamp(pos.x, -0.5f * wdmObjects->worldSizeX - cx, 0.5f * wdmObjects->worldSizeX - cx);
        pos.z = std::clamp(pos.z, -0.5f * wdmObjects->worldSizeZ - cz, 0.5f * wdmObjects->worldSizeZ - cz);
    }
    else
    {
        pos.x = storm::math::pos(wdmObjects->playerShip->mtx).x - cx;
        pos.z = storm::math::pos(wdmObjects->playerShip->mtx).z - cz;
        isInit = true;
    }
    // Camera rotation
    this->ang.y += rotspd;
    pos.x = pos.x + cx - r * sinf(ang.y);
    pos.z = pos.z + cz - r * cosf(ang.y);
    // Scaling
    pos.x += zoomspd * sn * csX;
    pos.y -= zoomspd * snX;
    pos.z += zoomspd * cs * csX;
    if (pos.y < WDM_CAMERA_HEIGHT_MIN)
    {
        r = (WDM_CAMERA_HEIGHT_MIN - pos.y) * csX / snX;
        pos.y = WDM_CAMERA_HEIGHT_MIN;
        pos.x -= r * sn;
        pos.z -= r * cs;
    }
    else if (pos.y > WDM_CAMERA_HEIGHT_MAX)
    {
        r = (WDM_CAMERA_HEIGHT_MAX - pos.y) * csX / snX;
        pos.y = WDM_CAMERA_HEIGHT_MAX;
        pos.x -= r * sn;
        pos.z -= r * cs;
    }
    // Limiting camera movement
    CheckRange();
    // Set the new camera position
    float height;
    k = dltTime * 2000.0f;
    if (!GetHightHeight(height))
    {
        height = pos.y;
    }
    if (realHeight <= height)
    {
        realHeight += k;
        if (realHeight > height)
            realHeight = height;
    }
    else
    {
        realHeight -= k * 2.0f;
        if (realHeight < height)
            realHeight = height;
    }
    r = (pos.y - realHeight) * csX / snX;
    auto ps = pos;
    kBlendPos += dltTime * 8.0f;
    if (kBlendPos < 1.0f)
    {
        ps.x = bndPos.x + (pos.x - bndPos.x) * kBlendPos;
        ps.z = bndPos.z + (pos.z - bndPos.z) * kBlendPos;
    }
    else
        kBlendPos = 1.0f;
    rs->SetCamera(glm::vec3(ps.x + r * sn, realHeight, ps.z + r * cs), ang);
}

void WdmCamera::CheckRange()
{
    const auto cs = cosf(ang.y);
    const auto sn = sinf(ang.y);
    const auto csX = cosf(ang.x);
    const auto snX = sinf(ang.x);
    // The point we're looking at
    const auto cx = pos.x + sn * pos.y * csX / snX;
    const auto cz = pos.z + cs * pos.y * csX / snX;
    const auto ccx = cx, ccz = cz;
    // Limit the coordinates
    // if(cx < -0.5f*WDM_WORLD_SIZE_X) cx = -0.5f*WDM_WORLD_SIZE_X;
    // if(cx > 0.5f*WDM_WORLD_SIZE_X) cx = 0.5f*WDM_WORLD_SIZE_X;
    // if(cz < -0.5f*WDM_WORLD_SIZE_Z) cz = -0.5f*WDM_WORLD_SIZE_Z;
    // if(cz > 0.5f*WDM_WORLD_SIZE_Z) cz = 0.5f*WDM_WORLD_SIZE_Z;
    // Calculating the offset
    pos.x += cx - ccx;
    pos.z += cz - ccz;
}
