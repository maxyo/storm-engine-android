//============================================================================================
//    Spirenkov Maxim, 2001
//--------------------------------------------------------------------------------------------
//    Sea Dogs II
//--------------------------------------------------------------------------------------------
//    WdmIslands
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#include "wdm_islands.h"

#include "core.h"
#include "math_inlines.h"

#include "wdm_camera.h"
#include "wdm_ship.h"
#include "world_map.h"
#include "string_compare.hpp"

#include "storm/render/frame_owner.hpp"

#include <algorithm>

namespace
{
// The labels on the display (LRender), in dp (1/160 inch)
constexpr float kLabelMinDp = 16.0f;    // the line of the smallest label font at least
constexpr float kLabelOutlineDp = 1.5f; // the dark outline around the text
constexpr float kLabelGapDp = 4.0f;     // the space colliding labels keep between them
constexpr float kLabelHoldDp = 3.0f;    // how much of it a label already shown may lose before it yields
// How fast labels fade (of their full opacity, per second)
constexpr float kLabelHeightFade = 1.5f; // the camera rose above the label's height (the game's pace)
constexpr float kLabelYieldFade = 6.0f;  // a heavier label covers it, or it comes free

// Pixels of the render target (`viewportWidth` wide) per dp of the display. The display's density
// is the frame owner's (Android: the display's DPI / 160). Without a surface there is no frame to
// draw labels on: 1 (the game's sizes), reported once.
float LabelPxPerDp(float viewportWidth)
{
    const auto *owner = storm::render::FrameOwner::Get();
    const storm::render::SurfaceMetrics surface = owner ? owner->Surface() : storm::render::SurfaceMetrics{};
    if (surface.valid && surface.width_px > 0 && surface.density > 0.0f)
        return surface.density * viewportWidth / static_cast<float>(surface.width_px);
    static bool reported = false;
    if (!reported)
    {
        core.Trace("World map: no display surface to size the labels for; they are drawn at the game's font sizes");
        reported = true;
    }
    return 1.0f;
}
} // namespace

// ============================================================================================
// Collision data
// ============================================================================================

glm::vec3 WdmIslands::centPos;
int32_t WdmIslands::numEdges;
glm::vec3 WdmIslands::curPos;
bool WdmIslands::checkMode;
glm::mat4 WdmIslands::curMatrix;

// ============================================================================================
// Construction, destruction
// ============================================================================================

WdmIslands::WdmIslands()
{
    labelSort.reserve(1024);
    icons.texture = -1;
    LabelsRelease();
    wdmObjects->islands = this;
    // Loading the base model
    baseModel = static_cast<WdmRenderModel *>(
        wdmObjects->wm->CreateModel(new WdmRenderModel(), "islands\\islands", false, false, false, 1));
    if (!baseModel || !baseModel->geo)
        return;
    // Geometry information
    GEOS::INFO ginfo;
    GEOS::LABEL label;
    baseModel->geo->GetInfo(ginfo);
    // Finding the size of the world and iterating over the locators
    glm::vec3 vmin, vmax, center(0.0f), vmn, vmx;
    auto isMin = false, isMax = false;
    for (int32_t i = 0; i < ginfo.nlabels; i++)
    {
        baseModel->geo->GetLabel(i, label);
        if (!label.group_name || !label.group_name[0])
            continue;
        if (storm::iEquals(label.group_name, "locators"))
        {
            if (storm::iEquals(label.name, "min"))
            {
                vmn = storm::math::pos(*((glm::mat4 *)label.m));
                isMin = true;
            }
            if (storm::iEquals(label.name, "max"))
            {
                vmx = storm::math::pos(*((glm::mat4 *)label.m));
                isMax = true;
            }
        }
        else if (storm::iEquals(label.group_name, "merchant"))
        {
            merchants.push_back(storm::math::pos(*((glm::mat4 *)label.m)));
        }
        else if (label.name && label.name[0] && storm::iEquals(label.group_name, "quests"))
        {
            quests.push_back(Quest{storm::math::pos(*((glm::mat4 *)label.m)), label.name});
            // Quest & q = quests[quests.Add()];
            // q.pos = ((glm::mat4 *)label.m)->Pos();
            // q.name = label.name;
        }
    }
    if (isMin && isMax)
    {
        vmin.x = std::min(vmn.x, vmx.x);
        vmin.y = 0.0f;
        vmin.z = std::min(vmn.z, vmx.z);
        vmax.x = std::max(vmn.x, vmx.x);
        vmax.y = 0.0f;
        vmax.z = std::max(vmn.z, vmx.z);
        center = (vmin + vmax) * -0.5f;
        vmin += center;
        vmax += center;
        wdmObjects->SetWorldSize(vmax.x * 2.0f, vmax.z * 2.0f);
    }
    // Reading the islands
    std::string name;
    for (int32_t i = 0; i < ginfo.nlabels; i++)
    {
        // receive information
        baseModel->geo->GetLabel(i, label);
        if (!label.group_name || !label.group_name[0])
            continue;
        if (!label.name || !label.name[0])
            continue;
        if (!storm::iEquals(label.group_name, "islands"))
            continue;
        // Skip if added
        int32_t j;
        for (j = 0; j < islands.size(); j++)
        {
            if (islands[j].modelName == label.name)
                break;
        }
        if (j < islands.size())
            continue;
        name = "islands\\";
        name += label.name;
        // Loading
        auto *model = static_cast<WdmRenderModel *>(
            wdmObjects->wm->CreateModel(new WdmRenderModel(), name.c_str(), false, false, true, 2));
        if (model)
        {
            // General
            islands.push_back(Islands{});
            // Islands & isl = islands[islands.Add()];
            auto &isl = islands.back();
            isl.model = model;
            model->mtx = *((glm::mat4 *)label.m);
            storm::math::pos(model->mtx) += center;
            isl.toLocal = model->mtx;
            storm::math::invertRigid(isl.toLocal);
            isl.modelName = label.name;
            isl.worldPosition = storm::math::pos(model->mtx);
            // Model describing the area of the island
            name += "_area";
            isl.area = static_cast<WdmRenderModel *>(
                wdmObjects->wm->CreateModel(new WdmRenderModel(), name.c_str(), false, false, false, 3));
            if (!isl.area)
            {
                core.Trace("World map: can't load model of island's area: %s", name.c_str());
            }
            // Palm trees
            name = "islands\\";
            name += label.name;
            name += "_palms";
            isl.palms = static_cast<WdmRenderModel *>(
                wdmObjects->wm->CreateModel(new WdmRenderModel(), name.c_str(), false, false, true, 4, 800));
            if (isl.palms)
            {
                static const auto *techName = "WdmModelDrawStdAlphaTest";
                isl.palms->SetTech(techName, techName);
            }
            // Foam
            name = "islands\\";
            name += label.name;
            name += "_waves";
            isl.waves = static_cast<WdmIslandWaves *>(
                wdmObjects->wm->CreateModel(new WdmIslandWaves(), name.c_str(), false, false, true, 4));
        }
        else
        {
            core.Trace("World map: can't load model of island: %s", name.c_str());
        }
    }
    // Loading the patch
    patch = new PtcData();
    if (!patch->Load("RESOURCE\\MODELS\\WorldMap\\islands\\islands_patch.ptc"))
    {
        delete patch;
        patch = nullptr;
    }
}

