#include "ai_cannon.h"
#include "math_inlines.h"
#include "shared/sea_ai/script_defines.h"

uint64_t dwTmpRDTSC;

AICannon::AICannon() : pAHolder(nullptr), eidParent(0), dwBortIndex(0), vPos(), vDir(), fSpeedV0(0), vEnemyPos()
{
    bLoad = false;
    bEmpty = true;
    bFired = false;
    fDamaged = 0.0f;
    bRecharged = true;
    bReady2Fire = false;
    fTime2Action = 0.0f;
    bCanRecharge = true;
    fTotalTime2Action = 0.0f;
    CannonType = CANNONTYPE_NORMAL;

    fMaxFireDistance = 100.0f;
}

AICannon::~AICannon()
{
}

void AICannon::Execute(float fDeltaTime)
{
    if (fTime2Action >= 0.0f)
    {
        if (isRecharged() && (!bCanRecharge || isEmpty()))
            ;
        else
            fTime2Action -= fDeltaTime;
    }

    if (fTime2Action <= 0.0f && isRecharged())
    {
        bRecharged = false;
        if (!isEmpty())
            bReady2Fire = true;
        return;
    }

    if (fTime2Action <= 0.0f && isFired() && !isDamaged() && !isEmpty())
    {
        RDTSC_B(dwTmpRDTSC);
        RealFire();
        RDTSC_E(dwTmpRDTSC);
    }
}

float AICannon::CalcHeightFireAngle(float _fSpeedV0, const glm::vec3 &vOur, const glm::vec3 &vEnemy) const
{
    auto vTemp = vOur - vEnemy;
    vTemp.y = 0.0f;
    const double fDistance = sqrtf(storm::math::lengthSquared(vTemp));
    const auto g = static_cast<double>(AIHelper::fGravity);
    const double fHeight = (vEnemy.y - vOur.y);
    const double a = SQR(g) / 4.0f;
    const double b = (fHeight * g - SQR(_fSpeedV0));
    const double c = SQR(fHeight) + SQR(fDistance);
    const double d = b * b - 4 * a * c;
    if (d < 0)
        return 0.0f;

    const double x1 = (-b + sqrt(d)) / (2.0 * a);
    const double x2 = (-b - sqrt(d)) / (2.0 * a);

    const double t1 = sqrt(x1);
    const double t2 = sqrt(x2);

    const auto T = (CannonType == CANNONTYPE_MORTAR) ? t1 : t2;

    const auto fTemp = fDistance / (_fSpeedV0 * T);
    const auto fAlpha = acos(Clamp(fTemp));

    const auto sy0 = _fSpeedV0 * T * sin(fAlpha) - g * SQR(T) / 2.0;
    const auto sy1 = _fSpeedV0 * T * sin(-fAlpha) - g * SQR(T) / 2.0;
    return (fabs(sy0 - fHeight) < fabs(sy1 - fHeight)) ? static_cast<float>(fAlpha) : static_cast<float>(-fAlpha);
}

VAI_OBJBASE *AICannon::GetAIObjPointer() const
{
    return static_cast<VAI_OBJBASE *>(core.GetEntityPointer(GetParentEID()));
}

float AICannon::GetDirY() const
{
    glm::mat4 mRot;
    auto *pAIObj = GetAIObjPointer();

    storm::math::copy3x3(mRot, (*pAIObj->GetMatrix()));

    const auto vDirTemp = storm::math::transformPoint(mRot, vDir);
    return NormalizeAngle(atan2f(vDirTemp.x, vDirTemp.z));
}

glm::vec3 AICannon::GetDir() const
{
    glm::mat4 mRot;
    auto *pAIObj = GetAIObjPointer();
    storm::math::copy3x3(mRot, (*pAIObj->GetMatrix()));
    return storm::math::transformPoint(mRot, vDir);
}

glm::vec3 AICannon::GetPos() const
{
    return storm::math::transformPoint(*GetAIObjPointer()->GetMatrix(), vPos);
}

