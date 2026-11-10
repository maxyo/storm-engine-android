#pragma once

#include "storm_glm.h"
#include "storm_glm.h"
#include "model.h"
#include "cannon_trace.h"

class CSaveLoad;

class VAI_OBJBASE : public CANNON_TRACE_BASE
{
  private:
    ATTRIBUTES *pACharacter;

  public:
    ~VAI_OBJBASE() override{};

    virtual void SetACharacter(ATTRIBUTES *pAP)
    {
        pACharacter = pAP;
    };

    virtual ATTRIBUTES *GetACharacter()
    {
        return pACharacter;
    };

    virtual bool Mount(ATTRIBUTES *) = 0;

    virtual void SetPos(const glm::vec3 &vNewPos) = 0;

    virtual glm::vec3 GetPos() const = 0;
    virtual glm::vec3 GetAng() const = 0;
    virtual glm::mat4 *GetMatrix() = 0;
    virtual glm::vec3 GetBoxsize() const = 0;
    virtual glm::vec3 GetRealBoxsize() const
    {
        return GetBoxsize();
    }

    virtual MODEL *GetModel() const = 0;
    virtual entid_t GetModelEID() const = 0;

    virtual void Save(CSaveLoad *pSL) = 0;
    virtual void Load(CSaveLoad *pSL) = 0;

    virtual void Fire(const glm::vec3 &vPos) = 0;
};
