#include "p_system.h"
#include "particles.h"
#include "../particles.h"

PARTICLE_SYSTEM::PARTICLE_SYSTEM(IParticleSystem *_pSystem)
{
    pManager = nullptr;
    pSystem = _pSystem;
}

PARTICLE_SYSTEM::~PARTICLE_SYSTEM()
{
    pManager->DeleteResource(this);

    if (pSystem)
        pSystem->Release();
    pSystem = nullptr;
}

void PARTICLE_SYSTEM::Stop()
{
}

void PARTICLE_SYSTEM::SetEmitter(glm::vec3 p, glm::vec3 a)
{
    // core.Trace("K2 Particles Wrapper: SetEmitter (%3.2f, %3.2f, %3.2f, %3.2f, %3.2f, %3.2f)", p.x, p.y, p.z, a.x,
    // a.y, a.z);
    pSystem->SetTransform(storm::math::eulerMatrix(a, p));
}

void PARTICLE_SYSTEM::LinkToObject(entid_t id, glm::vec3 _LinkPos)
{
}

void PARTICLE_SYSTEM::SetDelay(int32_t _delay)
{
}

void PARTICLE_SYSTEM::SetLifeTime(uint32_t time)
{
}

void PARTICLE_SYSTEM::StopEmitter()
{
}

void PARTICLE_SYSTEM::Pause(bool _bPause)
{
    pSystem->PauseEmission(_bPause);
}

void PARTICLE_SYSTEM::SetManager(PARTICLES *_pManager)
{
    pManager = _pManager;
}

IParticleSystem *PARTICLE_SYSTEM::GetSystem() const
{
    return pSystem;
}