void AICannon::RealFire()
{
    glm::mat4 mRot;
    // calculate real world cannon position and direction,
    // calculate fire height and azimuth angle, and call script
    auto *pAIObj = GetAIObjPointer();
    const auto vPosTemp = storm::math::transformPoint(*pAIObj->GetMatrix(), vPos);
    storm::math::copy3x3(mRot, (*pAIObj->GetMatrix()));

    const auto vEnemyDir = storm::math::normalizeOrZero(vEnemyPos - vPosTemp);
    const auto fFireDirection = NormalizeAngle(atan2f(vEnemyDir.x, vEnemyDir.z));
    const auto fFireHeightAngle = CalcHeightFireAngle(fSpeedV0, vPosTemp, vEnemyPos);

    const auto vDirTemp = storm::math::transformPoint(mRot, vDir);
    const auto fDirY = NormalizeAngle(atan2f(vDirTemp.x, vDirTemp.z));

    // raw angle
    const glm::vec3 vFlatEnemyDir = storm::math::normalizeOrZero(glm::vec3(vEnemyPos.x, vPosTemp.y, vEnemyPos.z) - vPosTemp);
    float fAngle;
    const float dp = glm::dot(vFlatEnemyDir, vEnemyDir);
    if (dp >= 1.0f)
    {
        fAngle = 0.0f;
    }
    else if (dp <= -1.0f)
    {
        fAngle = PI;
    }
    else
    {
        fAngle = acosf(dp);
    }
    if (vEnemyPos.y < vPosTemp.y)
    {
        fAngle = -fAngle;
    }

    core.Event(CANNON_FIRE, "afffffffff", pAHolder->GetACharacter(), vPosTemp.x, vPosTemp.y, vPosTemp.z, fSpeedV0,
               fFireDirection, fFireHeightAngle, fDirY, fMaxFireDistance, fAngle);

    pAIObj->Fire(storm::math::transformPoint(*pAIObj->GetMatrix(), vPos + vDir));

    bFired = false;
    bEmpty = true;
    Recharge();
}

bool AICannon::Fire(float _fSpeedV0, const glm::vec3 &_vFirePos)
{
    if (isFired() || isRecharged() || isDamaged() || isEmpty())
        return false;

    auto *pAIObj = GetAIObjPointer();
    const auto vPosTemp = storm::math::transformPoint(*pAIObj->GetMatrix(), vPos);
    glm::mat4 mRot;
    storm::math::copy3x3(mRot, (*pAIObj->GetMatrix()));

    const auto vFireDir = storm::math::normalizeOrZero(_vFirePos - vPosTemp);
    const auto vDirTemp = storm::math::transformPoint(mRot, vDir);
    const auto fDot = glm::dot(vFireDir, vDirTemp);
    if (fDot < 0.3f)
        return false;

    bFired = true;
    bReady2Fire = false;

    vEnemyPos = _vFirePos;
    fSpeedV0 = _fSpeedV0;

    // calculate in script timeout before real fire: bort, random delay of a real shot, cannon index
    auto *pVData = core.Event(CANNON_GET_FIRE_TIME, "asll", pAHolder->GetACharacter(), sBortName.c_str(), 1,
                              static_cast<int32_t>(dwBortIndex));
    Assert(pVData);
    fTotalTime2Action = fTime2Action = pVData->GetFloat();
    return true;
}

void AICannon::Unload()
{
    // call script
    if (!isDamaged() && !isEmpty())
    {
        core.Event(CANNON_UNLOAD, "a", pAHolder->GetACharacter());
    }
    bReady2Fire = false;
    bRecharged = false;
    bEmpty = true;
    bFired = false;
}

void AICannon::Load()
{
    auto *pVData = core.Event(CANNON_LOAD, "a", pAHolder->GetACharacter());
    Assert(pVData);
    bEmpty = pVData->GetInt() == 0;
}

