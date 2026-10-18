//============================================================================================
//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    Player
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#include "player.h"

#include "location.h"
#include "location_camera.h"
#include "collide.h"
#include "shared/messages.h"
#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
#include <platform/android_ui.hpp>
#include <platform/android_movement.hpp>
#endif

// ============================================================================================
// Construction, destruction
// ============================================================================================

Player::Player()
{
    baterfl = {};
    lastChange = 10.0f;
    task.task = npct_none;
    activatedDialog = false;
    CmdNone();
    isSpecialMode = false;
    isSetBlock = false;
    kSMReload = 1.0f;
    locCam = nullptr;
    shootgunMode = false;
    // shootgunMode = false;
}

Player::~Player()
{
    entid_t peid;
    if (peid = core.GetEntityId("ShootGunParticles"))
        core.EraseEntity(peid);
}

bool Player::PostInit()
{
    auto *const location = GetLocation();
    if (!location->supervisor.player)
        location->supervisor.player = this;
    baterfl = core.GetEntityId("Animals");
    return NPCharacter::PostInit();
}

void Player::Reset()
{
    if (fgtCurType == fgt_block && !isSetBlock)
        fgtCurType = fgt_none;
    NPCharacter::Reset();
}

// Move the character to the desired position
void Player::Move(float dltTime)
{
#ifdef __ANDROID__
    const auto traceCombat = [this](const char *action) {
        static unsigned budget = 192;
        if (!budget) return;
        char setting[PROP_VALUE_MAX] = {};
        __system_property_get("log.tag.StormCombat", setting);
        if (std::string_view(setting) != "DEBUG") return;
        --budget;
        auto *energy = AttributesPointer ? AttributesPointer->FindAClass(AttributesPointer, "chr_ai.energy") : nullptr;
        const std::string energyText = energy && energy->HasValue() ? std::string(energy->GetValue()) : "<absent>";
        __android_log_print(ANDROID_LOG_DEBUG, "StormCombat", "player=%llu action=%s setType=%d currentType=%d energy=%s priority=%s",
            static_cast<unsigned long long>(GetId()), action, static_cast<int>(fgtSetType), static_cast<int>(fgtCurType),
            energyText.c_str(), priorityAction.name ? priorityAction.name : "none");
    };
#else
    const auto traceCombat = [](const char *) {};
#endif
#ifdef __ANDROID__
    androidMotion = false;
    androidMotionApplied = false;
    // Poll even during a scripted task: hard context invalidation and lease
    // expiry must not leave a sample waiting to resume after that task ends.
    const auto androidAxes = GetId() == core.GetEntityId("Player") ?
        storm::android::movementInput.read(storm::android::movementTick()) : std::nullopt;
    if (task.task != npct_none || command.cmd != aicmd_none || GetId() != core.GetEntityId("Player"))
    {
        androidMotionStop = false;
        androidRunTier = false;
        if (androidAxes) storm::android::movementInput.resetActivity();
    }
    else if ((androidMovementHeld && !androidAxes) || (androidAxes && androidAxes->magnitude == 0.0f))
    {
        // StopMove only requests idle; bspeed still holds the full native gait
        // during blending. Do not let that residual speed replace a slow touch
        // vector after release, zero input, expiry or hard cancellation.
        StopAndroidMovement();
    }
    const auto cameraGeneration = storm::android::movementInput.generation();
    if (cameraGeneration != androidCameraGeneration || task.task != npct_none ||
        command.cmd != aicmd_none || GetId() != core.GetEntityId("Player") ||
        (locCam && !locCam->IsThirdPersonFollow()))
        androidCameraRelative = false;
    androidCameraGeneration = cameraGeneration;
    if (androidAxes && task.task == npct_none && command.cmd == aicmd_none &&
        (!locCam || locCam->IsThirdPersonFollow()))
        androidCameraRelative = true;
    androidMovementHeld = androidAxes.has_value();
    androidTraceMagnitude = androidAxes ? androidAxes->magnitude : 0.0f;
#endif
    kSMReload += dltTime * 0.7f;
    if (kSMReload > 1.0f)
        kSMReload = 1.0f;
    if (!locCam)
    {
        entid_t lcam;
        if (lcam = core.GetEntityId("LocationCamera"))
        {
            locCam = static_cast<LocationCamera *>(core.GetEntityPointer(lcam));
        }
    }
    else
    {
        locCam->LockFPMode(shootgunMode);
    }
    // tuner.isVisible = !shootgunMode;

    const auto oldSGMode = shootgunMode;
    shootgunMode = false;
    auto *vd = core.Event("EventSGMode");
    if (vd)
    {
        int32_t data = 0;
        if (vd->Get(data))
            shootgunMode = (data != 0);
    }
    if (oldSGMode != shootgunMode)
    {
        if (shootgunMode)
        {
            core.Send_Message(effects, "s", "SGInited");
            tuner.alpha = 0.0f;
            tuner.camAlpha = 0.0f;
            isFight = true;
        }
        else
        {
            core.Send_Message(effects, "s", "SGRelease");
            tuner.alpha = 1.0f;
            tuner.camAlpha = 1.0f;
            isFight = false;
        }
    }

    /*
    if(core.Controls->GetAsyncKeyState(VK_SPACE) < 0)
    {
      impulse.y += 5.0f;
      impulse.y *= 2;
    }*/

    if (task.task == npct_none)
    {
        isEnableJump = true;
        //------------------------------------
        Rotate(dltTime);
        //------------------------------------
        if (lastChange > 0.3f)
        {
            // lastChange = 0.0f;
#ifdef __ANDROID__
            if (androidAxes && command.cmd == aicmd_none && !shootgunMode && !isSwim && !isJump)
            {
                const auto &axes = *androidAxes;
                if (axes.magnitude == 0.0f)
                {
                    StopAndroidMovement();
                }
                else if (IsFight() && axes.y < -0.22f)
                {
                    // Backstep and side dodges are native combat actions, not
                    // ordinary locomotion whose impulses may be rescaled.
                    StopAndroidMovement();
                    if (axes.magnitude > .22f) Recoil();
                }
                else if (IsFight() && std::fabs(axes.y) < .22f)
                {
                    StopAndroidMovement();
                    if (axes.magnitude > .22f)
                    {
                        if (axes.x < 0) StrafeLeft(); else StrafeRight();
                    }
                }
                else
                {
                    // Explore input turns the actor towards its travel vector;
                    // moving towards the camera is a forward gait after a turn,
                    // never a backwards gait selected from the screen-space Y.
                    const bool cameraRelative = UsesTouchCamera();
                    const bool back = !cameraRelative && axes.y < 0;
                    const float walking = IsFight() ? fightwalk.speed : (back ? backwalk.speed : walk.speed);
                    const float running = IsFight() ? fightrun.speed : (back ? backrun.speed : run.speed);
                    const auto pace = storm::android::movementPace(axes.magnitude, walking, running,
                        axes.runAllowed && !isRunDisable, androidRunTier);
                    androidRunTier = pace.run;
                    SetRunMode(pace.run);
                    StartMove(back);
                    androidMotionStop = false;
                    strafeMove = cameraRelative ? 0.0f : axes.x;
                    // Combat stays relative to the fighter, like native
                    // recoil/strafe. Camera inertia cannot reverse its forward gait.
                    const CVECTOR forward = cameraRelative && locCam ? locCam->MovementViewDirection() :
                        CVECTOR(sinf(ay), 0.0f, cosf(ay));
                    const auto direction = storm::android::movementDirection(axes, forward.x, forward.z, ay);
                    androidMotionDirection = CVECTOR(direction.x, 0.0f, direction.z);
                    androidMotionSpeed = pace.speed;
                    androidMotionWalkSpeed = std::fabs(walking);
                    androidMotion = true;
                }
            }
            else
#endif
            {
                SetRunMode(IsRunMode(dltTime));
                if (GoForward(dltTime))
                {
#ifdef __ANDROID__
                    androidMotionStop = false;
                    androidCameraRelative = false;
#endif
                    StartMove(false);
                    StrafeWhenMove(dltTime);
                }
                else if (GoBack(dltTime))
                {
#ifdef __ANDROID__
                    androidMotionStop = false;
                    androidCameraRelative = false;
#endif
                    if (!IsFight())
                    {
                        StartMove(true);
                        StrafeWhenMove(dltTime);
                    }
                    else
                    {
                        Recoil();
                    }
                }
                else
                {
#ifdef __ANDROID__
                    if (androidMovementHeld) StopAndroidMovement();
                    else
#endif
                        StopMove();
                    StrafeWhenStop(dltTime);
                }
            }
        }
        else
            lastChange += dltTime;
        //------------------------------------
        if (!shootgunMode)
        {
            if (IsChangeFightMode())
            {
                isSetBlock = false;
                SetFightMode(!IsFight());
                traceCombat("weapon");
            }
            if (IsFight())
            {
                if (IsDoBlock())
                {
                    // boal -->
                    if (fgtCurType == fgt_attack_fast || fgtCurType == fgt_attack_force ||
                        fgtCurType == fgt_attack_round || fgtCurType == fgt_attack_break ||
                        fgtCurType == fgt_attack_feint || fgtCurType == fgt_parry)
                    {
                        StopFightAnimation();
                    }
                    // boal <--
                    Block();
                }
                else if (IsDoParry())
                {
                    // boal -->
                    if (fgtCurType == fgt_block || fgtCurType == fgt_blockhit || fgtCurType == fgt_blockbreak ||
                        fgtCurType == fgt_hit_attack)
                    {
                        StopFightAnimation();
                    }
                    // boal <--
                    Parry();
                    traceCombat("parry");
                }
                else if (IsDoAttackForce())
                {
                    // boal -->
                    if (fgtCurType == fgt_block || fgtCurType == fgt_blockhit || fgtCurType == fgt_blockbreak)
                    {
                        StopFightAnimation();
                    }
                    // boal <--
                    Attack(FindAttackCharacter(), fgt_attack_force);
                    traceCombat("force");
                }
                else if (IsDoAttackFast())
                {
                    if (fgtCurType == fgt_block || fgtCurType == fgt_blockhit || fgtCurType == fgt_blockbreak)
                    {
                        StopFightAnimation();
                    }
                    Attack(FindAttackCharacter(), fgt_attack_fast);
                    traceCombat("fast");
                }
                else if (IsDoAttackRound())
                {
                    if (fgtCurType == fgt_block || fgtCurType == fgt_blockhit || fgtCurType == fgt_blockbreak ||
                        fgtCurType == fgt_hit_attack)
                    {
                        StopFightAnimation();
                    }
                    Attack(FindAttackCharacter(), fgt_attack_round);
                    traceCombat("round");
                }
                else if (IsDoAttackBreak())
                {
                    if (fgtCurType == fgt_block || fgtCurType == fgt_blockhit || fgtCurType == fgt_blockbreak)
                    {
                        StopFightAnimation();
                    }
                    Attack(FindAttackCharacter(), fgt_attack_break);
                    traceCombat("break");
                }
                else if (IsDoAttackFeint())
                {
                    if (fgtCurType == fgt_block || fgtCurType == fgt_blockhit || fgtCurType == fgt_blockbreak ||
                        fgtCurType == fgt_hit_attack)
                    {
                        StopFightAnimation();
                    }
                    Attack(FindAttackCharacter(), fgt_attack_feint);
                    traceCombat("feint");
                }
                else if (IsFire())
                {
                    if (fgtCurType == fgt_block || fgtCurType == fgt_blockhit || fgtCurType == fgt_blockbreak ||
                        fgtCurType == fgt_hit_attack)
                    {
                        StopFightAnimation();
                    }
                    Fire();
                    traceCombat("pistol");
                }
            }
        }
        else
        {
            tuner.alpha = 0.0f;
            tuner.camAlpha = 0.0f;
            isFight = true;
            if (kSMReload >= 1.0f)
                if (IsFire())
                    FireFromShootgun();
        }
        //------------------------------------
    }
    else
        isEnableJump = false;
    NPCharacter::Move(dltTime);
}

