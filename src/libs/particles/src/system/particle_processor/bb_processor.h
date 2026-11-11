//****************************************************************
//*
//*  Author : Sergey Makeev aka Joker, 2003
//*
//*
//****************************************************************

#pragma once

#include "dx9render.h"
#include "../../gmx_qsort.h"

#include "../../i_common/particle.h"
#include "../data_source/field_list.h"

class ParticleSystem;

class BillBoardProcessor
{
    static storm::render::VertexDeclarationId vertexDecl_;
    void CreateVertexDeclaration() const;

    struct RECT_VERTEX
    {
        glm::vec3 vRelativePos;
        uint32_t dwColor;
        float tu1, tv1;
        float tu2, tv2;
        float angle;
        float BlendK;
        glm::vec3 vParticlePos;
        float AddPowerK;
    };

    VDX9RENDER *pRS;
    // Buffers for rendering billboards
    int32_t pVBuffer;
    int32_t pIBuffer;

    struct MemArrayItem
    {
        BB_ParticleData pData;
        bool Free;

        MemArrayItem()
            : pData()
        {
            Free = true;
        }
    };

    MemArrayItem *pMemArray;

    std::vector<BB_ParticleData *> Particles;

    GMXQSort<BB_ParticleData *> ParticleSorter;

    // Counts distance to billboards
    uint32_t CalcDistanceToCamera();

    // Compare function when sorting
    static BOOL CompareFunction(BB_ParticleData *e1, BB_ParticleData *e2);

    BB_ParticleData *AllocParticle() const;
    void FreeParticle(BB_ParticleData *pItem) const;

  public:
    BillBoardProcessor();
    ~BillBoardProcessor();

    void AddParticle(ParticleSystem *pSystem, const glm::vec3 &velocity_dir, const glm::vec3 &pos, const glm::mat4 &matWorld,
                     float EmitterTime, float EmitterLifeTime, FieldList *pFields, uint32_t *pActiveCount,
                     uint32_t dwGUID);

    void Process(float DeltaTime);
    void Draw();

    uint32_t GetCount() const;

    void DeleteWithGUID(uint32_t dwGUID, uint32_t GUIDRange = GUIDSTEP);

    void Clear();
};