WdmIslands::~WdmIslands()
{
    if (patch)
    {
        delete patch;
        patch = nullptr;
    }
    wdmObjects->islands = nullptr;
    for (int32_t i = 0; i < islands.size(); i++)
    {
        wdmObjects->wm->DeleteObject(islands[i].model);
        islands[i].model = nullptr;
    }
    if (baseModel)
        wdmObjects->wm->DeleteObject(baseModel);
    islands.clear();
    LabelsRelease();
}

// Check for possible collision
bool WdmIslands::CollisionTest(glm::mat4 &objMtx, float length, float width, bool heighTest)
{
    const auto maxHeightInTest = 0.5f;
    // Radius of a rectangle on a plane
    const auto boxRadius = sqrtf(length * length + width * width);
    if (boxRadius < 0.0000001f)
        return false;
    // Box radius
    float checkRadius = boxRadius + maxHeightInTest;
    if (!heighTest)
        checkRadius += 1000000.0f;
    // Test parameters
    numEdges = 0;
    centPos = glm::vec3(0.0f);
    checkMode = !heighTest;
    // walk through all the islands
    for (int32_t i = 0; i < islands.size(); i++)
    {
        // The island we work with
        Islands &island = islands[i];
        // Transform to the local coordinates system
        glm::mat4 locMtx = island.toLocal * objMtx;

        // Check hitting the island sphere
        const float dist2 = storm::math::lengthSquared(storm::math::pos(locMtx) - island.model->center);
        const float maxDist = boxRadius + island.model->radius;
        if (dist2 >= maxDist * maxDist)
            continue;
        // test triangles
        GEOS::PLANE p[5];
        // Direction vector
        p[0].nrm.x = storm::math::vz(locMtx).x;
        p[0].nrm.y = 0.0f;
        p[0].nrm.z = storm::math::vz(locMtx).z;
        float radius = p[0].nrm.x * p[0].nrm.x + p[0].nrm.z * p[0].nrm.z;
        // Skip broken model
        if (radius < 0.000001f)
            continue;
        // Direction normal
        radius = 1.0f / sqrtf(radius);
        p[0].nrm.x *= radius;
        p[0].nrm.z *= radius;
        // Remaining normals
        p[1].nrm.x = -p[0].nrm.x;
        p[1].nrm.y = 0.0f;
        p[1].nrm.z = -p[0].nrm.z;
        p[2].nrm.x = -p[0].nrm.z;
        p[2].nrm.y = 0.0f;
        p[2].nrm.z = p[0].nrm.x;
        p[3].nrm.x = p[0].nrm.z;
        p[3].nrm.y = 0.0f;
        p[3].nrm.z = -p[0].nrm.x;
        p[4].nrm.x = 0.0f;
        p[4].nrm.y = 1.0f;
        p[4].nrm.z = 0.0f;
        // Distances to planes
        p[0].d =
            (p[0].nrm.x * length + storm::math::pos(locMtx).x) * p[0].nrm.x + (p[0].nrm.z * length + storm::math::pos(locMtx).z) * p[0].nrm.z;
        p[1].d =
            (p[1].nrm.x * length + storm::math::pos(locMtx).x) * p[1].nrm.x + (p[1].nrm.z * length + storm::math::pos(locMtx).z) * p[1].nrm.z;
        p[2].d =
            (p[2].nrm.x * width + storm::math::pos(locMtx).x) * p[2].nrm.x + (p[2].nrm.z * width + storm::math::pos(locMtx).z) * p[2].nrm.z;
        p[3].d =
            (p[3].nrm.x * width + storm::math::pos(locMtx).x) * p[3].nrm.x + (p[3].nrm.z * width + storm::math::pos(locMtx).z) * p[3].nrm.z;
        p[4].d = maxHeightInTest;
        // Sphere position
        GEOS::VERTEX v;
        v.x = storm::math::pos(locMtx).x;
        v.y = storm::math::pos(locMtx).y;
        v.z = storm::math::pos(locMtx).z;
        curPos = storm::math::pos(locMtx);
        // Conversion matrix to world coordinates
        curMatrix = island.model->mtx;
        // looking for hitting polygons
        island.model->geo->Clip(p, heighTest ? 5 : 4, v, checkRadius, AddEdges);
    }
    if (numEdges)
    {
        centPos *= 1.0f / numEdges;
        return true;
    }
    return false;
}