void Player::Update(float dltTime)
{
    auto aDialog = false;
    if (task.task == npct_none)
    {
        if (!IsFight())
        {
            CONTROL_STATE cs;
            core.Controls->GetControlState("ChrAction", cs);
            if (cs.state == CST_ACTIVATED)
            {
                aDialog = true;
#ifdef __ANDROID__
                storm::android::traceContextAction("primary-poll", "ChrAction", "player=" + std::to_string(GetId()));
#endif
            }
            if (activatedDialog)
            {
                auto *chr = FindDialogCharacter();
#ifdef __ANDROID__
                storm::android::traceContextAction("dialog-target", "ChrAction",
                    "target=" + std::to_string(chr && chr->AttributesPointer ? chr->AttributesPointer->GetAttributeAsDword("index", -1) : uint32_t(-1)));
#endif
                if (chr)
                {
                    Assert(AttributesPointer);
                    Assert(chr->AttributesPointer);
                    const int32_t first = AttributesPointer->GetAttributeAsDword("index", -1);
                    const int32_t next = chr->AttributesPointer->GetAttributeAsDword("index", -1);
                    if (first >= 0 && next >= 0)
                    {
#ifdef __ANDROID__
                        storm::android::traceContextAction("dialog-dispatch", "dlgReady",
                            "target=" + std::to_string(next) + " player=" + std::to_string(first));
#endif
                        core.Event("dlgReady", "ll", next, first);
                    }
                    else
                    {
                        core.Trace("Incorrect character index! Dialog not activated...");
                    }
                }
            }

            core.Controls->GetControlState("ChrJump", cs);
            if (cs.state == CST_ACTIVATED)
            {
                StartJump();
            }
        }
        else
        {
            if (IsFireFindTarget())
            {
                float kDist;
                auto *c = FindGunTarget(kDist, CheckShotOnlyEnemyTest());
                if (c)
                    c->Select();
            }
        }
    }

    // float kDist;
    // Character * c = FindGunTarget(kDist);
    // if(c) c->Select();
    NPCharacter::Update(dltTime);
#ifdef __ANDROID__
    TraceAndroidMovement(dltTime);
#endif
    activatedDialog = aDialog;
    core.Send_Message(baterfl, "lff", MSG_ANIMALS_BUTTERFLIES_XYZ, curPos.x, curPos.z);
    // go through characters in search of enemies to the player
    if (const auto eid = core.GetEntityId("CharactersGroups"))
    {
        auto *const location = GetLocation();
        for (size_t i = 0; i < location->supervisor.character.size(); i++)
        {
            auto *const chr = location->supervisor.character[i].c;
            if (chr != this && chr)
            {
                chr->isPlayerEnemy = (core.Send_Message(eid, "sii", "IsEnemy", GetId(), chr->GetId()) != 0);
            }
        }
    }
}

