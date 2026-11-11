#pragma once

#include "character.h"
#include "island_base.h"
#include "save_load.h"
#include "collide.h"
#include "dx9render.h"
#include "script_relations.hpp"
#include <optional>
#include <vector>

class AIAttributesHolder
{
  protected:
    ATTRIBUTES *pACharacter;

  public:
    virtual void SetACharacter(ATTRIBUTES *pAP)
    {
        pACharacter = pAP;
    };

    virtual ATTRIBUTES *GetACharacter() const
    {
        return pACharacter;
    };
};

class VAI_INNEROBJ;

class AIHelper
{
  public:
    AIHelper();
    ~AIHelper();

    static ATTRIBUTES *pASeaCameras;
    static ISLAND_BASE *pIsland;
    static VRenderer *pRS;
    static COLLIDE *pCollide;

    static float fGravity;

    bool SetDevice();
    bool Init() const;
    bool Uninit();
    void AddCharacter(ATTRIBUTES *pACharacter, ATTRIBUTES *pAMainCharacter);
    void CalculateRelations();

    bool isFriend(ATTRIBUTES *pA1, ATTRIBUTES *pA2) const;
    bool isEnemy(ATTRIBUTES *pA1, ATTRIBUTES *pA2) const;
    bool isNeutral(ATTRIBUTES *pA1, ATTRIBUTES *pA2) const;

    ATTRIBUTES *GetMainCharacter(ATTRIBUTES *pACharacter);

    static VAI_INNEROBJ *FindAIInnerObj(ATTRIBUTES *pACharacter);

    uint32_t GetRelation(ATTRIBUTES *pA1, ATTRIBUTES *pA2) const;
    uint32_t GetRelationSafe(ATTRIBUTES *pA1, ATTRIBUTES *pA2) const;
    // GetRelationSafe in the scripts' numbering, for relations handed to the scripts.
    int32_t GetScriptRelationSafe(ATTRIBUTES *pA1, ATTRIBUTES *pA2);

    static void Print(float x, float y, float fScale, const char *pFormat, ...);
    static void Print3D(glm::vec3 vPos, float dy, float fScale, const char *pFormat, ...);

    void Save(CSaveLoad *pSL);
    void Load(CSaveLoad *pSL);

  private:
    uint32_t *pRelations, dwRelationSize;
    std::optional<storm::sea_ai::ScriptRelations> scriptRelations_; // read from the scripts on first use

    const storm::sea_ai::ScriptRelations &ScriptRelationNumbers();
    std::vector<ATTRIBUTES *> aCharacters, aMainCharacters;

    uint32_t *GetRelation(uint32_t x, uint32_t y) const;
    uint32_t FindIndex(ATTRIBUTES *pACharacter) const;
};

extern AIHelper Helper;
