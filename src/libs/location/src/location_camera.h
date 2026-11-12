//============================================================================================
//    Spirenkov Maxim
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    LocationCamera
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "storm_glm.h"
#include "path_tracks.h"
#include "renderer.h"
#include "vma.hpp"
#include <string>
#include <vector>

#include "camera_follow.h"

class MODEL;
class Character;
class Location;

class LocationCamera : public Entity
{
    friend CameraFollow;

    enum CameraWorkMode
    {
        cwm_none = 0,
        cwm_follow,
        // Follow Camera
        cwm_topos,
        // Move camera to preset position
        cwm_free,
        // Free flying camera (debug)
    };

    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    LocationCamera();
    ~LocationCamera() override;

    // Initialization
    bool Init() override;
    // Execution
    void Execute(uint32_t delta_time);
    void Realize(uint32_t delta_time);
    // Messages
    uint64_t ProcessMessage(MESSAGE &message) override;
    // Changing an attribute
    uint32_t AttributeChanged(ATTRIBUTES *apnt) override;

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
        case Stage::execute:
            Execute(delta);
            break;
        case Stage::realize:
            Realize(delta);
            break;
            /*case Stage::lost_render:
              LostRender(delta); break;
            case Stage::restore_render:
              RestoreRender(delta); break;*/
        }
    }

    void LockFPMode(bool isLock);
    float GetAx() const;
#ifdef __ANDROID__
    // Use the actual camera, not a renderer view temporarily changed by UI/shadow draws.
    glm::vec3 MovementViewDirection() const { return lookTo - camPos; }
    bool IsThirdPersonFollow() const { return wmode == cwm_follow && !isLookMode && !isLockFPMode; }
#endif

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    // Prepare data
    bool Set();
    // Change camera mode
    void SetMode(CameraWorkMode wm);
    // Execute the camera following the character
    void ExecuteFollow(float dltTime);
    // First-person View Camera
    void ExecuteLook(float dltTime);
    // Execution of a camera observing from a point
    void ExecuteTopos(float dltTime);
    // Free flying camera execution
    void ExecuteFree(float dltTime);

    // Moving a camera from a given position to a new one
    bool MoveFollow(glm::vec3 &pos, const glm::vec3 &cpos, const glm::vec3 &to);

    // Trace the ray through the location
    float Trace(const glm::vec3 &src, const glm::vec3 &dst) const;
    bool GetCollideTriangle(TRIANGLE &trg) const;
    void Clip(PLANE *p, int32_t numPlanes, glm::vec3 &cnt, float rad, bool (*fnc)(const glm::vec3 *vtx, int32_t num)) const;

  private:
    VRenderer *rs;
    // The sea
    entid_t sea;
    // Camera parameters
    float ax; // Camera tilt angle
    float lAx;
    float vAx;           // Current speed of change
    float axmin, axmax;  // Camera tilt angle
    float teleport_dist; // Maximum distance at which the camera teleports
    float lookHeight;    // The value of where to look in height (relative)
    float radius;        // Radius of distance from the character
    float rotInertia;    // Rotational inertia
    float radInertia;    // Inertia of approaching

    bool isSpecialMode;
    bool isLockFPMode;
    bool isViewExecute;

    entid_t loc;
    Location *location;

    // Work mode
    CameraWorkMode wmode;
    bool isSleep;
    bool lockAx;

    float dAx, dAy;
#ifdef __ANDROID__
    float dragAy = 0.0f; // this frame's camera drag as a yaw angle (mobile camera mode)
    float gameRadius = 3.0f; // the game's radius (MSG_CAMERA_SET_RADIUS); radius is it scaled by the pinch
#endif

    glm::vec3 camPos, lookTo;    // Current camera parameters
    glm::vec3 oldPos, oldLookTo; // Last camera position in the previous mode
    float kMorph;              // Current morph ratio

    // Models
    entid_t chr;      // Character
    entid_t patchMdl; // Camera patch

    CameraFollow cf; // Camera following the character

    float obstructTime; // Character obstruction time
    glm::vec3 obstruct;   // After

    float freeAx, freeAy; // Free camera angles

    bool isLookMode;
    bool isTeleport;

    // Camera observing the character from a point
    glm::vec3 fromLook;

    // Temporary values at the time of execution
    Character *character; // Character pointer
    glm::vec3 pos;          // Character position
    float chay;           // Character direction
    float lheight;        // Height where to look
    float height;         // Character height
    float chradius;       // Character radius

    // dynamic change of perspective (drunkenness)
    // ~!~ rearrange this!
    struct
    {
        bool isOn;
        float fMinFov;
        float fMaxFov;
        float fCurFov;
        bool bFogUp;
        float fFogChangeSpeed;
        float fFogTimeCur;
        float fFogTimeMax;
        float fCurAngle;
        float fMaxAngle;
        float fAngleSpeed;
        bool bAngleUp;
    } dynamic_fog;

    void TurnOnDynamicFov(float fSpeed, float fTime, float fRelationMin, float fRelationMax, float fAngSpeed,
                          float fAngMax);
    void ProcessDynamicFov(float fDeltaTime, const glm::vec3 &vFrom, const glm::vec3 &vTo, glm::vec3 &vUp);
    void StoreRestoreDynamicFov(bool bStore);

    // track mode section
    bool m_bTrackMode;
    float m_fTrackCurTime;
    float m_fTrackMaxTime;
    std::string m_sCurTrackName;
    float m_fPauseTime;

    struct CameraTrackPause
    {
        float trackTime;
        float pauseTime;
    };

    std::vector<CameraTrackPause> m_aTrackPauses;
    int32_t m_nCurPauseIndex;

    bool LoadCameraTrack(const char *pcTrackFile, float fTrackTime);
    void TurnOffTrackCamera();
    void ProcessTrackCamera();
    float TrackPauseProcess();

    PathTracks m_track;

    bool forcedPos = false;

    float cameraPerspective;
};

inline void LocationCamera::LockFPMode(bool isLock)
{
    isLockFPMode = isLock;
}

inline float LocationCamera::GetAx() const
{
    return ax;
}
