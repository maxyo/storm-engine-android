#pragma once

#include "dx9render.h"
#include "model.h"
#include "sea_base.h"
#include "ship_base.h"
#include <v_particle_system.h>

#include <string>

class FirePlace
{
  public:
    FirePlace();
    ~FirePlace();

    glm::vec3 GetPos()
    {
        Assert(GetShip());
        return storm::math::transformPoint(*GetShip()->GetMatrix(), vOrigPos);
    };

    void SetPos(glm::vec3 &vPos)
    {
        vOrigPos = vPos;
    };

    float GetDistance(glm::vec3 &vOtherPos)
    {
        return sqrtf(storm::math::lengthSquared(GetPos() - vOtherPos));
    };

    void SetShip(SHIP_BASE *_pShip)
    {
        pShip = _pShip;
    }

    SHIP_BASE *GetShip()
    {
        return pShip;
    }

    void Execute(float fDeltaTime);
    void Run(const char *pParticleSmokeName, const char *pParticleFireName, int32_t _iBallCharacterIndex,
             const char *pSoundName, float _fRunTime);
    void Stop();
    void StopSound(int32_t _iSoundID);

    bool isActive()
    {
        return bActive;
    }

    float GetRunTime()
    {
        return fRunTime;
    }

    int32_t GetBallCharacterIndex()
    {
        return iBallCharacterIndex;
    }

    void Init(SEA_BASE *_pSea, SHIP_BASE *_pShip, GEOS::LABEL &label);

    void Save(CSaveLoad *pSL);
    void Load(CSaveLoad *pSL);

    static entid_t eidSound;

  private:
    bool CreateParticle(const char *pParticleSmokeName, const char *pParticleFireName);
    void DeleteParticle();

    void SetActive(bool _bActive)
    {
        bActive = _bActive;
    }

    VPARTICLE_SYSTEM *pParticleSmoke, *pParticleFire;
    SHIP_BASE *pShip;
    SEA_BASE *pSea;
    int32_t iSoundID;
    int32_t iBallCharacterIndex;
    glm::vec3 vOrigPos;
    bool bActive;
    float fRunTime;
    std::string sParticleSmokeName, sParticleFireName, sSoundName;
};