bool WdmIslands::AddEdges(const GEOS::VERTEX *vrt, int32_t numVrt)
{
    // Skip broken Data
    if (numVrt < 3)
        return true;
    if (checkMode)
    {
        centPos = storm::math::transformPoint(curMatrix, glm::vec3(vrt[0].x, 0.0f, vrt[0].z));
        numEdges = 1;
        return false;
    }
    // try to determine the location of the point relative to the plane
    const glm::vec3 v1(vrt[1].x - vrt[0].x, vrt[1].y - vrt[0].y, vrt[1].z - vrt[0].z);
    const glm::vec3 v2(vrt[2].x - vrt[1].x, vrt[2].y - vrt[1].y, vrt[2].z - vrt[1].z);
    const glm::vec3 v = glm::cross(v1, v2);
    const float d = glm::dot(v, curPos) - glm::dot(v, glm::vec3(vrt[0].x, vrt[0].y, vrt[0].z));
    if (d < 0)
        return true;
    // Adding edges
    numEdges += numVrt;
    for (int32_t i = 0; i < numVrt; i++)
    {
        centPos += storm::math::transformPoint(curMatrix, glm::vec3(vrt[i].x, 0.0f, vrt[i].z));
    }
    // continue
    return true;
}

// Check for the presence of triangles in this place
bool WdmIslands::ObstacleTest(float x, float z, float radius)
{
    if (radius <= 0.0f)
        return false;
    const glm::vec3 wPos(x, 0.0f, z);
    // walk through all the islands
    for (int32_t i = 0; i < islands.size(); i++)
    {
        // The island we work with
        Islands &island = islands[i];
        // Converting to the local island system
        const glm::vec3 pos = storm::math::transformPoint(island.toLocal, wPos);
        GEOS::PLANE p[4];
        p[0].nrm.x = 0.0f;
        p[0].nrm.y = 0.0f;
        p[0].nrm.z = 1.0f;
        p[0].d = pos.z + radius;
        p[1].nrm.x = 0.0f;
        p[1].nrm.y = 0.0f;
        p[1].nrm.z = -1.0f;
        p[1].d = -(pos.z - radius);
        p[2].nrm.x = 1.0f;
        p[2].nrm.y = 0.0f;
        p[2].nrm.z = 0.0f;
        p[2].d = pos.x + radius;
        p[3].nrm.x = -1.0f;
        p[3].nrm.y = 0.0f;
        p[3].nrm.z = 0.0f;
        p[3].d = -(pos.x - radius);
        GEOS::VERTEX vrt;
        vrt.x = pos.x;
        vrt.y = 0.0f;
        vrt.z = pos.z;
        if (island.model->geo->Clip(p, 4, vrt, 50.0f, nullptr))
            return true;
    }
    return false;
}

