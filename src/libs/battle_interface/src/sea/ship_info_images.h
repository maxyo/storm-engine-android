#pragma once

#include "ships_list.h"

struct SII_VERTEX
{
    glm::vec3 pos;
    float tu, tv;
};

class ShipInfoImages
{
  public:
    ShipInfoImages(VDX9RENDER *rs, ATTRIBUTES *pAttr);
    ~ShipInfoImages();

    void Draw();

    void SetVisible(bool bVisible)
    {
        m_bVisible = bVisible;
    }

  protected:
    void Release();
    void Init(ATTRIBUTES *pAttr);
    void CheckAndRecreateBuffers(int32_t nShipQ);
    void UpdateShipList();
    void UpdateShipData(int32_t nShipNum, SHIP_DESCRIBE_LIST::SHIP_DESCR *pSD);
    bool IsEnableShowShipInfo(SHIP_DESCRIBE_LIST::SHIP_DESCR *pSD) const;

    const FRECT &GetUVForRelation(int32_t nRelation) const;
    float GetProgressHull(SHIP_DESCRIBE_LIST::SHIP_DESCR *pSD);
    float GetProgressSail(SHIP_DESCRIBE_LIST::SHIP_DESCR *pSD);
    float GetProgressCrew(SHIP_DESCRIBE_LIST::SHIP_DESCR *pSD);

    void CalculateDirectingVectors(const glm::vec3 &pos);
    void WriteSquareVertex(SII_VERTEX *pV, const glm::vec3 &center, const glm::vec3 &offset, const FPOINT &size,
                           const FRECT &uv, float fProgress) const;

  protected:
    VDX9RENDER *pRS;
    bool m_bVisible;

    int32_t m_idRelationTexture;
    int32_t m_vbRelation;
    int32_t m_ibRelation;

    int32_t m_idProgressTexture;
    int32_t m_vbBackProgress;
    int32_t m_ibBackProgress;
    int32_t m_vbProgress;
    int32_t m_ibProgress;

    int32_t m_nShipQ;
    int32_t m_nCurMaxQuantity;

    glm::vec3 m_vRelationOffset;
    FPOINT m_fpRelationSize;
    FRECT m_uvRelation[3];

    glm::vec3 m_vProgressBackOffset;
    FPOINT m_fpProgressBackSize;
    FRECT m_uvProgressBack;

    glm::vec3 m_vHullOffset;
    FRECT m_uvHull;
    glm::vec3 m_vSailOffset;
    FRECT m_uvSail;
    glm::vec3 m_vCrewOffset;
    FRECT m_uvCrew;
    FPOINT m_fpProgressSize;

    SII_VERTEX *m_pVBuffProgress;
    SII_VERTEX *m_pVBuffBackProgress;
    SII_VERTEX *m_pVBuffRelation;
    glm::vec3 m_vUpDir;
    glm::vec3 m_vRightDir;
    float m_fImgScale;
};
