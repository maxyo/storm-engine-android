//****************************************************************
//*
//*  Author : Sergey Makeev aka Joker, 2003
//*
//*
//****************************************************************

#pragma once

#include "dx9render.h"

#include "../../i_common/particle.h"
#include "../data_source/field_list.h"
#include "name_parser.h"

class ParticleSystem;
class ParticleManager;

class ModelProcessor
{
    VRenderer *pRS;
    ParticleManager *pMasterManager;
    GeomNameParser Parser;

    struct MemArrayItem
    {
        MDL_ParticleData pData;
        bool Free;

        MemArrayItem()
            : pData()
        {
            Free = true;
        }
    };

    MemArrayItem *pMemArray;

    std::vector<MDL_ParticleData *> Particles;

    MDL_ParticleData *AllocParticle() const;
    void FreeParticle(MDL_ParticleData *pItem) const;

  public:
    ModelProcessor(ParticleManager *pManager);
    ~ModelProcessor();

    void AddParticle(ParticleSystem *pSystem, const glm::vec3 &velocity_dir, const glm::vec3 &pos, const glm::mat4 &matWorld,
                     float EmitterTime, float EmitterLifeTime, FieldList *pFields, uint32_t *pActiveCount,
                     uint32_t dwGUID);

    void Process(float DeltaTime);
    void Draw();

    uint32_t GetCount() const;

    void DeleteWithGUID(uint32_t dwGUID, uint32_t GUIDRange = GUIDSTEP);

    void Clear();
};