#ifdef __ANDROID__
void Player::StopAndroidMovement()
{
    StopMove();
    strafeMove = 0.0f;
    androidMotionStop = true;
    androidRunTier = false;
}

void Player::TraceAndroidMovement(float dltTime)
{
    // Diagnostic observations after collision/forces/floor update. Never poll
    // the input mailbox here: logging must not renew a lease or change its token.
    static unsigned budget = 192;
    if (!budget) return;
    if (androidTraceHasPosition) androidTraceSimulationTime += dltTime;
    const auto now = storm::android::movementTick();
    if (androidTraceHasTick && now - androidTraceTick < 250) return;
    if (!androidMovementHeld && !androidTraceTail) return;
    const auto elapsed = androidTraceHasPosition ? now - androidTraceTick : 0;
    androidTraceTick = now;
    androidTraceHasTick = true;
    char setting[PROP_VALUE_MAX] = {};
    __system_property_get("log.tag.StormInput", setting);
    if (std::string_view(setting) != "DEBUG")
    {
        androidTraceHasPosition = false;
        androidTraceSimulationTime = 0.0f;
        androidTraceTail = 0;
        return;
    }
    const float dx = androidTraceHasPosition ? curPos.x - androidTracePosition.x : 0.0f;
    const float dz = androidTraceHasPosition ? curPos.z - androidTracePosition.z : 0.0f;
    const float distance = sqrtf(dx * dx + dz * dz);
    const float velocity = androidTraceSimulationTime > 0.0f ? distance / androidTraceSimulationTime : 0.0f;
    const char *phase = androidMovementHeld ? "held" : (androidTraceTail == 2 ? "stop" : "idle");
    __android_log_print(ANDROID_LOG_DEBUG, "StormInput",
        "move-observe phase=%s player=%llu tick=%u wallMs=%u simSec=%.4f held=%d applied=%d mag=%.4f "
        "target=%.4f run=%d vector=(%.4f,%.4f) pos=(%.4f,%.4f,%.4f) delta=(%.4f,%.4f) "
        "distance=%.4f velocity=%.4f collision=%d locked=%d task=%d fight=%d yaw=%.4f cameraYaw=%.4f touchCamera=%d",
        phase, static_cast<unsigned long long>(GetId()), now, elapsed, androidTraceSimulationTime,
        androidMovementHeld, androidMotionApplied, androidTraceMagnitude, androidMotion ? androidMotionSpeed : 0.0f,
        IsRun(), androidMotion ? androidMotionDirection.x : 0.0f, androidMotion ? androidMotionDirection.z : 0.0f,
        curPos.x, curPos.y, curPos.z, dx, dz, distance, velocity, isCollision, lockMove,
        static_cast<int>(task.task), IsFight(), ay,
        locCam ? atan2f(locCam->MovementViewDirection().x, locCam->MovementViewDirection().z) : ay,
        UsesTouchCamera());
    --budget;
    androidTracePosition = curPos;
    androidTraceTick = now;
    androidTraceSimulationTime = 0.0f;
    androidTraceHasPosition = true;
    // One release observation followed by one settled observation, both under
    // the same 250 ms throttle; never flood logs while idle in the game world.
    if (androidMovementHeld) androidTraceTail = 2;
    else --androidTraceTail;
}
#endif

