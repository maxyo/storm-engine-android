// sailone.h: interface for the SAILONE class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "storm_glm.h"
#include "dx9render.h"
#include "sail_base.h"

extern float g_fSailHoleDepend;
extern float GetSailSpeed(int holeQ, int holeMax, float maxSpeed, float fSailHoleDepend = g_fSailHoleDepend);

// used other classes
class NODE;

#define SAIL_COL_MAX 13
#define SAIL_ROW_MAX 17
#define SAILVERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX3 | D3DFVF_TEXTUREFORMAT2)

struct SAILVERTEX
{
    glm::vec3 pos, norm;
    float tu1, tv1;
    float tu2, tv2;
    float tu3, tv3;
};

// parameters for sail triangulation
#define SAIL_FAR_IDX 7

const struct
{
    uint16_t row, col;
    float len;
} farGrid[SAIL_FAR_IDX] = {
    {17, 13, 10.f},                                               // 12x16
    {13, 13, 20.f}, {13, 10, 30.f}, {13, 7, 50.f}, {9, 7, 200.f}, // 6x8
    {5, 7, 250.f},                                                // 6x4
    {5, 4, 600.f},                                                // 3x4
    //    {3, 4, 10000.f} // 3x2
};

struct WIND
{
    float base; // amplitude of wind fluctuations from 0 to 1.0
    struct
    {
        float x, y, z;
    } ang;
};

struct SPHERE
{
    glm::vec3 c;
    glm::vec3 rc; // center in world coordinates
    float r;
};

#define TEXTUREMAXCOUNTER 3

// texture table
struct TEXTURESLIST
{
    int texCount;
    int uses[TEXTUREMAXCOUNTER];
    int32_t ntex[TEXTUREMAXCOUNTER];
    int32_t htex[TEXTUREMAXCOUNTER];
    char *nname[TEXTUREMAXCOUNTER];
    char *hname[TEXTUREMAXCOUNTER];
};

struct SAILGEOMETRY
{
    union {
        struct
        {
            float x, y, z;
        } v[9];

        struct
        {
            glm::vec3 dVv, ddVv, dddVv; // Vectors for vertical flex of sail
            glm::vec3 dVh, ddVh;        // Vectors for horizontal flaex of sail
            glm::vec3 normL, dnormL;    // Vectors for leftside sail normals
            glm::vec3 normR, dnormR;    // Vectors for rightside sail normals
            glm::vec3 dopV;             // Vector for rope tie;
        } cv;
    };

    SAILGEOMETRY() : cv{}
    {
    }
};

#define SAILROLLSTEPS 40 // steps counter of sail rolling

struct SAILROLLING
{
    SAILGEOMETRY oldgeo; // final value of geometry parameters
    float delta;
    bool rollup; // the sail folds (false-unfolds)
};

struct ROTATEROPEDSAIL
{
    int ropenum;
    float r1, r2; // radius to the end of the rope and the beginning of another point
    glm::vec3 b;    // x, y, z coordinate for calculating rope tension in Z
    int tiePoint;
};

struct SAILTIEROPE
{
    bool pnttie[4];
    glm::vec3 *pPos[4];
    ROTATEROPEDSAIL *rrs[2];
};

enum SAIL_TYPE
{
    SAIL_TREANGLE,
    SAIL_SQUARE,
    SAIL_TRAPECIDAL,
    SAIL_GERBED,
    SAIL_ROLLING,
    SAIL_SPECIAL
};

struct SAILSTATE
{
    uint32_t holeCount;
    bool hole[12];
    uint16_t shi, sni, Nh, Nn; // Start index and number of indexes in punched / normal sail reflection

    SAIL_TYPE eSailType;

    // sail anchor points
    glm::vec3 hardPoints[4];

    bool bYesLimitPoint;
    glm::vec3 LimitPoint;

    // texture number
    uint16_t texNum;

    // ---- Parameters that make up the sail shape ----
    float fDeepZ;  // absolute vertical sail bulge
    float fDeepH;  // absolute horizontal sail bulge
    float fDeepVz; // deflection of the lower edge of the sail along the normal
    float fDeepVh; // deflection of the lower edge of the sail in height

    uint32_t sVert, nVert;
    uint32_t sIndx, nIndx;
    uint32_t sholeIndx, nnormIndx, nholeIndx;

    // sail bounding sphere
    SPHERE boundSphere;
    glm::vec3 boxCenter;
    glm::vec3 boxSize;

