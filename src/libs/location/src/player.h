//============================================================================================
//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    Player
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "np_character.h"

class LocationCamera;

class Player : public NPCharacter
{
    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    Player();
    ~Player() override;

    bool PostInit() override;

    void Reset() override;

    // Move the character to the desired position
    void Move(float dltTime) override;
    void Update(float dltTime) override;

    // Save parameters
    void SetSaveData(ATTRIBUTES *sdata) override;
    // Restore parameters
    void GetSaveData(ATTRIBUTES *sdata) override;

    bool IsPlayer() override
    {
        return true;
    }

    void Rotate(float dltTime);
    bool GoForward(float dltTime);
    bool GoBack(float dltTime) const;
    bool IsRunMode(float dltTime);
    void StrafeWhenMove(float dltTime);
    void StrafeWhenStop(float dltTime);

    bool IsDoBlock();
    bool IsDoParry();
    bool IsDoAttackForce();
    bool IsDoAttackFast();
    bool IsDoAttackRound();
    bool IsDoAttackBreak();
    bool IsDoAttackFeint();
    bool IsFire();
    bool IsChangeFightMode();

    // Find an attacking enemy
    Player *FindAttackCharacter();

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    void FireFromShootgun();
    void ShootParticles(const CVECTOR &pos, const CVECTOR &ndir, float size, uint32_t color, int32_t num);

    float GetRotateH();

  private:
#ifdef __ANDROID__
    bool androidMovementHeld = false;
    bool androidRunTier = false;
    uint64_t androidCameraGeneration = 0;
    void TraceAndroidMovement(float dltTime);
    void StopAndroidMovement();
    float androidTraceMagnitude = 0.0f;
    float androidTraceSimulationTime = 0.0f;
    uint32_t androidTraceTick = 0;
    unsigned androidTraceTail = 0;
    bool androidTraceHasPosition = false;
    bool androidTraceHasTick = false;
    CVECTOR androidTracePosition = CVECTOR(0.0f, 0.0f, 0.0f);
#endif
    float lastChange;
    bool activatedDialog;
    bool isSpecialMode;
    bool isSetBlock;
    entid_t baterfl;
    bool shootgunMode;
    float kSMReload;
    LocationCamera *locCam;
};