// Read Island Data
void WdmIslands::SetIslandsData(ATTRIBUTES *apnt, bool isChange)
{
    glm::vec3 pos;
    if (!isChange)
    {
        LabelsRelease();
        // Reading the parameters of the pictures
        LabelsReadIconParams(apnt);
    }
    if (!apnt)
        return;
    // Get the description of the labels
    apnt = apnt->FindAClass(apnt, "labels");
    if (!apnt)
        return;
    // Number of labels
    const uint32_t numAttr = apnt->GetAttributesNum();
    // Looping through all labels
    for (uint32_t i = 0; i < numAttr; i++)
    {
        // get access to the description of the label
        ATTRIBUTES *a = apnt->GetAttributeClass(i);
        if (!a)
            continue;
        // Get the parameters of the label
        const char *id = a->GetAttribute("id");
        const char *locator = a->GetAttribute("locator");
        const char *text = a->GetAttribute("text");
        const int32_t icon = static_cast<int32_t>(a->GetAttributeAsDword("icon", static_cast<uint32_t>(-1)));
        const char *font = a->GetAttribute("font");
        const float heightView = a->GetAttributeAsFloat("heightView", 250.0);
        const uint32_t weight = a->GetAttributeAsDword("weight", 0);
        // Check for sufficiency
        if (!id || !text || !locator || !locator[0])
        {
            core.Trace("World map: label \"%s\" will be skipping...", apnt->GetAttributeName(i));
            continue;
        }
        // looking for a label among existing
        const uint32_t hash = MakeHashValue(id);
        int32_t index = LabelsFind(id, hash);
        if (index < 0)
        {
            if (!LabelsFindLocator(locator, pos))
            {
                core.Trace("World map: locator \"%s\" in label \"%s\" not found...", locator,
                           apnt->GetAttributeName(i));
                continue;
            }
            // Adding a new label
            const int32_t iEntry = hash & (sizeof(labelsEntry) / sizeof(labelsEntry[0]) - 1);
            // index = labels.Add();
            labels.push_back(Label{});
            index = labels.size() - 1;
            labels[index].id = id;
            labels[index].idHash = hash;
            labels[index].next = labelsEntry[iEntry];
            labelsEntry[iEntry] = index;
            labels[index].locatorName = locator;
            labels[index].alpha = 255.0f;
            labels[index].placed = true;
        }
        else
        {
            if (!storm::iEquals(labels[index].locatorName, locator))
            {
                if (LabelsFindLocator(locator, pos))
                {
                    labels[index].locatorName = locator;
                }
            }
        }
        // Text
        labels[index].text = text ? text : ""; // ~!~
        // Font
        labels[index].font = LabelsAddFont(font);
        // Picture
        labels[index].icon = icon < static_cast<int32_t>(icons.num) ? icon : icons.num - 1;
        // Position
        labels[index].pos = pos;
        // Blanking height
        labels[index].heightView = heightView;
        labels[index].weight = weight;
        // The label's size at the font's own size (LRender scales it for the display)
        const float textWidth = static_cast<float>(wdmObjects->rs->StringWidth(labels[index].text, labels[index].font));
        const float textHeight = static_cast<float>(wdmObjects->rs->CharHeight(labels[index].font));
        const bool withIcon = labels[index].icon >= 0;
        labels[index].width = textWidth + (withIcon ? icons.w + 4.0f : 0.0f);
        labels[index].height = withIcon ? std::max(textHeight, icons.h) : textHeight;
        // The picture on the left, the text after it, both centred vertically
        labels[index].iconX = 0.0f;
        labels[index].iconY = withIcon ? (labels[index].height - icons.h) * 0.5f : 0.0f;
        labels[index].textX = withIcon ? icons.w + 4.0f : 0.0f;
        labels[index].textY = (labels[index].height - textHeight) * 0.5f;

        // Store locator position as label attribute
        auto* position_attr = a->VerifyAttributeClass("position");
        position_attr->SetAttributeUseFloat("x", pos.x);
        position_attr->SetAttributeUseFloat("y", pos.y);
        position_attr->SetAttributeUseFloat("z", pos.z);
    }
}

void WdmIslands::LabelsReadIconParams(ATTRIBUTES *apnt)
{
    if (!apnt)
        return;
    apnt = apnt->FindAClass(apnt, "icon");
    if (!apnt)
        return;
    icons.w = static_cast<float>(apnt->GetAttributeAsDword("width", 32));
    icons.h = static_cast<float>(apnt->GetAttributeAsDword("height", 32));
    icons.num = apnt->GetAttributeAsDword("num", 8);
    icons.frames = apnt->GetAttributeAsDword("frames", 1);
    icons.fps = apnt->GetAttributeAsFloat("fps", 0.0f);
    icons.frame = 0.0f;
    icons.f[0] = 0.0f;
    icons.f[1] = 0.0f;
    icons.blend = 0;
    const char *texName = apnt->GetAttribute("texture");
    if (!texName)
        texName = "";
    std::string name = "WorldMap\\Interfaces\\";
    name += texName;
    icons.texture = wdmObjects->rs->TextureCreate(name.c_str());
    // Dimensions uv
    const float tw = icons.w * icons.frames;
    const float th = icons.h * icons.num;
    if (tw)
    {
        icons.u = icons.w / tw;
    }
    else
    {
        icons.u = 0.0f;
    }
    if (th)
    {
        icons.v = icons.h / th;
    }
    else
    {
        icons.v = 0.0f;
    }
}

