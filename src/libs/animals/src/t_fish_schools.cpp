#include "t_fish_schools.h"

#include <algorithm>
#include "entity.h"
#include "core.h"
#include "math_inlines.h"
#include "rands.h"
#include "shared/messages.h"
#include "v_file_service.h"

//--------------------------------------------------------------------
TFishSchools::TFishSchools() : enabled(false)
{
    memset(fishSchools, 0, sizeof(fishSchools));
}

//--------------------------------------------------------------------
TFishSchools::~TFishSchools()
{
    if (!created)
        return;
    for (auto i = 0; i < fishSchoolsCount; i++)
        delete fishSchools[i];
    core.EraseEntity(fishSchoolModel);
}

//--------------------------------------------------------------------
void TFishSchools::LoadSettings()
{
    auto ini = fio->OpenIniFile(ANIMALS_INI_FILENAME);
    if (!ini)
        return;

    fishSchoolsCount = std::clamp(static_cast<int32_t>(ini->GetInt(ANIMALS_FISHSCHOOLS_SECTION, "count", FISHSCHOOL_COUNT)), 0,
                                  static_cast<int32_t>(FISHSCHOOL_COUNT));
    maxDistance = ini->GetFloat(ANIMALS_FISHSCHOOLS_SECTION, "distance", FISHSCHOOL_DISTANCE);
}

//--------------------------------------------------------------------
void TFishSchools::Init()
{
    LoadSettings();

    renderService = static_cast<VRenderer *>(core.GetService("dx9render"));
    if (!renderService)
        throw std::runtime_error("!FishSchools: No service 'dx9render'");

    enabled = false;
    CreateSchools();
}
//--------------------------------------------------------------------
bool TFishSchools::CreateSchools()
{
    if (created)
        return true;
    sea = static_cast<SEA_BASE *>(core.GetEntityPointer(core.GetEntityId("sea")));
    if (!sea)
        return false;
    for (auto i = 0; i < fishSchoolsCount; i++)
    {
        fishSchools[i] = new TFishSchool();
        fishSchools[i]->Initialize(glm::vec3(0.0f, 0.0f, 0.0f), maxDistance);
        fishSchools[i]->depth = 0.05f;
        fishSchools[i]->time = 0;
        fishSchools[i]->timeDivider = randUpper(100.0f);
        fishSchools[i]->amplitude = rand(0.01f);
        AddDeflector(fishSchools[i]);
    }

    AddAttractor(&cameraObject);

    fishSchoolModel = core.CreateEntity("MODELR");
    core.Send_Message(fishSchoolModel, "ls", MSG_MODEL_LOAD_GEO, ANIMALS_FISHSCHOOL_FILENAME);
    created = true;
    return true;
}

//--------------------------------------------------------------------
uint64_t TFishSchools::ProcessMessage(int32_t _code, MESSAGE &message)
{
    const uint32_t outValue = 0;

    switch (_code)
    {
    case MSG_ANIMALS_FISHSCHOOLS_SHOW:
        // Shown before any sea existed (e.g. a boarding started from land): the schools are created
        // now, or stay hidden until a sea exists.
        enabled = CreateSchools();
        break;

    case MSG_ANIMALS_FISHSCHOOLS_HIDE:
        enabled = false;
        break;
    }

    return outValue;
}

//--------------------------------------------------------------------
void TFishSchools::Execute(uint32_t _dTime)
{
    if (!enabled)
        return;

    glm::vec3 pos, ang, fishPos;
    float persp;
    renderService->GetCamera(pos, ang, persp);

    cameraObject.SetXYZ(pos);
    const auto speedK = static_cast<float>(_dTime) / 1000.0f;
    for (auto i = 0; i < fishSchoolsCount; i++)
    {
        // respawn near camera if needed
        fishPos = fishSchools[i]->GetXYZ();
        if ((fabs(fishPos.x - pos.x) + fabs(fishPos.x - pos.x)) > RESPAWN_DISTANCE)
            fishSchools[i]->Initialize(pos, maxDistance);

        // recalculate all fishes
        fishSchools[i]->Calculate(attractors, MAX_DYNAMIC_OBJECTS, deflectors, MAX_DYNAMIC_OBJECTS, speedK);
        fishSchools[i]->time += _dTime;
    }
}

//--------------------------------------------------------------------
void TFishSchools::Realize(uint32_t _dTime)
{
    if (!enabled)
        return;
    /*
      glm::vec3 cameraPos, cameraAng;
      float   cameraPersp;
      renderService->GetCamera(cameraPos, cameraAng, cameraPersp);
    */
    sea = static_cast<SEA_BASE *>(core.GetEntityPointer(core.GetEntityId("sea")));
    if (!sea)
    {
        enabled = false;
        return;
    }

    auto *fishSchool = static_cast<MODEL *>(core.GetEntityPointer(fishSchoolModel));
    if (!fishSchool)
        return;

    for (auto i = 0; i < fishSchoolsCount; i++)
    {
        static const auto OSC_AMPLITUDE = 0.1f;
        const auto fishSchoolAngle = fishSchools[i]->GetAngle();
        const auto fishSchoolPos = fishSchools[i]->GetXYZ();
        const auto fishSchoolTime = fishSchools[i]->time / fishSchools[i]->timeDivider;
        glm::vec3 ang(0.0f,
                    PId2 - fishSchoolAngle - (fishSchools[i]->amplitude * (PId2 / 10.0f) / 0.1f) * cosf(fishSchoolTime),
                    0.0f);
        glm::vec3 pos;
        pos.x = fishSchoolPos.x + fishSchools[i]->amplitude * cosf(fishSchoolAngle + PId2) * sinf(fishSchoolTime);
        pos.z = fishSchoolPos.z + fishSchools[i]->amplitude * sinf(fishSchoolAngle + PId2) * sinf(fishSchoolTime);
        pos.y = sea->WaveXZ(pos.x, pos.z) - fishSchools[i]->depth;
        fishSchool->mtx = storm::math::eulerMatrix(ang, pos);
        fishSchool->ProcessStage(Entity::Stage::realize, _dTime);
    }
}

//--------------------------------------------------------------------