// Save parameters
void Player::SetSaveData(ATTRIBUTES *sdata)
{
    if (!sdata)
        return;
    sdata->SetAttributeUseDword("isFight", isFight);
}

// Restore parameters
void Player::GetSaveData(ATTRIBUTES *sdata)
{
    if (!sdata)
        return;
    if (task.task == npct_none)
    {
        SetFightMode(sdata->GetAttributeAsDword("isFight", isFight) != 0, false);
    }
}

void Player::Rotate(float dltTime)
{
    CONTROL_STATE cs;
#ifdef __ANDROID__
    // Touch camera orbit is handled by CameraFollow. Feeding its swipe back
    // into character yaw would fight joystick steering and re-orbit the camera.
    if (UsesTouchCamera() && (!locCam || locCam->IsThirdPersonFollow())) return;
#endif
    if (!isSpecialMode)
    {
        auto a = GetRotateH() * 0.01f;
        if (a > 0.3f)
            a = 0.3f;
        if (a < -0.3f)
            a = -0.3f;
        Turn(GetAY() + a);
    }
    else
    {
        if (!lockRotate)
        {
            const auto dx = GetRotateH() * 0.067f;
            core.Controls->GetControlState("ChrTurnV", cs);
            auto dz = cs.fValue * 0.067f;
            if (core.Controls->GetControlState("ChrTurnV1", cs))
                dz += cs.fValue * 0.067f;
            if (core.Controls->GetControlState("ChrTurnV2", cs))
                dz += cs.fValue * 0.067f;
            if (dx * dx + dz * dz > 0.1f)
            {
                // Rotate the vector relative to the camera
                CMatrix mtx;
                auto *const location = GetLocation();
                location->GetRS()->GetTransform(D3DTS_VIEW, mtx);
                mtx.Transposition3X3();
                mtx.Vy() = CVECTOR(0.0f, 1.0f, 0.0f);
                mtx.Vx().y = 0.0f;
                mtx.Vz().y = 0.0f;
                mtx.Vx() = !CVECTOR(mtx.Vx());
                mtx.Vz() = !CVECTOR(mtx.Vz());
                mtx.Pos() = 0.0f;
                const auto res = mtx * CVECTOR(dx, 0.0f, dz);
                Turn(res.x, res.z);
            }
        }
    }
}