int32_t WdmIslands::LabelsFind(const char *id, uint32_t hash)
{
    if (!id)
        id = "";
    int32_t i = hash & (sizeof(labelsEntry) / sizeof(labelsEntry[0]) - 1);
    for (i = labelsEntry[i]; i >= 0; i = labels[i].next)
    {
        if (labels[i].idHash == hash)
        {
            if (storm::iEquals(labels[i].id, id))
                return i;
        }
    }
    return -1;
}

bool WdmIslands::LabelsFindLocator(const char *name, glm::vec3 &pos) const
{
    if (!baseModel || !name || !name[0])
        return false;
    // Geometry information
    GEOS::INFO ginfo;
    GEOS::LABEL label;
    baseModel->geo->GetInfo(ginfo);
    for (int32_t i = 0; i < ginfo.nlabels; i++)
    {
        baseModel->geo->GetLabel(i, label);
        if (!label.group_name || !label.group_name[0])
            continue;
        if (!storm::iEquals(label.group_name, "labels"))
            continue;
        // if(!storm::iEquals(label.group_name, "geometry")) continue;
        if (storm::iEquals(label.name, name))
        {
            pos = storm::math::pos(*((glm::mat4 *)label.m));
            return true;
        }
    }
    return false;
}

int32_t WdmIslands::LabelsAddFont(const char *name)
{
    if (!name)
        name = "";
    for (int32_t i = 0; i < fonts.size(); i++)
    {
        if (storm::iEquals(fonts[i].name, name))
        {
            return fonts[i].id;
        }
    }
    // Font & font = fonts[index = fonts.Add()];
    Font font;
    font.name = name;
    font.id = wdmObjects->rs->LoadFont((char *)name);
    if (font.id < 0)
    {
        core.Trace("World map: label font \"%s\" not loaded, the labels that use it get the default font", name);
        font.id = FONT_DEFAULT;
    }
    fonts.push_back(font);
    // The renderer's id, as for a font already loaded (it used to be the index into fonts: the first
    // label of each new font was printed with another font, worked around in worldmap_init.c "BugFix").
    return font.id;
}

void WdmIslands::LabelsRelease()
{
    // Remove all labels
    labels.clear();
    for (int32_t i = 0; i < sizeof(labelsEntry) / sizeof(labelsEntry[0]); i++)
    {
        labelsEntry[i] = -1;
    }
    // Remove all fonts
    for (int32_t i = 0; i < fonts.size(); i++)
    {
        if (fonts[i].id != FONT_DEFAULT)
        {
            wdmObjects->rs->UnloadFont(fonts[i].id);
        }
    }
    // Delete all pictures
    if (icons.texture >= 0)
    {
        wdmObjects->rs->TextureRelease(icons.texture);
    }
    memset(&icons, 0, sizeof(icons));
    icons.texture = -1;
}

void WdmIslands::Update(float dltTime)
{
    if (wdmObjects->playerShip)
    {
        glm::vec3 pos;
        wdmObjects->playerShip->GetPosition(pos.x, pos.z, pos.y);
        pos.y = 0.0f;
        for (int32_t i = 0; i < islands.size(); i++)
        {
            if (IsShipInArea(i, pos))
            {
                if (wdmObjects->wm->AttributesPointer)
                {
                    wdmObjects->wm->AttributesPointer->SetAttribute("island", (char *)islands[i].modelName.c_str());
                    ATTRIBUTES *a =
                        wdmObjects->wm->AttributesPointer->FindAClass(wdmObjects->wm->AttributesPointer, "island");
                    if (a)
                    {
                        a->SetAttributeUseFloat("x", islands[i].worldPosition.x);
                        a->SetAttributeUseFloat("z", islands[i].worldPosition.z);
                    }
                    wdmObjects->curIsland = (char *)islands[i].modelName.c_str();
                }
                return;
            }
        }
    }
    wdmObjects->curIsland = nullptr;
    wdmObjects->wm->AttributesPointer->SetAttribute("island", "");
    ATTRIBUTES *a = wdmObjects->wm->AttributesPointer->FindAClass(wdmObjects->wm->AttributesPointer, "island");
    if (a)
    {
        a->SetAttributeUseFloat("x", 0.0f);
        a->SetAttributeUseFloat("z", 0.0f);
    }
}

