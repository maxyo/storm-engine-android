//============================================================================================
//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    Grass
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "supervisor.h"
#include "renderer.h"
#include "grs.h"
#include "grass_pack.hpp"
#include "vma.hpp"

#define MSG_GRASS_LOAD_DATA 40666
#define MSG_GRASS_SET_TEXTURE 41666
#define MSG_GRASS_SET_PARAM 42666

class GEOS;

class Character;

class Grass : public Entity
{
    storm::render::VertexDeclarationId vertexDecl_;

#pragma pack(push, 1)

    using Vertex = grass_pack::Vertex; // a corner of a blade in the buffer (grass_pack.hpp, vs_port_grass.sc)

    struct VSConstant
    {
        VSConstant()
        {
            x = y = z = 0.0f;
            w = 1.0f;
        };

        VSConstant(float _x, float _y, float _z, float _w)
        {
            x = _x;
            y = _y;
            z = _z;
            w = _w;
        };
        float x, y, z, w;
    };

#pragma pack(pop)

    struct AngleInfo
    {
        float dx, dz; // Direction
        float cs;     // Cosine of the angle between direction and source
    };

    struct GRSMapElementEx
    {
        float x, y, z; // The position of a blade of grass in the world
        union {
            struct
            {
                uint8_t frame; // Frame
                uint8_t h;     // Height
                uint8_t w;     // Width
                uint8_t ang;   // Angle of rotation
            };

            uint32_t data;
        };
    };

    enum RenderQuality
    {
        rq_full = 0,
        rq_middle = 1,
        rq_low = 2,
        rq_off = 3,
    };

  public:
    struct CharacterPos
    {
        glm::vec3 pos;     // Current position
        glm::vec3 lastPos; // Inertial position
        Character *chr;  // Character pointer
        int32_t useCounter; // Grass Influence Counter
    };

    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    Grass();
    ~Grass() override;

    // Initialization
    bool Init() override;
    // Work
    void Execute(uint32_t delta_time);
    void Realize(uint32_t delta_time);
    //
    uint64_t ProcessMessage(MESSAGE &message) override;

    void RestoreRender();

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
        case Stage::execute:
            Execute(delta);
            break;
        case Stage::realize:
            Realize(delta);
            break;
        case Stage::restore_render:
            RestoreRender();
            break;
        }
    }

    // Load data for grass
    bool LoadData(const char *patchName);
    // Set texture
    void SetTexture(const char *texName);

    std::vector<CharacterPos> characters;

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    // Render block
    void RenderBlock(const glm::vec3 &camPos, const PLANE *plane, int32_t numPlanes, int32_t mx, int32_t mz);
    // Box visibility check
    bool VisibleTest(const PLANE *plane, int32_t numPlanes, const glm::vec3 &min, const glm::vec3 &max);
    // The block's drawn blades are queued; the characters' grass contacts are counted
    void QueueBlock(int32_t mx, int32_t mz, float kLod);
    // The per-frame constants c42.. and the draws of the queued blades
    void DrawRuns();
    // The base heights of a new map (LoadData); the buffer is filled by UpdateWindow
    void PrepareHeights();
    // The blades of the blocks around the camera's block into the buffer, when the camera left the current window
    void UpdateWindow(int32_t camx, int32_t camz);
    // The visible blocks
    void DrawGrass(const glm::vec3 &pos, const PLANE *plane, int32_t numPlanes, int32_t left, int32_t right, int32_t top,
                   int32_t bottom);
    // Get the color
    static int32_t GetColor(glm::vec3 color);
    // Vertex declaration
    void CreateVertexDeclaration();

  private:
    // Render service
    VRenderer *rs;
    // Buffers: the blades of a window of blocks around the camera, 4 vertices each (UpdateWindow); the indices of the
    // blades drawn (DrawRuns)
    int32_t vb, ib;
    int32_t vbBlades = 0;                                        // capacity of vb
    grass_pack::Window window;                                   // the blocks in vb
    std::vector<int32_t> windowStart;                            // first blade of each window block in vb
    bool outsideLogged = false, reachLogged = false;
    // Texture
    int32_t texture;

    // Minimap
    GRSMiniMapElement *miniMap;
    // Full non-empty cached minimap
    std::vector<std::pair<int32_t, int32_t>> cachedMiniMap;
    // Minimap sizes
    int32_t miniX, miniZ;
    // Starting position of the map
    float startX, startZ;
    // Blocks with heights
    GRSMapElementEx *block;
    int32_t numElements;
    // Current parameters for grass turns
    AngleInfo angInfo[16];
    // Current swing phases
    float phase[7];
    // Current light source options
    glm::vec3 lDir;   // Direction of the source
    glm::vec3 lColor; // Source color
    glm::vec3 aColor; // Ambient light color

    std::vector<int32_t> blockChrs; // Indexes of characters processed by the block

    float lodSelect; // Lod selection range factor (kLod = kLod^lodSelect)
    float winForce;  // Wind speed coefficient 0..1
    glm::vec3 winDir;  // Normalized wind direction

    std::vector<std::pair<int32_t, int32_t>> runs; // first blade, count of each block drawn this frame, in order
    std::vector<uint8_t> chrNear;             // characters touching a drawn blade this frame
    float heightMin = 0.0f, heightStep = 1.0f; // the base heights of Vertex::height
    bool tooManyLogged = false;

    RenderQuality quality; // Rendering quality

    float cosPh1, sinPh2, sinPh5, sinPh6, winPow, winF10, kAmpWF, kDirWF, kLitWF;
    float windAng;
    int32_t initForce;

    VSConstant consts[42];

    char textureName[64];

    float m_fDataScale;
    float m_fMaxWidth;
    float m_fMaxHeight;
    float m_fMinVisibleDist;
    float m_fMaxVisibleDist;
    float m_fMinGrassLod;

    // boal grass parameters
    int32_t isGrassLightsOn;
};