bool Player::GoForward(float dltTime)
{
    CONTROL_STATE cs;
    if (!isSpecialMode)
    {
        auto res = false;
        core.Controls->GetControlState("ChrForward", cs);
        if (cs.lValue != 0)
            res = true;
        if (core.Controls->GetControlState("ChrForward1", cs) && cs.lValue != 0)
            res = true;
        if (core.Controls->GetControlState("ChrForward2", cs) && cs.lValue != 0)
            res = true;
        return res;
    }
    const auto dx = GetRotateH() * 0.067f;
    core.Controls->GetControlState("ChrTurnV", cs);
    auto dz = cs.fValue * 0.067f;
    if (core.Controls->GetControlState("ChrTurnV1", cs))
        dz += cs.fValue * 0.067f;
    if (core.Controls->GetControlState("ChrTurnV2", cs))
        dz += cs.fValue * 0.067f;
    if (IsMove())
    {
        return dx * dx + dz * dz > 0.1f;
    }
    return dx * dx + dz * dz > 0.2f;
}

bool Player::GoBack(float dltTime) const
{
    if (!isSpecialMode)
    {
        CONTROL_STATE cs;
        auto res = false;
        core.Controls->GetControlState("ChrBackward", cs);
        if (cs.lValue != 0)
            res = true;
        if (core.Controls->GetControlState("ChrBackward1", cs) && cs.lValue != 0)
            res = true;
        if (core.Controls->GetControlState("ChrBackward2", cs) && cs.lValue != 0)
            res = true;
        return res;
    }
    return false;
}