void WdmIslands::LRender(VRenderer *rs)
{
    // Draw a patch if needed
    if (wdmObjects->isDebug)
    {
        if (patch && core.Controls->GetDebugAsyncKeyState('1') < 0)
        {
            patch->DebugDraw(rs, core.GetDeltaTime() * 0.001f);
        }
    }
    // Update the state of the pictures
    icons.frame += core.GetDeltaTime() * 0.001f * icons.fps;
    icons.frame = (icons.frame / icons.frames - static_cast<int32_t>(icons.frame / icons.frames)) * icons.frames;
    if (icons.frame < 0.0f)
    {
        icons.frame += icons.frames;
    }
    icons.f[0] = static_cast<float>(static_cast<int32_t>(icons.frame));
    icons.blend = static_cast<int32_t>((icons.frame - icons.f[0]) * 255.0f);
    if (icons.blend < 0)
        icons.blend = 0;
    if (icons.blend > 255)
        icons.blend = 255;
    icons.blend |= (icons.blend << 24) | (icons.blend << 16) | (icons.blend << 8);
    if (icons.f[0] < 0)
        icons.f[0] = icons.frames - 1.0f;
    if (icons.f[0] >= icons.frames)
        icons.f[0] = 0.0f;
    icons.f[1] = icons.f[0] + 1;
    if (icons.f[1] < 0)
        icons.f[1] = icons.frames - 1.0f;
    if (icons.f[1] >= icons.frames)
        icons.f[1] = 0.0f;
    icons.f[0] *= icons.u;
    icons.f[1] *= icons.u;
    // get the current transformation matrix
    static glm::mat4 mtx, view, prj;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
    rs->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prj));
    mtx = prj * view;
    // Get the current vp sizes
    D3DVIEWPORT9 vp;
    rs->GetViewport(&vp);
    const auto w = static_cast<float>(vp.Width);
    const auto h = static_cast<float>(vp.Height);
    // The labels' size: the game's fonts are pixel sizes of a PC screen, far too small on a phone.
    // All labels grow by one factor (the game's proportions stay) so that the smallest font's line
    // is at least kLabelMinDp of the display, at every height of the camera (the labels are drawn
    // at a size on the screen, not on the map); a dark outline keeps them readable over land and sea.
    const float pxPerDp = LabelPxPerDp(w);
    float smallest = 0.0f;
    for (const Font &font : fonts)
        if (const auto height = static_cast<float>(rs->CharHeight(font.id)); height > 0.0f && (smallest == 0.0f || height < smallest))
            smallest = height;
    const float scale = smallest > 0.0f ? std::max(1.0f, kLabelMinDp * pxPerDp / smallest) : 1.0f;
    const float outline = std::max(1.0f, kLabelOutlineDp * pxPerDp);
    // The height of the camera: each kind of label shows below its own (heightView)
    const float cameraHeight = wdmObjects->camera->realHeight;
    const float seconds = core.GetDeltaTime() * 0.001f;
    // Projecting to the screen; the labels that may show take part in the layout
    labelSort.clear();
    labelBoxes.clear();
    storm::math::ProjectedPoint prjVertex;
    for (int32_t i = 0; i < labels.size(); i++)
    {
        Label &label = labels[i];
        storm::math::projectPoints(mtx, &label.pos, &prjVertex, 1, w * 0.5f, h * 0.5f, 0, 0);
        // Centred over its point, below it
        label.l = prjVertex.x - 0.5f * label.width * scale;
        label.t = prjVertex.y;
        label.r = label.l + label.width * scale;
        label.b = label.t + label.height * scale;
        label.onScreen = label.l < w && label.t < h && label.r >= 0.0f && label.b >= 0.0f;
        const bool mayShow = label.onScreen && label.heightView > cameraHeight && (!label.text.empty() || label.icon >= 0);
        if (!mayShow)
        {
            label.placed = false;
            continue;
        }
        labelSort.push_back(i);
        labelBoxes.push_back({label.l, label.t, label.r, label.b, label.weight, label.placed});
    }
    // Colliding labels: the heavier shows (wdm_label_layout.hpp)
    labelLayout.Place(labelBoxes, kLabelGapDp * pxPerDp, kLabelHoldDp * pxPerDp);
    for (size_t i = 0; i < labelSort.size(); i++)
        labels[labelSort[i]].placed = labelBoxes[i].shown;
    // Draw icons and write text
    for (Label &label : labels)
    {
        // A label the camera's height hides fades at the game's pace, one a heavier label covers quickly
        const float rate = (label.heightView > cameraHeight ? kLabelYieldFade : kLabelHeightFade) * 255.0f * seconds;
        label.alpha = label.placed ? std::min(255.0f, label.alpha + rate) : std::max(0.0f, label.alpha - rate);
        if (label.alpha < 1.0f || !label.onScreen)
            continue;
        // writing a text
        const uint32_t color = (static_cast<int32_t>(label.alpha) << 24) | 0xffffff;
        rs->PrintOutlined(label.font, color, scale, outline, static_cast<int32_t>(label.l + label.textX * scale),
                          static_cast<int32_t>(label.t + label.textY * scale), label.text);
        // Draw a picture
        if (label.icon < 0)
            continue;
        rs->TextureSet(0, icons.texture);
        rs->TextureSet(1, icons.texture);
        rs->SetRenderState(D3DRS_TEXTUREFACTOR, static_cast<uint32_t>(icons.blend));
        // Filling the vertices
        static struct
        {
            float x, y, z, rhw;
            uint32_t c;
            float tu1, tv1;
            float tu2, tv2;
        } drawbuf[4];
        // 0
        drawbuf[0].x = label.l + label.iconX * scale;
        drawbuf[0].y = label.t + label.iconY * scale;
        drawbuf[0].z = 0.0f;
        drawbuf[0].rhw = 0.5f;
        drawbuf[0].c = color;
        drawbuf[0].tu1 = icons.f[0];
        drawbuf[0].tv1 = label.icon * icons.v;
        drawbuf[0].tu2 = icons.f[1];
        drawbuf[0].tv2 = drawbuf[0].tv1;
        // 1
        drawbuf[1].x = drawbuf[0].x;
        drawbuf[1].y = drawbuf[0].y + icons.h * scale;
        drawbuf[1].z = 0.0f;
        drawbuf[1].rhw = 0.5f;
        drawbuf[1].c = color;
        drawbuf[1].tu1 = drawbuf[0].tu1;
        drawbuf[1].tv1 = drawbuf[0].tv1 + icons.v;
        drawbuf[1].tu2 = drawbuf[0].tu2;
        drawbuf[1].tv2 = drawbuf[1].tv1;
        // 2
        drawbuf[2].x = drawbuf[0].x + icons.w * scale;
        drawbuf[2].y = drawbuf[0].y;
        drawbuf[2].z = 0.0f;
        drawbuf[2].rhw = 0.5f;
        drawbuf[2].c = color;
        drawbuf[2].tu1 = drawbuf[0].tu1 + icons.u;
        drawbuf[2].tv1 = drawbuf[0].tv1;
        drawbuf[2].tu2 = drawbuf[0].tu2 + icons.u;
        drawbuf[2].tv2 = drawbuf[2].tv1;
        // 3
        drawbuf[3].x = drawbuf[0].x + icons.w * scale;
        drawbuf[3].y = drawbuf[0].y + icons.h * scale;
        drawbuf[3].z = 0.0f;
        drawbuf[3].rhw = 0.5f;
        drawbuf[3].c = color;
        drawbuf[3].tu1 = drawbuf[2].tu1;
        drawbuf[3].tv1 = drawbuf[0].tv1 + icons.v;
        drawbuf[3].tu2 = drawbuf[2].tu2;
        drawbuf[3].tv2 = drawbuf[3].tv1;
        // draw the result
        rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2, 2, drawbuf,
                            sizeof(drawbuf[0]), "WdmDrawLabelIcon");
    }
}