void AICannon::Recharge()
{
    if (isFired() || isDamaged())
        return;
    if (!isEmpty())
        Unload();
    if (bCanRecharge)
        Load();
    bReady2Fire = false;
    bRecharged = true;

    // calculate in script recharge time, and possibility of recharge
    auto *pVData = core.Event(CANNON_GET_RECHARGE_TIME, "as", pAHolder->GetACharacter(), sBortName.c_str());
    Assert(pVData);
    fTotalTime2Action = fTime2Action = pVData->GetFloat();
}

void AICannon::QuickRecharge(bool bHaveEnoughBalls)
{
    Recharge();
    fTime2Action = (bHaveEnoughBalls) ? 0.0f : fTotalTime2Action;
}

float AICannon::GetRechargePercent()
{
    return (isRecharged()) ? 1.0f - fTime2Action / fTotalTime2Action : 1.0f;
}

float AICannon::CalcMaxFireDistance(float fFireHeight, float fSpeedV0, float fAngle)
{
    const auto a = -AIHelper::fGravity / 2.0f;
    const auto b = fSpeedV0 * sinf(fAngle);
    const auto c = fFireHeight;
    const auto d = b * b - 4.0f * a * c;
    const auto t1 = (-b - sqrtf(d)) / (2.0f * a);
    // float t2 = (-b + sqrtf(d)) / (2.0f * a);

    const auto fDistance = fSpeedV0 * t1 * cosf(fAngle);
    // float s2 = fSpeedV0 * t2 * cosf(fAngle);

    return fDistance;
}

void AICannon::Init(AIAttributesHolder *_pAHolder, const entid_t eid, GEOS::LABEL &lbl, std::string bortName,
                    const uint32_t bortIndex)
{
    pAHolder = _pAHolder;
    eidParent = eid;
    sBortName = std::move(bortName);
    dwBortIndex = bortIndex;

    glm::mat4 m;
    memcpy(&m[0][0], &lbl.m[0][0], sizeof(m));

    vPos = storm::math::pos(m);
    vDir = glm::vec3(storm::math::vz(m).x, 0.0f, storm::math::vz(m).z);
    // vDir = atan2f(m.Vz().x,m.Vz().z);
}

void AICannon::Save(CSaveLoad *pSL) const
{
    pSL->SaveVector(vPos);
    pSL->SaveVector(vDir);
    pSL->SaveFloat(fTime2Action);
    pSL->SaveFloat(fTotalTime2Action);
    pSL->SaveFloat(fSpeedV0);
    pSL->SaveVector(vEnemyPos);
    pSL->SaveDword(bEmpty);
    pSL->SaveFloat(fDamaged);
    pSL->SaveDword(bFired);
    pSL->SaveDword(bReady2Fire);
    pSL->SaveDword(bRecharged);
    pSL->SaveDword(bLoad);
    pSL->SaveDword(bCanRecharge);
}

void AICannon::Load(CSaveLoad *pSL, AIAttributesHolder *_pAHolder, const entid_t eid, std::string bortName,
                    const uint32_t bortIndex)
{
    pAHolder = _pAHolder;
    eidParent = eid;
    sBortName = std::move(bortName);
    dwBortIndex = bortIndex;

    vPos = pSL->LoadVector();
    vDir = pSL->LoadVector();
    fTime2Action = pSL->LoadFloat();
    fTotalTime2Action = pSL->LoadFloat();
    fSpeedV0 = pSL->LoadFloat();
    vEnemyPos = pSL->LoadVector();
    bEmpty = pSL->LoadDword() != 0;
    fDamaged = pSL->LoadFloat();
    bFired = pSL->LoadDword() != 0;
    bReady2Fire = pSL->LoadDword() != 0;
    bRecharged = pSL->LoadDword() != 0;
    bLoad = pSL->LoadDword() != 0;
    bCanRecharge = pSL->LoadDword() != 0;
}
