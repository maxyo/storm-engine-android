#pragma once

#include "animals_defines.h"
#include "storm_glm.h"
#include "t_dynamic_object.h"
#include "t_dynamic_system.h"
#include "dx9render.h"
#include "geometry.h"
#include "sea_base.h"
#include "ship_base.h"

///////////////////////////////////////////////////////////////////
// DEFINES & TYPES
///////////////////////////////////////////////////////////////////
class TFishSchool : public TDynamicObject
{
  public:
    float depth;
    uint32_t time;
    float timeDivider;
    float amplitude;
};

///////////////////////////////////////////////////////////////////
// CLASS DEFINITION
///////////////////////////////////////////////////////////////////
class TFishSchools : public TDynamicSystem
{
    friend class TFishSchool;

  public:
    TFishSchools();
    ~TFishSchools() override;

    uint64_t ProcessMessage(int32_t _code, MESSAGE &message);
    void Init();
    void Realize(uint32_t dTime);
    void Execute(uint32_t dTime);

  private:
    void LoadSettings();
    // Creates the schools and their model once a sea exists; false while there is no sea.
    bool CreateSchools();

    VDX9RENDER *renderService;
    entid_t fishSchoolModel;
    TFishSchool *fishSchools[FISHSCHOOL_COUNT];
    int32_t shipsCount;
    int32_t fishSchoolsCount = 0; // schools to create (settings), at most FISHSCHOOL_COUNT
    bool created = false;         // fishSchools[0..fishSchoolsCount) and fishSchoolModel exist
    TDynamicObject cameraObject;

    float maxDistance;

    bool enabled; // only ever true while `created`
    SEA_BASE *sea;
};