// Find the direction to arrive at a given destination from the current
void WdmIslands::FindDirection(const glm::vec3 &position, const glm::vec3 &destination, glm::vec3 &direction) const
{
    // If there is no patch, then go in a straight line
    if (!patch)
    {
        direction = Norm2D(destination - position);
        return;
    }
    // trying to determine the node on which we are standing and the node to which we need to sail
    float y = 0.0f;
    const int32_t from = patch->FindNode(position, y);
    y = 0.0f;
    int32_t to = patch->FindNode(destination, y);
    if (from < 0 || to < 0)
    {
        direction = Norm2D(destination - position);
        return;
    }
    // Getting directions
    if (!patch->FindPathDir(from, position, to, destination, to, direction))
    {
        direction = Norm2D(destination - position);
        return;
    }
    direction = Norm2D(direction - position);
}

// Find the repulsive force
void WdmIslands::FindReaction(const glm::vec3 &position, glm::vec3 &reaction) const
{
    if (patch)
    {
        float y = 0.0f;
        const int32_t n = patch->FindNode(position, y);
        if (n >= 0)
        {
            patch->FindForce(n, position, 20.0f, reaction);
        }
        else
        {
            reaction = glm::vec3(0.0f);
        }
    }
    else
    {
        reaction = glm::vec3(0.0f);
    }
}

// Find a random point for a merchant
bool WdmIslands::GetRandomMerchantPoint(glm::vec3 &p)
{
    if (merchants.size() <= 0)
    {
        p = glm::vec3(0.0f);
        return false;
    }
    p = merchants[rand() % merchants.size()];
    return true;
}

// Get the coordinates of the quest locator
bool WdmIslands::GetQuestLocator(const char *locName, glm::vec3 &p)
{
    if (!locName || !locName[0])
    {
        p = glm::vec3(0.0f);
        return false;
    }
    for (int32_t i = 0; i < quests.size(); i++)
    {
        if (storm::iEquals(quests[i].name, std::string_view(locName)))
        {
            p = quests[i].pos;
            return true;
        }
    }
    p = glm::vec3(0.0f);
    return false;
}

// Check if the ship is in the island zone
bool WdmIslands::CheckIslandArea(const char *islandName, float x, float z)
{
    for (int32_t i = 0; i < islands.size(); i++)
    {
        if (!islands[i].area)
            continue;
        if (islands[i].modelName == islandName)
        {
            return IsShipInArea(i, glm::vec3(x, 0.0f, z));
        }
    }
    return false;
}