    bool rollingSail, turningSail;

    float maxAngle, curAngle;
};

struct SAILTIME
{
    uint64_t idx, vert, noholedraw, holedraw;
};

class SAIL;
struct SAILLIST;

class SAILONE : public SAILONE_BASE
{
    friend SAIL;

  public:
    VRenderer *RenderService;
    SAILONE();
    ~SAILONE();
    bool SetSail();
    void releaseAll();
    SAILSTATE ss{};
    SAILTIME tm{};

    void FillIndex(uint16_t *pt); // filling an array of triangles
    void ClearVertex(SAILVERTEX *pv, uint32_t maxIdx);
    void goWave(SAILVERTEX *pv, uint32_t Delta_Time);
    void FillVertex(SAILVERTEX *pv);         // filling an array of vertices
    void SetTexGrid(SAILVERTEX *pv) const;   // setting coordinates in texture
    void SetGeometry();                      // setting parameters for creating sail geometry
    void SetRolling(bool bRoll);             // set roll-up sail
    void DoRollingStep(uint32_t Delta_Time); // perform the step of folding-unfolding the sail
    void TurnSail(float fTurnStep);          // turn the sail around the OY axis
    void CalculateMirrorSailIndex() override;
    bool GetGrid(glm::vec3 &pos, float perspect);
    float Trace(const glm::vec3 &src, const glm::vec3 &dst, bool bCannonTrace);
    float SSailTrace(glm::vec3 &src, glm::vec3 &dst, bool bCannonTrace);
    float TSailTrace(glm::vec3 &src, glm::vec3 &dst, bool bCannonTrace);
    float CheckSailSquar(int i, glm::vec3 &va, glm::vec3 &vb, glm::vec3 &vc, glm::vec3 &vsrc, glm::vec3 &vdst, bool bCannonTrace);
    void SetAllHole(uint32_t holeData) override;
    // number of vertices vertically and horizontally
    uint32_t m_dwRow, m_dwCol;
    float sailWidth, sailHeight;

  private:
    SAILGEOMETRY sgeo{};
    void GoVWave(SAILVERTEX *pv);
    void GoTWave(SAILVERTEX *pv);
    void DoSRollSail(SAILVERTEX *pv);
    void DoTRollSail(SAILVERTEX *pv);
    void DoSFreeSail(SAILVERTEX *pv);
    void DoTFreeSail(SAILVERTEX *pv);

    void SetTurnLimits();

    uint32_t GetHoleDword()
    {
        uint32_t v = 0, t = 1;
        for (auto i = 0; i < 12; i++, t <<= 1)
            if (ss.hole[i])
                v |= t;
        return v;
    }

    uint32_t GetMaxHoleCount() const
    {
        return ss.eSailType == SAIL_TREANGLE ? 10 : 12;
    }

    float GetDistanceFromPointTo3Point(const glm::vec3 &v, const glm::vec3 &vB1, const glm::vec3 &vB2, const glm::vec3 &vB3);

    // Wind exposure parameters
    // ------------------------------
    int VertIdx; // index in array of wind vectors
    int HorzIdx;
    bool WindUp; // raising the sail up from the wind
    float SumWind, MaxSumWind;
    bool bFreeSail; // free sails - when falling or flying

    glm::vec3 SailPnt[20]{};

    // ------------------------------------
    // Unfolding the sail
    int rollType;       // sail folding type
    SAILROLLING *sroll; // structure at the time of folding / unfolding of the sail
    bool bRolling;      // the sail is rolled on the yard

    SAIL *pp;
    // ------------------------------------
    // parameters for determining the position in the sail list
    int groupNum;       // sail group number in the node
    int mastNum;        // mast number
    NODE *hostNode;     // host
    glm::mat4 *pMatWorld; // the matrix of this node

    SAILTIEROPE sailtrope{};
    float oldWindAngl;

    // physics of influence on ship speed
    float maxSpeed; // Maximum sail speed
    float curSpeed; // Current speed

    // wind
    glm::vec3 sailWind{};
    void CalculateSailWind();

    int wind_incr, wind_add;

    int HostNum; // sail group number of the ship
    bool bDeleted;

    // Parameters limiting sail turn
    glm::vec3 m_vMastTrace[2];
    float m_fMaxAngle;
    float m_fMinAngle;

    int32_t surfaceTex;
    bool m_bIsGerald;
    int32_t m_nGeraldTex;

    float m_fHorzGeraldScale;
    float m_fVertGeraldScale;
};