bool Player::IsRunMode(float dltTime)
{
    CONTROL_STATE cs;
    if (!isSpecialMode)
    {
        core.Controls->GetControlState("ChrRun", cs);
        return !(cs.lValue != 0);
    }
    const auto dx = GetRotateH() * 0.067f;
    core.Controls->GetControlState("ChrTurnV", cs);
    auto dz = cs.fValue * 0.067f;
    if (core.Controls->GetControlState("ChrTurnV1", cs))
        dz += cs.fValue * 0.067f;
    if (core.Controls->GetControlState("ChrTurnV2", cs))
        dz += cs.fValue * 0.067f;
    if (IsMove() && IsRun())
    {
        return dx * dx + dz * dz > 0.4f;
    }
    return dx * dx + dz * dz > 0.6f;
}

void Player::StrafeWhenMove(float dltTime)
{
    strafeMove = 0.0f;

    // ----------------------------------------------------------------------------------------
    // If you need to turn off strafe - uncomment return
    // stopstrafe
    // ----------------------------------------------------------------------------------------

    // return;

    if (!isSpecialMode)
    {
        CONTROL_STATE cs;
        if (core.Controls->GetControlState("ChrStrafeLeft", cs))
        {
            if (cs.lValue != 0)
            {
                strafeMove += -1.0f;
            }
        }
        if (core.Controls->GetControlState("ChrStrafeRight", cs))
        {
            if (cs.lValue != 0)
            {
                strafeMove += 1.0f;
            }
        }
    }
}

void Player::StrafeWhenStop(float dltTime)
{
    StrafeWhenMove(dltTime);
#ifdef __ANDROID__
    // The native poll above owns physical A/D, including configurations whose
    // stopped strafe is subsequently cleared or converted into a combat dodge.
    if (strafeMove != 0.0f) androidMotionStop = false;
#endif
    if (IsFight())
    {
        if (strafeMove > 0.5f)
        {
            StrafeRight();
        }
        else if (strafeMove < -0.5f)
        {
            StrafeLeft();
        }
        strafeMove = 0.0f;
    }
    else
    {
        strafeMove = 0.0f;
    }
}

bool Player::IsDoBlock()
{
#ifdef __ANDROID__
    // A modal/actor can skip the release frame. Reconcile both real controls
    // instead of retaining a latch after all physical/virtual input is gone.
    CONTROL_STATE primary{}, secondary{};
    core.Controls->GetControlState("ChrBlock", primary);
    core.Controls->GetControlState("ChrBlock2", secondary);
    const auto held = [](const CONTROL_STATE &state) {
        return state.state == CST_ACTIVATED || state.state == CST_ACTIVE;
    };
    isSetBlock = IsFight() && (held(primary) || held(secondary));
    return isSetBlock;
#else
    //    return true;
    CONTROL_STATE cs;
    core.Controls->GetControlState("ChrBlock", cs);
    if (cs.state == CST_ACTIVATED)
    {
        isSetBlock = true;
    }
    else if (cs.state == CST_INACTIVATED)
    {
        isSetBlock = false;
    }
    else if (cs.state == CST_INACTIVE)
    {
        core.Controls->GetControlState("ChrBlock2", cs);
        if (cs.state == CST_ACTIVATED)
        {
            isSetBlock = true;
        }
        else if (cs.state == CST_INACTIVATED)
        {
            isSetBlock = false;
        }
    }
    if (!IsFight())
    {
        isSetBlock = false;
    }
    return isSetBlock;
#endif
}