// Get the closest point to the island zone
void WdmIslands::GetNearPointToArea(const char *islandName, float &x, float &z)
{
    // Looking for an area
    int32_t i;
    for (i = 0; i < islands.size(); i++)
    {
        if (!islands[i].area)
            continue;
        if (islands[i].modelName == islandName)
        {
            break;
        }
    }
    if (i >= islands.size())
    {
        return;
    }
    // iterate over the edges choosing the nearest point
    curPos.x = x;
    curPos.y = 0.0f;
    curPos.z = z;
    checkMode = false;
    GEOS::VERTEX v;
    v.x = 0.0f;
    v.y = 0.0f;
    v.z = 0.0f;
    islands[i].area->geo->Clip(nullptr, 0, v,
                               wdmObjects->worldSizeX * wdmObjects->worldSizeX +
                                   wdmObjects->worldSizeZ * wdmObjects->worldSizeZ,
                               FindNearPoint);
    if (checkMode)
    {
        x = centPos.x;
        z = centPos.z;
    }
}

bool WdmIslands::FindNearPoint(const GEOS::VERTEX *vrt, int32_t numVrt)
{
    // Skip broken Data
    for (int32_t i = 0; i < numVrt; i++)
    {
        const float dx = vrt[i].x - curPos.x;
        const float dz = vrt[i].z - curPos.z;
        const float dst = dx * dx + dz * dz;
        if (!checkMode || dst < centPos.y)
        {
            checkMode = true;
            centPos.x = vrt[i].x;
            centPos.z = vrt[i].z;
            centPos.y = dst;
        }
    }
    return true;
}

bool WdmIslands::IsShipInArea(int32_t islIndex, const glm::vec3 &pos)
{
    static const float testRadius = 0.01f;
    // No area, no intersection
    if (!islands[islIndex].area)
        return false;
    // Checking the contact of the radii
    const float dist2 = storm::math::lengthSquared(pos - islands[islIndex].area->center);
    const float rad = testRadius + islands[islIndex].area->radius;
    if (dist2 > rad * rad)
        return false;
    // Vertical planes
    GEOS::PLANE p[4];
    p[0].nrm.x = 0.0f;
    p[0].nrm.y = 0.0f;
    p[0].nrm.z = 1.0f;
    p[0].d = pos.z + testRadius;
    p[1].nrm.x = 0.0f;
    p[1].nrm.y = 0.0f;
    p[1].nrm.z = -1.0f;
    p[1].d = -(pos.z - testRadius);
    p[2].nrm.x = 1.0f;
    p[2].nrm.y = 0.0f;
    p[2].nrm.z = 0.0f;
    p[2].d = pos.x + testRadius;
    p[3].nrm.x = -1.0f;
    p[3].nrm.y = 0.0f;
    p[3].nrm.z = 0.0f;
    p[3].d = -(pos.x - testRadius);
    GEOS::VERTEX vrt;
    vrt.x = pos.x;
    vrt.y = 0.0f;
    vrt.z = pos.z;
    return islands[islIndex].area->geo->Clip(p, 4, vrt, 1000.0f, nullptr);
}

WdmIslandWaves::WdmIslandWaves()
{
    phase = rand() * (1.0f / RAND_MAX);
}

void WdmIslandWaves::Update(float dltTime)
{
    phase += dltTime * 0.1f;
    while (phase > 1.0f)
        phase -= 1.0f;
}

void WdmIslandWaves::LRender(VRenderer *rs)
{
    const float fullPeriod = 0.8f;
    float k = phase * (1.0f / fullPeriod);
    if (k <= 1.0f)
    {
        Render(rs, k);
    }
    if (phase >= 0.5f)
    {
        k = phase - 0.5f;
    }
    else
    {
        k = phase + 0.5f;
    }
    k *= (1.0f / fullPeriod);
    if (k <= 1.0f)
    {
        Render(rs, k);
    }
}

void WdmIslandWaves::Render(VRenderer *rs, float k)
{
    // Setting the matrix for the texture
    // k = 0.5f + 0.5f * sinf (k * 2.0f * PI);
    glm::mat4 mtx;
    mtx[1][1] = 0.3f + 1.0f * sinf(k * PI);
    mtx[2][1] = 0.0f; // 0.2f*sinf((k - 0.25f)*2.0f*PI) - 0.5f;
    rs->SetTransform(D3DTS_TEXTURE0, storm::math::d3dMatrix(mtx));
    // set the overall transparency
    const float a = sinf(k * PI) * (1.0f - k * 0.5f) * 1.25f;
    // Draw the model
    SetTech("WdmIslandWaves", nullptr);
    rs->SetRenderState(D3DRS_TEXTUREFACTOR, (static_cast<int32_t>(a * a * a * a * 255.0f) << 24) | 0xffffff);
    WdmRenderModel::LRender(rs);
}
