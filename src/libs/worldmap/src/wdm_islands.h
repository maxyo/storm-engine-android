//============================================================================================
//    Spirenkov Maxim, 2001
//--------------------------------------------------------------------------------------------
//    Sea Dogs II
//--------------------------------------------------------------------------------------------
//    WdmIslands
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "ptc_data.h"
#include "wdm_render_model.h"
#include "geometry.h"

class WdmIslandWaves : public WdmRenderModel
{
  public:
    WdmIslandWaves();

    void Update(float dltTime) override;
    void LRender(VDX9RENDER *rs) override;
    void Render(VDX9RENDER *rs, float k);

  private:
    float phase;
};

class WdmIslands : public WdmRenderObject
{
    struct Islands
    {
        WdmRenderModel *model; // Island model
        WdmRenderModel *area;  // Island area model
        WdmRenderModel *palms; // Model with palm trees
        WdmIslandWaves *waves; // Model with foam
        glm::mat4 toLocal;       // Conversion to the local island system
        std::string modelName; // Island Model Name
        glm::vec3 worldPosition; // The position of the island in the world
    };

    struct Label
    {
        std::string text;        // Label text
        glm::vec3 pos;             // Label position
        float l, t, r, b;        // Rectangle describing the label in screen coordinates
        float dl, dt, dr, db;    // Offsets to get a rectangle at a known point on the screen
        float textX, textY;      // Relative text position
        float iconX, iconY;      // The relative position of the picture
        float alpha;             // The current state of the rectangle
        float heightView;        // Height from which the label goes out
        int32_t font;               // Font index in font array
        int32_t icon;               // Image index
        uint32_t weight;         // Offset weight
        std::string id;          // Label ID
        uint32_t idHash;         // Hash value of identifier
        int32_t next;               // Next label on the list
        std::string locatorName; // The name of the locator in which it is located
    };

    struct Font
    {
        std::string name; // Font name
        int32_t id;          // Its identifier
    };

    struct Icons
    {
        float w, h;
        float u, v;
        uint32_t num;
        uint32_t frames;
        float fps;
        float frame;
        float f[2];
        int32_t blend;
        int32_t texture;
    };

    struct Quest
    {
        glm::vec3 pos;
        std::string name;
    };

    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    WdmIslands(WdmIslands &&) = delete;
    WdmIslands(const WdmIslands &) = delete;
    WdmIslands();
    ~WdmIslands() override;

    // Check for possible collision
    bool CollisionTest(glm::mat4 &objMtx, float length, float width, bool heighTest = true);
    // Check for the presence of triangles in this place
    bool ObstacleTest(float x, float z, float radius);

    // Read Island Data
    void SetIslandsData(ATTRIBUTES *apnt, bool isChange);

    // Find the direction to arrive at a given destination from the current
    void FindDirection(const glm::vec3 &position, const glm::vec3 &destination, glm::vec3 &direction) const;
    // Find the repulsive force
    void FindReaction(const glm::vec3 &position, glm::vec3 &reaction) const;
    // Find a random point for a merchant
    bool GetRandomMerchantPoint(glm::vec3 &p);
    // Get the coordinates of the quest locator
    bool GetQuestLocator(const char *locName, glm::vec3 &p);

    // Check if the boat is in the island zone
    bool CheckIslandArea(const char *islandName, float x, float z);
    // Get the closest point to the island zone
    void GetNearPointToArea(const char *islandName, float &x, float &z);

    void Update(float dltTime) override;
    void LRender(VDX9RENDER *rs) override;

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    bool IsShipInArea(int32_t islIndex, const glm::vec3 &pos);
    static bool AddEdges(const GEOS::VERTEX *vrt, int32_t numVrt);
    static bool FindNearPoint(const GEOS::VERTEX *vrt, int32_t numVrt);
    void LabelsReadIconParams(ATTRIBUTES *apnt);
    int32_t LabelsFind(const char *id, uint32_t hash);
    bool LabelsFindLocator(const char *name, glm::vec3 &pos) const;
    int32_t LabelsAddFont(const char *name);
    void LabelsRelease();
    static glm::vec3 Norm2D(const glm::vec3 &ret);

  private:
    // Model containing all locators
    WdmRenderModel *baseModel;
    // Pathfinder patch
    PtcData *patch;
    // Island models
    std::vector<Islands> islands;
    // Labels
    std::vector<Label> labels;
    // Fonts used by labels
    std::vector<Font> fonts;
    // Images
    Icons icons;
    // Labels registered for rendering
    std::vector<int32_t> labelSort;
    // Merchant destinations
    std::vector<glm::vec3> merchants;
    // Destination Points of Quest Encounters
    std::vector<Quest> quests;

    // Input table for finding labels
    int32_t labelsEntry[1024];

    static glm::mat4 curMatrix, locMatrix;
    static int32_t numEdges;
    static glm::vec3 curPos;
    static bool checkMode;

  public:
    static glm::vec3 centPos;
};

inline glm::vec3 WdmIslands::Norm2D(const glm::vec3 &v)
{
    auto ret = v;
    ret.y = 0.0f;
    double len = ret.x * ret.x + ret.z * ret.z;
    if (len >= 1e-30f)
    {
        len = 1.0f / sqrt(len);
        ret.x = static_cast<float>(len * ret.x);
        ret.z = static_cast<float>(len * ret.z);
    }
    else
    {
        ret = glm::vec3(0.0f);
    }
    return ret;
}