bool Player::IsDoParry()
{
    CONTROL_STATE cs;
    auto bPressed = false;
    core.Controls->GetControlState("ChrParry", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    core.Controls->GetControlState("ChrParry2", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    return bPressed;
}

bool Player::IsDoAttackForce()
{
    CONTROL_STATE cs;
    auto bPressed = false;
    core.Controls->GetControlState("ChrAttackForce", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    core.Controls->GetControlState("ChrAttackForce2", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    return bPressed;
}

bool Player::IsDoAttackFast()
{
    CONTROL_STATE cs;
    auto bPressed = false;
    core.Controls->GetControlState("ChrAttackFast", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    core.Controls->GetControlState("ChrAttackFast2", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    return bPressed;
}

bool Player::IsDoAttackRound()
{
    CONTROL_STATE cs;
    auto bPressed = false;
    core.Controls->GetControlState("ChrAttackRound", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    core.Controls->GetControlState("ChrAttackRound2", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    return bPressed;
}

bool Player::IsDoAttackBreak()
{
    CONTROL_STATE cs;
    auto bPressed = false;
    core.Controls->GetControlState("ChrAttackBreak", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    core.Controls->GetControlState("ChrAttackBreak2", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    return bPressed;
}

bool Player::IsDoAttackFeint()
{
    CONTROL_STATE cs;
    auto bPressed = false;
    core.Controls->GetControlState("ChrAttackFient", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    core.Controls->GetControlState("ChrAttackFient2", cs);
    if (cs.state == CST_ACTIVATED)
        bPressed = true;
    return bPressed;
}

bool Player::IsFire()
{
    CONTROL_STATE cs;
    core.Controls->GetControlState("ChrFire", cs);
    return (cs.state == CST_ACTIVATED);
}

bool Player::IsChangeFightMode()
{
    CONTROL_STATE cs;
    core.Controls->GetControlState("ChrFightMode", cs);
    return (cs.state == CST_ACTIVATED);
}

// Find an attacking enemy
Player *Player::FindAttackCharacter()
{
    auto *const location = GetLocation();
    // Find the surrounding characters
    auto fndCharacter = location->supervisor.FindCharacters(this, CHARACTER_ATTACK_DIST * 1.1f);
    if (fndCharacter.empty())
        return nullptr;
    // Choosing the best
    float minDst;
    int32_t task = -1;
    auto isFgt = false;
    const auto isEnemy = false;
    auto enemyCos = -1.0f;
    const auto cdx = sinf(ay);
    const auto cdz = cosf(ay);
    int32_t j = -1;
    for (size_t i = 0; i < fndCharacter.size(); i++)
    {
        // Character
        auto &fc = fndCharacter[i];
        // do not look at non-combatants
        // if(!fc.c->IsFight()) continue;
        auto *chr = static_cast<Player *>(fc.c);
        if (chr == this)
            continue;
        // skip the dead
        if (chr->liveValue < 0 || chr->deadName)
            continue;
        // filter out uninteresting
        if (isEnemy) // ~!~
        {
            if (chr->task.task != npct_fight || core.GetEntityPointer(chr->task.target) != this)
                continue;
        }
        if (isFgt)
        {
            if (!chr->isFight)
                continue;
        }
        else
        {
            if (chr->isFight)
            {
                j = -1;
                isFgt = true;
            }
        }
        /*
                if(chr->task.task == npct_fight &&
                    core.GetEntityPointer(chr->task.target) == this)
                {
                    j = -1;
                    isEnemy = true;
                }
                */
        // skip non-enemies
        if constexpr (!isEnemy) // ~!~
        {
            if (const auto eid = core.GetEntityId("CharactersGroups"))
            {
                if (!core.Send_Message(eid, "sii", "IsEnemy", GetId(), chr->GetId()))
                    continue;
            }
        }
        // This bastard climbs on us
        if (j >= 0)
        {
            auto cs = -1.0f;
            if (fc.d2 > 0.0f)
                cs = (fc.dx * cdx + fc.dz * cdz) / sqrtf(fc.d2);
            if (cs > enemyCos)
            {
                j = i;
                minDst = fc.d2;
                enemyCos = cs;
            }
        }
        else
        {
            j = i;
            minDst = fc.d2;
            if (fc.d2 > 0.0f)
                enemyCos = (fc.dx * cdx + fc.dz * cdz) / sqrtf(fc.d2);
            else
                enemyCos = -1.0f;
        }
    }
    if (j >= 0)
        return static_cast<Player *>(fndCharacter[j].c);
    return nullptr;
}

void Player::FireFromShootgun()
{
    kSMReload = 0.0f;
    if (const auto peid = core.GetEntityId("sound"))
    {
        core.Send_Message(peid, "lsllll", MSG_SOUND_PLAY, "OBJECTS\\sgboom.wav", 4, false, false, false);
    }
    // Get the position from where to shoot
    auto dx = sinf(ay);
    auto dz = cosf(ay);
    CMatrix mtx;
    auto *const location = GetLocation();
    location->GetRS()->GetTransform(D3DTS_VIEW, mtx);
    mtx.Transposition();
    const auto src = mtx.Pos() + mtx.Vz() * 0.7f;
    core.Send_Message(effects, "sffffff", "SGFireParticles", src.x, src.y - 0.35f, src.z, mtx.Vz().x, mtx.Vz().y,
                      mtx.Vz().z);

    auto *collide = static_cast<COLLIDE *>(core.GetService("COLL"));
    if (!collide)
    {
        return;
    }
    struct ChrsDmg
    {
        Character *chr;
        float dmg;
    };
    ChrsDmg chrs[16];
    int32_t numChrs = 0;

    const auto ids = core.GetEntityIds(SUN_TRACE);
    for (int32_t i = 0; i < 6; i++)
    {
        // Get the position where the buckshot will fall
        const auto r = rand() * 3.0f / RAND_MAX;
        const auto a = rand() * 6.283185307f / (RAND_MAX + 1);
        auto dst = mtx * CVECTOR(r * sinf(a), r * cosf(a), 25.0f);
        if (collide)
        {
            auto id = GetId();
            const auto dist = collide->Trace(ids, src, dst, &id, 0);
            if (dist <= 1.0f && dist > (0.2f / 25.0f))
            {
                auto dir = !(src - dst);
                dst = src + (dst - src) * dist;
                // Got somewhere
                auto *const e = core.GetEntityPointer(collide->GetObjectID());
                if (e && e != this)
                {
                    int32_t nm;
                    size_t n;
                    for (n = 0, nm = location->supervisor.character.size(); n < nm; n++)
                    {
                        auto *c = static_cast<Player *>(location->supervisor.character[n].c);
                        if (c->Model() == e)
                        {
                            core.Send_Message(effects, "sffffff", "SGBloodParticles", dst.x, dst.y, dst.z, dir.x, dir.y,
                                              dir.z);
                            c->impulse -= dir * (1.5f + rand() * (1.0f / RAND_MAX));
                            c->impulse.y += 1.5f + rand() * (1.0f / RAND_MAX);
                            int32_t j;
                            for (j = 0; j < numChrs; j++)
                            {
                                if (chrs[j].chr == c)
                                {
                                    chrs[j].dmg *= 2.0f;
                                    break;
                                }
                            }
                            if (j >= numChrs && numChrs < 16)
                            {
                                chrs[numChrs].chr = c;
                                chrs[numChrs++].dmg = 1.0f;
                            }
                            break;
                        }
                    }
                    if (n >= nm)
                        core.Send_Message(effects, "sffffff", "SGEnvParticles", dst.x, dst.y, dst.z, dir.x, dir.y,
                                          dir.z);
                }
                else
                    core.Send_Message(effects, "sffffff", "SGEnvParticles", dst.x, dst.y, dst.z, dir.x, dir.y, dir.z);
            }
        }
    }
    for (int32_t i = 0; i < numChrs; i++)
    {
        core.Event("Location_CharacterSGFire", "iif", GetId(), chrs[i].chr->GetId(), chrs[i].dmg);
    }
}

float Player::GetRotateH()
{
    CONTROL_STATE cs;
    core.Controls->GetControlState("ChrCamSpecMode", cs);
    auto dx = 0.f;
    if (cs.state != CST_ACTIVE)
    {
        core.Controls->GetControlState("ChrTurnH", cs);
        dx = cs.fValue;
    }
    if (core.Controls->GetControlState("ChrTurnH1", cs))
        dx += cs.fValue;
    if (core.Controls->GetControlState("ChrTurnHR", cs))
    {
        if (cs.state == CST_ACTIVE)
            dx += 12.0f;
    }
    if (core.Controls->GetControlState("ChrTurnHL", cs))
    {
        if (cs.state == CST_ACTIVE)
            dx -= 12.0f;
    }
    return dx;
}
