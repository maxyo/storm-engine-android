//============================================================================================
//    Spirenkov Maxim, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    Location
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#include "location.h"

#include <chrono>
#include <string>
#include <optional>
#include <memory>
#include <cstring>
#include <algorithm>

#include "core.h"
#include "v_file_service.h"
#include "character.h"
#include "grass.h"
#include "lights.h"

#include "shared/messages.h"
#include "storm/render/effects.hpp"
#include "string_compare.hpp"

float fCausticScale, fCausticDelta, fFogDensity, fCausticDistance;
glm::vec4 v4CausticColor;
bool bCausticEnable = false;
float fCausticFrame = 0.0f;
int32_t iCausticTex[32];
float fCausticSpeed = 0.0f;

// ============================================================================================
// Construction, destruction
// ============================================================================================

Location::Location()
{
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::system_clock;

    numLocators = 0;
    maxLocators = 16;
    locators.resize(maxLocators);
    patchJump = -1;
    isDebugView = true;
    sphereVertex = nullptr;
    sphereNumTrgs = 0;
    lastLoadStaticModel = -1;
    srand(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() | 1);
    isPause = false;
    lights = nullptr;
    curMessage = 0;
    for (int32_t i = 0; i < sizeof(message) / sizeof(DmgMessage); i++)
        message[i].alpha = 0.0f;
    locationTimeUpdate = 0.0f;
    enemyBarsCount = 0;
    bDrawBars = true;
    bSwimming = true;
    bCausticEnable = false;
}

Location::~Location()
{
    if (aoGeometryReported_)
        storm::render::SetEffectsAoGeometry({}, {}, {}); // the next location reports its own
    if (groundLightReported_)
        storm::render::SetEffectsGroundLight({}, {}, {}, {}, {}, {});
    if (!AttributesPointer)
        return;
    auto *const atr = AttributesPointer->FindAClass(AttributesPointer, "locators");
    if (atr)
        AttributesPointer->DeleteAttributeClassX(atr);
    // core.EraseEntity(cubeShotMaker);
    core.EraseEntity(lighter);
    core.EraseEntity(lizards);
    core.EraseEntity(rats);
    core.EraseEntity(crabs);
    core.EraseEntity(eagle);
    core.EraseEntity(grass);
    core.EraseEntity(lightsid);
    core.EraseEntity(loceffectsid);
    core.EraseEntity(blood);

    for (int32_t i = 0; i < numLocators; i++)
        delete locators[i];
    delete sphereVertex;
}

void Location::ReportAoGeometry()
{
    aoGeometryReported_ = true;
    const char *id = AttributesPointer ? AttributesPointer->GetAttribute("id") : nullptr;
    const char *weather = AttributesPointer ? AttributesPointer->GetAttribute("lockWeather") : nullptr;
    std::vector<std::array<float, 3>> positions;
    std::vector<uint32_t> indices;
    std::array<float, 3> lo{}, hi{};
    // Interiors (the game's own marker: lockWeather "Inside"). Their occluders are the still models one can collide
    // with: walls, floors, furniture. Models without collision are what lies outside the room (the backdrop behind
    // the windows, hundreds of metres wide) or glass; animated and moving models do not stay where they were baked.
    if (weather && storm::iEquals(weather, "Inside") && ptc.numTriangles == 0)
        core.Trace("ERROR: Location %s: an interior without a walk patch: no ambient occlusion", id ? id : "?");
    else if (weather && storm::iEquals(weather, "Inside"))
    {
        // The volume covers where one walks (the characters' patch) with room for the walls, furniture and ceiling.
        lo = {ptc.min.x - 2.0f, ptc.min.y - 0.5f, ptc.min.z - 2.0f};
        hi = {ptc.max.x + 2.0f, ptc.max.y + 4.0f, ptc.max.z + 2.0f};
        std::vector<glm::vec3> local;
        std::vector<uint32_t> local_indices;
        for (int32_t i = 0; i < model.Models(); ++i)
        {
            MODEL *m = model[i];
            if (!m || !model.IsStill(i))
                continue;
            for (int32_t n = 0;; ++n)
            {
                NODE *node = m->GetNode(n);
                if (!node)
                    break;
                if (!node->geo || !node->geo->HasCollision())
                    continue;
                local.clear();
                local_indices.clear();
                if (!node->geo->AppendTriangles(local, local_indices))
                {
                    core.Trace("ERROR: Location %s: the buffers of model %s cannot be read: it is missing from the "
                               "ambient occlusion",
                               id ? id : "?", model.GetModelName(i));
                    continue;
                }
                const auto base = static_cast<uint32_t>(positions.size());
                for (const glm::vec3 &p : local)
                {
                    const glm::vec3 w = storm::math::transformPoint(node->glob_mtx, p);
                    positions.push_back({w.x, w.y, w.z});
                }
                for (const uint32_t index : local_indices)
                    indices.push_back(base + index);
            }
        }
    }
    storm::render::SetEffectsAoGeometry(id ? id : "", positions, indices, lo, hi);
}

namespace
{

// One mip level of a .tx lightmap, the first not wider than kMaxWidth (a texel of a 2048² town lightmap is a few
// centimetres; the ground lookup averages over 0.3 m anyway).
std::optional<storm::render::EffectsLightmap> ReadLightmap(const std::string &file)
{
    constexpr uint32_t kMaxWidth = 512;
    char *data = nullptr;
    uint32_t size = 0;
    if (!fio->LoadFile(file.c_str(), &data, &size))
        return std::nullopt;
    std::unique_ptr<char[]> owned(data);
    struct Header
    {
        int32_t flags, width, height, nmips;
        char format[4];
        int32_t mip_size;
    } head{};
    if (size < sizeof(head))
        return std::nullopt;
    std::memcpy(&head, data, sizeof(head));
    const bool dxt = head.format[0] == 'D' && head.format[1] == 'X' && head.format[2] == 'T' &&
                     (head.format[3] == '1' || head.format[3] == '3' || head.format[3] == '5');
    if (!dxt || head.flags != 0 || head.width <= 0 || head.height <= 0 || head.nmips <= 0)
    {
        core.Trace("ERROR: lightmap %s: format %.4s, flags %d: only plain DXT1/3/5 lightmaps light the characters", file.c_str(),
                   head.format, head.flags);
        return std::nullopt;
    }
    const uint32_t block = head.format[3] == '1' ? 8 : 16;
    size_t offset = sizeof(head);
    uint32_t w = static_cast<uint32_t>(head.width), h = static_cast<uint32_t>(head.height);
    for (int32_t m = 1; m < head.nmips && w > kMaxWidth && w > 4 && h > 4; ++m)
    {
        offset += static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * block;
        w /= 2;
        h /= 2;
    }
    const size_t level = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * block;
    if (offset + level > size)
    {
        core.Trace("ERROR: lightmap %s is shorter than its %dx%d %d-level header says", file.c_str(), head.width,
                   head.height, head.nmips);
        return std::nullopt;
    }
    storm::render::EffectsLightmap map;
    map.width = w;
    map.height = h;
    std::memcpy(map.format.data(), head.format, 4);
    map.data.assign(reinterpret_cast<const uint8_t *>(data) + offset, reinterpret_cast<const uint8_t *>(data) + offset + level);
    return map;
}

} // namespace

void Location::ReportGroundLight()
{
    groundLightReported_ = true;
    const char *id = AttributesPointer ? AttributesPointer->GetAttribute("id") : nullptr;
    const char *weather = AttributesPointer ? AttributesPointer->GetAttribute("lockWeather") : nullptr;
    if (weather && storm::iEquals(weather, "Inside"))
        return; // no sun indoors
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> uvs;
    std::vector<uint32_t> indices, triangle_lightmap;
    std::vector<storm::render::EffectsLightmap> lightmaps;
    std::vector<std::string> files;
    std::vector<glm::vec3> local;
    std::vector<glm::vec2> local_uvs;
    std::vector<uint32_t> local_indices;
    for (int32_t i = 0; i < model.Models(); ++i)
    {
        MODEL *m = model[i];
        if (!m || !model.IsStill(i))
            continue;
        for (int32_t n = 0;; ++n)
        {
            NODE *node = m->GetNode(n);
            if (!node)
                break;
            if (!node->geo || node->geo->LightmapFile().empty())
                continue;
            const std::string &file = node->geo->LightmapFile();
            auto known = std::find(files.begin(), files.end(), file);
            uint32_t lightmap = static_cast<uint32_t>(known - files.begin());
            if (known == files.end())
            {
                auto map = ReadLightmap(file);
                if (!map)
                {
                    core.Trace("ERROR: Location %s: lightmap %s of model %s cannot be read: characters on it keep the "
                               "full sun", id ? id : "?", file.c_str(), model.GetModelName(i));
                    continue;
                }
                files.push_back(file);
                lightmaps.push_back(std::move(*map));
            }
            local.clear();
            local_uvs.clear();
            local_indices.clear();
            if (!node->geo->AppendLightmapTriangles(local, local_uvs, local_indices))
            {
                core.Trace("ERROR: Location %s: the buffers of model %s cannot be read: characters on it keep the full sun",
                           id ? id : "?", model.GetModelName(i));
                continue;
            }
            const auto base = static_cast<uint32_t>(positions.size());
            for (size_t v = 0; v < local.size(); ++v)
            {
                const glm::vec3 w = storm::math::transformPoint(node->glob_mtx, local[v]);
                positions.push_back({w.x, w.y, w.z});
                uvs.push_back({local_uvs[v].x, local_uvs[v].y});
            }
            for (const uint32_t index : local_indices)
                indices.push_back(base + index);
            triangle_lightmap.insert(triangle_lightmap.end(), local_indices.size() / 3, lightmap);
        }
    }
    storm::render::SetEffectsGroundLight(id ? id : "", positions, uvs, indices, triangle_lightmap, lightmaps);
}

// Initialization
bool Location::Init()
{
    // renderer
    rs = static_cast<VRenderer *>(core.GetService("renderer"));
    if (!rs)
        throw std::runtime_error("No service: renderer");
    rs->SetRenderState(D3DRS_LIGHTING, FALSE);

    // core.LayerCreate("execute", true, false);
    core.SetLayerType(EXECUTE, layer_type_t::execute);
    core.AddToLayer(EXECUTE, GetId(), 10);

    // core.LayerCreate("realize", true, false);
    core.SetLayerType(REALIZE, layer_type_t::realize);
    core.AddToLayer(REALIZE, GetId(), 100000);

    lightsid = core.CreateEntity("Lights");
    loceffectsid = core.CreateEntity("LocationEffects");

    enemyBarsTexture = rs->TextureCreate("LocEfx\\state_bars.tga");

    lighter = core.CreateEntity("Lighter");
    // cubeShotMaker = core.CreateEntity("CubeShotMakerCam");
    return true;
}

// Execution
void Location::Execute(uint32_t delta_time)
{
    bSwimming = AttributesPointer->GetAttributeAsDword("swimming", 1) != 0;

    // Updating characters
    if (!isDebugView)
        Update(delta_time);
    // Updating messages
    const auto dltTime = delta_time * 0.001f;
    for (int32_t i = 0; i < sizeof(message) / sizeof(DmgMessage); i++)
    {
        if (message[i].alpha <= 0.0f)
            continue;
        message[i].p.y += dltTime * 0.2f;
        message[i].alpha -= dltTime * 0.4f;
    }
    // Updating data for grass
    auto *grs = static_cast<Grass *>(core.GetEntityPointer(grass));
    if (grs)
    {
        grs->characters.resize(supervisor.character.size());
        for (size_t i = 0; i < supervisor.character.size(); i++)
        {
            supervisor.character[i].c->GetGrassPosition(grs->characters[i].pos, grs->characters[i].lastPos);
            grs->characters[i].chr = supervisor.character[i].c;
            grs->characters[i].useCounter = 0;
        }
    }
    // Location update
    locationTimeUpdate += dltTime;
    if (locationTimeUpdate > 1.0f)
    {
        core.Event("LocationTimeUpdate", "f", locationTimeUpdate);
        locationTimeUpdate = 0.0f;
    }
}

void Location::Realize(uint32_t delta_time)
{
    // The first frame: every model of the location (always, day/night, back) is loaded by now.
    if (!aoGeometryReported_)
        ReportAoGeometry();
    if (!groundLightReported_)
        ReportGroundLight();
    const auto fDeltaTime = static_cast<float>(delta_time) * 0.001f;

    fCausticFrame += fDeltaTime * fCausticSpeed;

    while (fCausticFrame >= 32.0f)
        fCausticFrame -= 32.0f;

    // Rendering locators
    for (int32_t i = 0; i < numLocators; i++)
        if (locators[i]->isVisible)
            DrawLocators(locators[i]);
    if (IsDebugView())
    {
        // Patch rendering
        ptc.DebugDraw(rs, delta_time * 0.001f);
        // Location information
        const char *c = nullptr;
        if (AttributesPointer)
        {
            c = AttributesPointer->GetAttribute("id");
        }
        if (!c)
            c = "ID not found";
        rs->Print(10, 10, "Location ID: '%s'", c);
        if (IsExDebugView())
        {
            for (int32_t i = 0; i < model.Models(); i++)
            {
                rs->Print(10, 40 + i * 26, "%2i mdl: '%s.gm'", i, model.GetModelName(i));
            }
        }
    }
    // Updating characters
    if (isDebugView)
        Update(delta_time);
    // Rendering messages
    auto i = curMessage;
    for (int32_t c = 0; c < sizeof(message) / sizeof(DmgMessage); c++, i--)
    {
        if (i < 0)
            i = sizeof(message) / sizeof(DmgMessage) - 1;
        if (message[i].alpha <= 0.0f)
            continue;
        // Print(message[i].p, 10.0f, 0, message[i].alpha, message[i].c, 0.8f, "%.0f/%.0f", message[i].hit,
        // message[i].hp);
        Print(message[i].p, 10.0f, 0, message[i].alpha, message[i].c, 0.8f, "%.0f", message[i].hit);
    }
    // Drawing bars over characters
    if (bDrawBars)
    {
        DrawEnemyBars();
    }
    enemyBarsCount = 0;
}

void Location::Update(uint32_t delta_time)
{
    lights = static_cast<Lights *>(core.GetEntityPointer(lightsid));

    const uint32_t max_delta_time = 500;
    const auto maxDltTime = 0.1f;
    if (delta_time > max_delta_time)
        delta_time = max_delta_time;
    auto dltTime = delta_time * 0.001f;
    // Model effects
    model.Update(dltTime);
    // Characters
    if (!isPause)
    {
        supervisor.PreUpdate(dltTime);
        for (; dltTime > maxDltTime; dltTime -= maxDltTime)
            supervisor.Update(maxDltTime);
        if (dltTime > 0.0f)
            supervisor.Update(dltTime);
        supervisor.PostUpdate(dltTime);
    }
    else
        supervisor.Update(0.0f);
}

// Messages
uint64_t Location::ProcessMessage(MESSAGE &message)
{
    int32_t i;
    float u0, v0, u1, v1;
    float ax, ay, az;
    int32_t level;
    LocatorArray *la;
    switch (message.Long())
    {
    case MSG_LOCATION_ADD_MODEL: {
        const std::string &name = message.String();
        const std::string &tech = message.String();
        level = message.Long();

        // Dynamic lights are optional
        const auto dynamicLightsOn = message.GetFormat().size() > 4 ? message.Long() : 0;

        lastLoadStaticModel = LoadStaticModel(name.c_str(), tech.c_str(), level, dynamicLightsOn == 1);
        return lastLoadStaticModel >= 0;
    }
    case MSG_LOCATION_GET_MODEL:
        if (lastLoadStaticModel < 0)
            return 0;
        if (!model.IsValidateIndex(lastLoadStaticModel))
            return 0;
        if (!core.GetEntityPointer(model.ID(lastLoadStaticModel)))
            return 0;
        message.ScriptVariablePointer()->Set(model.ID(lastLoadStaticModel));
        return 1;
    case MSG_LOCATION_MODEL_SET_POS: {
        if (lastLoadStaticModel < 0)
            return 0;
        if (!model.IsValidateIndex(lastLoadStaticModel))
            return 0;
        if (!model[lastLoadStaticModel])
            return 0;
        // Looking for a group
        const std::string &name = message.String();
        la = FindLocatorsGroup(name.c_str());
        if (!la)
            return 0;
        // Looking for a locator
        const std::string &name2 = message.String();
        i = la->FindByName(name2.c_str());
        if (i < 0)
            return 0;
        la->GetLocatorPos(i, model[lastLoadStaticModel]->mtx);
        return 1;
    }
    case MSG_LOCATION_MODEL_SET_ROT:
        if (lastLoadStaticModel < 0)
            return 0;
        u0 = message.Float(); // rx
        v0 = message.Float(); // ry
        u1 = message.Float(); // rz
        model.SetRotation(lastLoadStaticModel, u0, v0, u1);
        return 1;
    case MSG_LOCATION_MODEL_SET_ROT_ANGLE:
        if (lastLoadStaticModel < 0)
            return 0;
        ax = message.Float(); // ax
        ay = message.Float(); // ay
        az = message.Float(); // az
        model.SetRotationAngle(lastLoadStaticModel, ax, ay, az);
        return 1;
    case MSG_LOCATION_MODEL_SET_UVS:
        if (lastLoadStaticModel < 0)
            return 0;
        u0 = message.Float();
        v0 = message.Float();
        u1 = message.Float();
        v1 = message.Float();
        model.SetUVSlide(lastLoadStaticModel, u0, v0, u1, v1);
        return 1;
    case MSG_LOCATION_MODEL_LAMPS:
        if (lastLoadStaticModel < 0)
            return 0;
        lights = static_cast<Lights *>(core.GetEntityPointer(lightsid));
        if (!lights)
            return 0;
        return lights->AddLampModel(model.ID(lastLoadStaticModel));
    case MSG_LOCATION_MODEL_REFLECTION:
        if (lastLoadStaticModel < 0)
            return 0;
        model.SetReflection(lastLoadStaticModel, message.Float());
        return 1;
    case MSG_LOCATION_SET_CHRS_PATCH: {
        const std::string &name = message.String();
        return LoadCharacterPatch(name.c_str());
    }
    case MSG_LOCATION_SET_JMP_PATCH: {
        const std::string &name = message.String();
        return LoadJumpPatch(name.c_str());
    }
    case MSG_LOCATION_SET_GRS_PATCH: {
        const std::string &name = message.String();
        const std::string &tech = message.String();
        return LoadGrass(name.c_str(), tech.c_str());
    }
    case MSG_LOCATION_UPDATELOCATORS:
        UpdateLocators();
        return 1;
    case MSG_LOCATION_MODELSPATH:
        model.modelspath = message.String();
        model.UpdateModelsPath();

        core.Send_Message(lighter, "ss", "ModelsPath", model.modelspath.c_str());

        return 1;
    case MSG_LOCATION_TEXTURESPATH:
        model.texturespath = message.String();
        model.UpdateTexturesPath();
        return 1;
    case MSG_LOCATION_LIGHTPATH:
        model.lightpath = message.String();
        model.UpdateLightPath();
        core.Send_Message(lighter, "ss", "LightPath", model.lightpath.c_str());

        return 1;
    case MSG_LOCATION_SHADOWPATH:
        model.shadowpath = message.String();
        model.UpdateShadowPath();
        return 1;
    case MSG_LOCATION_PAUSE:
        isPause = message.Long() != 0;
        return 1;
    case MSG_LOCATION_VIEWLGROUP: {
        const std::string &name = message.String();
        la = FindLocatorsGroup(name.c_str());
        if (!la)
            return 0;
        la->isVisible = true;
        la->kViewRadius = message.Float();
        la->viewDist = message.Float();
        la->color = message.Long();
        isDebugView = true;
        return 1;
    }
    case MSG_LOCATION_HIDELGROUP: {
        const std::string &name = message.String();
        la = FindLocatorsGroup(name.c_str());
        if (!la)
            return 0;
        la->isVisible = false;
        la->kViewRadius = 0.0f;
        isDebugView = false;
        for (i = 0; i < numLocators; i++)
        {
            if (locators[i]->isVisible)
            {
                isDebugView = true;
                break;
            }
        }
        return 1;
    }
    case MSG_LOCATION_GRP_RADIUS: {
        const std::string &name = message.String();
        la = FindLocatorsGroup(name.c_str());
        if (!la)
            return 0;
        la->radius = message.Float();
        return 1;
    }
    case MSG_LOCATION_LOC_RADIUS: {
        const std::string &name = message.String();
        la = FindLocatorsGroup(name.c_str());
        if (!la)
            return 0;
        const std::string &name2 = message.String();
        i = la->FindByName(name2.c_str());
        if (i < 0)
            return 0;
        la->SetLocatorRadius(i, message.Float());
        return 1;
    }
    case MSG_LOCATION_LOC_EXISTS: {
        const std::string &name = message.String();
        if (CheckIfLocatorExists(name.c_str()))
            return 1;
        return 0;
    }
    case MSG_LOCATION_CHECKENTRY:
        u0 = message.Float(); // x
        v0 = message.Float(); // y
        u1 = message.Float(); // z
        return supervisor.CheckPosition(u0, v0, u1, nullptr);
    case MSG_LOCATION_SETCHRPOSITIONS:
        supervisor.SetSavePositions();
        break;
    case MSG_LOCATION_TLPCHRPOSITIONS:
        supervisor.DelSavePositions(true);
        break;
    case MSG_LOCATION_CLRCHRPOSITIONS:
        supervisor.DelSavePositions(false);
        break;
    case MSG_LOCATION_ADD_LIGHT: {
        lights = static_cast<Lights *>(core.GetEntityPointer(lightsid));
        if (!lights)
            return false;
        const std::string &name = message.String();
        i = lights->FindLight(name.c_str());
        if (i < 0)
            return false;
        u0 = message.Float();
        v0 = message.Float();
        u1 = message.Float();
        lights->AddLight(i, glm::vec3(u0, v0, u1));
        return true;
    }
    case MSG_LOCATION_EX_MSG: {
        const std::string &name = message.String();
        return MessageEx(name.c_str(), message);
    }
    case MSG_LOCATION_VIEWSTATEBARS:
        bDrawBars = message.Long() != 0;
        return 1;
    }
    return 0;
}

// Find locator group
LocatorArray *Location::FindLocatorsGroup(const char *gName)
{
    if (!gName || !gName[0])
        return nullptr;
    const auto hash = LocatorArray::CalcHashString(gName);
    for (int32_t i = 0; i < numLocators; i++)
    {
        if (locators[i]->CompareGroup(gName, hash))
            return locators[i];
    }
    return nullptr;
}

bool Location::CheckIfLocatorExists(const char *lName)
{
    for (int32_t i = 0; i < numLocators; i++)
    {
        if (locators[i]->FindByName(lName) != -1)
        {
            return true;
        }
    }

    return false;
}

int32_t Location::LoadStaticModel(const char *modelName, const char *tech, int32_t level, bool useDynamicLights)
{
    lights = static_cast<Lights *>(core.GetEntityPointer(lightsid));
    const auto im = model.CreateModel(modelName, tech, level, true, useDynamicLights ? GetLights() : nullptr);
    if (im < 0)
        return -1;
    // Pointer to geometry
    auto *mdl = model[im];
    if (!mdl)
    {
        model.DeleteModel(im);
        return -1;
    }
    auto *const node = mdl->GetNode(0);
    if (!node)
    {
        model.DeleteModel(im);
        return -1;
    }
    auto *const g = node->geo;
    if (!g)
    {
        model.DeleteModel(im);
        return -1;
    }
    // Add the model to special layers
    core.AddToLayer(SHADOW, mdl->GetId(), 10);
    core.AddToLayer(SUN_TRACE, mdl->GetId(), 10);
    core.AddToLayer(BLOOD, mdl->GetId(), 100);
    core.AddToLayer(RAIN_DROPS, mdl->GetId(), 100);
    // Reading out all locators
    GEOS::INFO ginfo;
    GEOS::LABEL label;
    g->GetInfo(ginfo);
    for (int32_t i = 0; i < ginfo.nlabels; i++)
    {
        g->GetLabel(i, label);
        if (!label.group_name || !label.group_name[0])
            continue;
        const auto hash = LocatorArray::CalcHashString(label.group_name);
        int32_t j;
        for (j = 0; j < numLocators; j++)
        {
            if (locators[j]->CompareGroup(label.group_name, hash))
                break;
        }
        if (j == numLocators)
        {
            if (numLocators == maxLocators)
            {
                maxLocators += 16;
                locators.resize(maxLocators);
            }
            numLocators++;
            locators[j] = new LocatorArray(label.group_name);
        }
        const auto locIndex = locators[j]->FindByName(label.name);
        if (locIndex < 0)
        {
            auto &mtxx = *((glm::mat4 *)label.m);
            for (int32_t me = 0; me < 16; me++)
                if (std::isnan(storm::math::data(mtxx)[me]))
                {
                    core.Trace("Location: locator %s::%s in position have NaN value, reset it!", label.group_name,
                               label.name);
                    mtxx = glm::mat4(1.0f);
                    break;
                }
            locators[j]->AddLocator(mtxx, label.name);
        }
        else
        {
            locators[j]->SetNewMatrix(locIndex, *((glm::mat4 *)label.m));
        }
    }

    core.Send_Message(lighter, "ssi", "AddModel", modelName, mdl->GetId());

    return im;
}

bool Location::LoadCharacterPatch(const char *ptcName)
{
    // Form the path to the file
    char path[512];
    strcpy_s(path, "resource\\models\\");
    strcat_s(path, model.modelspath.c_str());
    strcat_s(path, ptcName);
    strcat_s(path, ".ptc");
    // load the patch
    const auto result = ptc.Load(path);
    if (!result)
        core.Trace("Can't loaded patch data file %s.ptc for npc.", ptcName);
    return result;
}

bool Location::LoadJumpPatch(const char *modelName)
{
    if (patchJump >= 0)
        model.DeleteModel(patchJump);
    patchJump = -1;
    if (!modelName || !modelName[0])
        return false;
    patchJump = model.CreateModel(modelName, "", 0, false, nullptr);
    return patchJump >= 0;
}

bool Location::LoadGrass(const char *modelName, const char *texture)
{
    core.EraseEntity(grass);
    if (!modelName || !modelName[0])
        return true;
    grass = core.CreateEntity("Grass");
    auto *grs = static_cast<Grass *>(core.GetEntityPointer(grass));
    if (!grs)
        return false;
    if (texture && texture[0])
        grs->SetTexture(texture);
    char nm[512];
    strcpy_s(nm, "resource\\models\\");
    strcat_s(nm, model.modelspath.c_str());
    strcat_s(nm, modelName);
    strcat_s(nm, ".grs");
    int32_t ll = strlen(nm);
    if (grs->LoadData(nm))
        return true;
    core.Trace("Can't load grass data file: %s", nm);
    core.EraseEntity(grass);
    return false;
}

bool Location::MessageEx(const char *name, MESSAGE &message)
{
    if (storm::iEquals(name, "DelAllLights"))
    {
        lights->DelAllLights();
    }
    else if (storm::iEquals(name, "AddFlys"))
    {
        const auto effects = core.GetEntityId("LocationEffects");
        const auto x = message.Float();
        const auto y = message.Float();
        const auto z = message.Float();
        core.Send_Message(effects, "sfff", "AddFly", x, y, z);
        return true;
    }
    else if (storm::iEquals(name, "DelFlys"))
    {
        const auto effects = core.GetEntityId("LocationEffects");
        core.Send_Message(effects, "s", "DelFlys");
        return true;
    }
    else if (storm::iEquals(name, "GetPatchMiddlePos"))
    {
        VDATA *vx = message.ScriptVariablePointer();
        if (!vx)
            return false;
        VDATA *vy = message.ScriptVariablePointer();
        if (!vy)
            return false;
        VDATA *vz = message.ScriptVariablePointer();
        if (!vz)
            return false;
        vx->Set(ptc.middle.x);
        vy->Set(ptc.middle.y);
        vz->Set(ptc.middle.z);
        return true;
    }
    else if (storm::iEquals(name, "AddEagle"))
    {
        eagle = core.CreateEntity("LocEagle");
        return true;
    }
    else if (storm::iEquals(name, "AddLizards"))
    {
        lizards = core.CreateEntity("Lizards");
        return true;
    }
    else if (storm::iEquals(name, "AddRats"))
    {
        rats = core.CreateEntity("LocRats");
        if (!core.Send_Message(rats, "l", message.Long()))
        {
            core.EraseEntity(rats);
            return false;
        }
        return true;
    }
    else if (storm::iEquals(name, "AddCrabs"))
    {
        crabs = core.CreateEntity("LocCrabs");
        if (!core.Send_Message(crabs, "l", message.Long()))
        {
            core.EraseEntity(crabs);
            return false;
        }

        return true;
    }
    else if (storm::iEquals(name, "AddBlood"))
    {
        if (!core.GetEntityPointer(blood))
        {
            blood = core.CreateEntity("Blood");
            core.AddToLayer(EXECUTE, blood, 65540);
            core.AddToLayer(REALIZE, blood, 65540);
        }
        glm::vec3 vPos;
        vPos.x = message.Float();
        vPos.y = message.Float();
        vPos.z = message.Float();
        core.Send_Message(blood, "lfff", 2, vPos.x, vPos.y, vPos.z);
        return true;
    }
    else if (storm::iEquals(name, "TestLocatorsGroup"))
    {
        TestLocatorsInPatch(message);
        return true;
    }
    else if (storm::iEquals(name, "DeleteLocationModel"))
    {
        const std::string &modelname = message.String();
        const int32_t n = model.FindModel(modelname.c_str());
        if (n >= 0)
            model.DeleteModel(n);
        return true;
    }
    else if (storm::iEquals(name, "HideLocationModel"))
    {
        const std::string &modelname = message.String();
        const int32_t n = model.FindModel(modelname.c_str());
        if (n >= 0)
            // core.LayerDel("realize", model.RealizerID(n));
            core.Send_Message(model.RealizerID(n), "ll", 2, 0);
    }
    else if (storm::iEquals(name, "ShowLocationModel"))
    {
        const std::string &modelname = message.String();
        int32_t layer = message.Long();
        const int32_t n = model.FindModel(modelname.c_str());
        if (n >= 0)
            // core.AddToLayer(realize, model.RealizerID(n), layer);
            core.Send_Message(model.RealizerID(n), "ll", 2, 1);
    }
    else if (storm::iEquals(name, "SetGrassParams"))
    {
        const float fScale = message.Float();
        const float fMaxWidth = message.Float();
        const float fMaxHeight = message.Float();
        const float fMinVisibleDist = message.Float();
        const float fMaxVisibleDist = message.Float();
        const float fMinGrassLod = message.Float();
        core.Send_Message(grass, "lffffff", MSG_GRASS_SET_PARAM, fScale, fMaxWidth, fMaxHeight, fMinVisibleDist,
                          fMaxVisibleDist, fMinGrassLod);
    }
    else if (storm::iEquals(name, "LoadCaustic"))
    {
        LoadCaustic();
    }
    else if (storm::iEquals(name, "EnableCaustic"))
    {
        bCausticEnable = message.Long() != 0;
    }
    return false;
}

void Location::UpdateLocators()
{
    glm::mat4 mtx;
    if (!AttributesPointer)
        return;
    ATTRIBUTES *atr = AttributesPointer->FindAClass(AttributesPointer, "locators");
    if (atr)
        AttributesPointer->DeleteAttributeClassX(atr);
    AttributesPointer->CreateSubAClass(AttributesPointer, "locators");
    atr = AttributesPointer->FindAClass(AttributesPointer, "locators");
    if (atr)
    {
        // Creating new attributes
        for (int32_t i = 0; i < numLocators; i++)
        {
            char *groupName = locators[i]->GetGroupName();
            atr->CreateSubAClass(atr, groupName);
            ATTRIBUTES *at = atr->FindAClass(atr, groupName);
            if (at)
            {
                for (int32_t j = 0; j < locators[i]->Num(); j++)
                {
                    at->CreateSubAClass(at, locators[i]->Name(j));
                    ATTRIBUTES *a = at->FindAClass(at, locators[i]->Name(j));
                    if (a)
                    {
                        // Radius
                        a->CreateSubAClass(a, "radius");
                        a->SetAttributeUseFloat("radius", locators[i]->GetLocatorRadius(j));
                        // Matrix
                        locators[i]->GetLocatorPos(j, mtx);
                        // Pos
                        a->SetAttributeUseFloat("x", storm::math::pos(mtx).x);
                        a->SetAttributeUseFloat("y", storm::math::pos(mtx).y);
                        a->SetAttributeUseFloat("z", storm::math::pos(mtx).z);
                        // vz
                        a->CreateSubAClass(a, "vz");
                        ATTRIBUTES *v = a->FindAClass(a, "vz");
                        if (v)
                        {
                            v->SetAttributeUseFloat("x", storm::math::vz(mtx).x);
                            v->SetAttributeUseFloat("y", storm::math::vz(mtx).y);
                            v->SetAttributeUseFloat("z", storm::math::vz(mtx).z);
                        }
                        else
                        {
                            core.Trace("Location: Can't create attribute 'locators.%s.%s.vz'!", groupName,
                                       locators[i]->Name(j));
                        }
                        // vy
                        a->CreateSubAClass(a, "vy");
                        v = a->FindAClass(a, "vy");
                        if (v)
                        {
                            v->SetAttributeUseFloat("x", storm::math::vy(mtx).x);
                            v->SetAttributeUseFloat("y", storm::math::vy(mtx).y);
                            v->SetAttributeUseFloat("z", storm::math::vy(mtx).z);
                        }
                        else
                        {
                            core.Trace("Location: Can't create attribute 'locators.%s.%s.vy'!", groupName,
                                       locators[i]->Name(j));
                        }
                        // vx
                        a->CreateSubAClass(a, "vx");
                        v = a->FindAClass(a, "vx");
                        if (v)
                        {
                            v->SetAttributeUseFloat("x", storm::math::vx(mtx).x);
                            v->SetAttributeUseFloat("y", storm::math::vx(mtx).y);
                            v->SetAttributeUseFloat("z", storm::math::vx(mtx).z);
                        }
                        else
                        {
                            core.Trace("Location: Can't create attribute 'locators.%s.%s.vx'!", groupName,
                                       locators[i]->Name(j));
                        }
                    }
                    else
                    {
                        core.Trace("Location: Can't create attribute 'locators.%s.%s'!", groupName,
                                   locators[i]->Name(j));
                    }
                }
            }
            else
            {
                core.Trace("Location: Can't create attribute 'locators.%s'!", groupName);
            }
        }
    }
    else
    {
        core.Trace("Location: Can't create attribute 'locators'!");
    }
}

void Location::DrawLocators(LocatorArray *la)
{
    if (la->kViewRadius > 0.0f)
    {
        // States
        if (!sphereNumTrgs)
            CreateSphere();
        glm::mat4 mPos;
        // Remove textures
        rs->TextureSet(0, -1);
        rs->TextureSet(1, -1);
        // start the technique
        const bool isSet = rs->TechniqueExecuteStart("DbgDrawLocators");
        rs->SetRenderState(D3DRS_TEXTUREFACTOR, la->color);
        // Draw
        for (int32_t i = 0; i < la->Num(); i++)
        {
            // Draw a ball
            la->GetLocatorPos(i, mPos);
            const float rad = la->GetLocatorRadius(i) * la->kViewRadius;
            if (rad <= 0.0f)
                continue;
            mPos[0][0] *= rad;
            mPos[0][1] *= rad;
            mPos[0][2] *= rad;
            mPos[1][0] *= rad;
            mPos[1][1] *= rad;
            mPos[1][2] *= rad;
            mPos[2][0] *= rad;
            mPos[2][1] *= rad;
            mPos[2][2] *= rad;
            rs->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(mPos));
            rs->DrawPrimitiveUP(D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_DIFFUSE, sphereNumTrgs, sphereVertex,
                                sizeof(SphVertex), nullptr);
        }
        if (isSet)
            while (rs->TechniqueExecuteNext())
            {
            }
    }
    // sign
    static glm::mat4 mtx, view, prj;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
    rs->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prj));
    mtx = prj * view;
    // Get the current vp sizes
    static D3DVIEWPORT9 vp;
    rs->GetViewport(&vp);
    const float w = vp.Width * 0.5f;
    const float h = vp.Height * 0.5f;
    glm::vec3 lvrt;
    storm::math::ProjectedPoint vrt;
    const int32_t fh = rs->CharHeight(FONT_DEFAULT);
    const int32_t gw = rs->StringWidth(la->GetGroupName()) / 2;
    storm::math::invertRigid(view);
    const float d = glm::dot(storm::math::vz(view), storm::math::pos(view));
    const float viewDst = la->viewDist * la->viewDist;
    // Draw
    for (int32_t i = 0; i < la->Num(); i++)
    {
        float lbh = la->GetLocatorRadius(i) * la->kViewRadius;
        if (lbh <= 0.0f)
            continue;
        if (lbh > 1.5f)
            lbh = 1.5f;
        // writing a text
        la->GetLocatorPos(i, lvrt.x, lvrt.y, lvrt.z);
        if (glm::dot(lvrt, storm::math::vz(view)) < d)
            continue;
        if (storm::math::lengthSquared(lvrt - storm::math::pos(view)) > viewDst)
            continue;
        lvrt.y += lbh;
        storm::math::projectPoints(mtx, &lvrt, &vrt, 1, w, h, sizeof(glm::vec3), sizeof(storm::math::ProjectedPoint));
        rs->Print(static_cast<int32_t>(vrt.x - gw), static_cast<int32_t>(vrt.y - fh), la->GetGroupName());
        const int32_t lw = rs->StringWidth((char *)la->LocatorName(i)) / 2;
        rs->Print(static_cast<int32_t>(vrt.x - lw), static_cast<int32_t>(vrt.y), (char *)la->LocatorName(i));
    }
    rs->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(glm::mat4()));
}

void Location::DrawLine(const glm::vec3 &s, uint32_t cs, const glm::vec3 &d, uint32_t cd, bool useZ) const
{
    SphVertex lineVertex[2];
    lineVertex[0].v = s;
    lineVertex[0].c = cs;
    lineVertex[1].v = d;
    lineVertex[1].c = cd;
    rs->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(glm::mat4()));
    // Remove textures
    rs->TextureSet(0, -1);
    rs->TextureSet(1, -1);
    // set Z
    uint32_t oldZState = 1;
    rs->GetRenderState(D3DRS_ZENABLE, &oldZState);
    rs->SetRenderState(D3DRS_ZENABLE, useZ);
    // Draw
    rs->DrawPrimitiveUP(D3DPT_LINELIST, D3DFVF_XYZ | D3DFVF_DIFFUSE, 1, lineVertex, sizeof(SphVertex),
                        "DbgLocationDrawLine");
    // restore
    rs->SetRenderState(D3DRS_ZENABLE, oldZState);
}

void Location::CreateSphere()
{
#define CalcKColor(ind)                                                                                                \
    {                                                                                                                  \
        kColor = glm::dot(light, storm::math::normalizeOrZero(glm::vec3(sphereVertex[t * 3 + ind].v.x,                  \
                    sphereVertex[t * 3 + ind].v.y, sphereVertex[t * 3 + ind].v.z)));                                  \
        if (kColor < 0.0f)                                                                                             \
            kColor = 0.0f;                                                                                             \
    }
#define CLerp(c, min) (uint32_t(c * (kColor * (1.0f - min) + min)))
#define Color                                                                                                          \
    ((CLerp(255.0f, 0.5f) << 24) | (CLerp(255.0f, 0.7f) << 16) | (CLerp(255.0f, 0.7f) << 8) |                          \
     (CLerp(255.0f, 0.7f) << 0));

    if (sphereVertex)
        return;

    const float myPI = 3.1415926535897932f;
    const int32_t a1 = 32;
    const int32_t a2 = (a1 / 2);

    sphereNumTrgs = a1 * a2 * 2;
    sphereVertex = new SphVertex[sphereNumTrgs * 6];

    const glm::vec3 light = storm::math::normalizeOrZero(glm::vec3(0.0f, 0.0f, 1.0f));
    float kColor;
    // Filling the vertices
    int32_t t = 0;
    for (int32_t i = 0; i < a2; i++)
    {
        const float r1 = sinf(myPI * i / static_cast<float>(a2));
        const float y1 = cosf(myPI * i / static_cast<float>(a2));
        const float r2 = sinf(myPI * (i + 1) / static_cast<float>(a2));
        const float y2 = cosf(myPI * (i + 1) / static_cast<float>(a2));
        for (int32_t j = 0; j < a1; j++)
        {
            const float x1 = sinf(2.0f * myPI * j / static_cast<float>(a1));
            const float z1 = cosf(2.0f * myPI * j / static_cast<float>(a1));
            const float x2 = sinf(2.0f * myPI * (j + 1) / static_cast<float>(a1));
            const float z2 = cosf(2.0f * myPI * (j + 1) / static_cast<float>(a1));
            // 0
            sphereVertex[t * 3 + 0].v.x = r1 * x1;
            sphereVertex[t * 3 + 0].v.y = y1;
            sphereVertex[t * 3 + 0].v.z = r1 * z1;
            CalcKColor(0);
            sphereVertex[t * 3 + 0].c = Color;
            // 1
            sphereVertex[t * 3 + 1].v.x = r2 * x1;
            sphereVertex[t * 3 + 1].v.y = y2;
            sphereVertex[t * 3 + 1].v.z = r2 * z1;
            CalcKColor(1);
            sphereVertex[t * 3 + 1].c = Color;
            // 2
            sphereVertex[t * 3 + 2].v.x = r1 * x2;
            sphereVertex[t * 3 + 2].v.y = y1;
            sphereVertex[t * 3 + 2].v.z = r1 * z2;
            CalcKColor(2);
            sphereVertex[t * 3 + 2].c = Color;
            // 3 = 2
            sphereVertex[t * 3 + 3] = sphereVertex[t * 3 + 2];
            // 4 = 1
            sphereVertex[t * 3 + 4] = sphereVertex[t * 3 + 1];
            // 5
            sphereVertex[t * 3 + 5].v.x = r2 * x2;
            sphereVertex[t * 3 + 5].v.y = y2;
            sphereVertex[t * 3 + 5].v.z = r2 * z2;
            CalcKColor(5);
            sphereVertex[t * 3 + 5].c = Color;
            // Added 2 triangles
            t += 2;
        }
    }
    Assert(sphereNumTrgs == t);
}

bool Location::IsExDebugView()
{
    return core.Controls->GetDebugAsyncKeyState('O') < 0;
}

bool Location::IsDebugView()
{
    return core.Controls->GetDebugAsyncKeyState('G') < 0 || core.Controls->GetDebugAsyncKeyState('O') < 0;
}

// Write text
void Location::Print(const glm::vec3 &pos3D, float rad, int32_t line, float alpha, uint32_t color, float scale,
                     const char *format, ...) const
{
    static char buf[256];
    scale *= 2.0f;
    // print to the buffer
    va_list args;
    va_start(args, format);
    int32_t len = vsnprintf(buf, sizeof(buf) - 1, format, args);
    va_end(args);
    buf[sizeof(buf) - 1] = 0;
    // Find a position of a point on the screen
    static glm::mat4 mtx, view, prj;
    static D3DVIEWPORT9 vp;
    storm::math::ProjectedPoint vrt;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
    rs->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prj));
    mtx = prj * view;
    storm::math::invertRigid(view);
    float dist = storm::math::lengthSquared(pos3D - storm::math::pos(view));
    if (dist >= rad * rad)
        return;
    const float d = glm::dot(storm::math::vz(view), storm::math::pos(view));
    if (glm::dot(pos3D, storm::math::vz(view)) < d)
        return;
    rs->GetViewport(&vp);
    storm::math::projectPoints(mtx, (glm::vec3 *)&pos3D, &vrt, 1, vp.Width * 0.5f, vp.Height * 0.5f, sizeof(glm::vec3), sizeof(storm::math::ProjectedPoint));
    // Find a position
    const float fh = rs->CharHeight(FONT_DEFAULT) * 0.8f;
    vrt.y -= (line + 0.5f) * fh * scale;
    // Transparency
    const float kDist = 0.75f;
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    if (dist > kDist * kDist * rad * rad)
    {
        dist = 1.0f - (sqrtf(dist) - kDist * rad) / (rad - kDist * rad);
        alpha *= dist;
    }
    if (alpha <= 0.0f)
        return;
    color = (static_cast<uint32_t>(alpha * 255.0f) << 24) | (color & 0xffffff);
    // print the text
    rs->ExtPrint(FONT_DEFAULT, color, 0x00000000, PR_ALIGN_CENTER, false, scale, 0, 0, static_cast<int32_t>(vrt.x),
                 static_cast<int32_t>(vrt.y), buf);
}

// Add a damage message
void Location::AddDamageMessage(const glm::vec3 &pos3D, float hit, float curhp, float maxhp)
{
    curMessage++;
    if (curMessage >= sizeof(message) / sizeof(DmgMessage))
        curMessage = 0;
    message[curMessage].p = pos3D;
    message[curMessage].alpha = 1.0f;
    message[curMessage].hit = hit;
    message[curMessage].hp = curhp;
    float k = 0.0f;
    if (maxhp > 0.0f)
        k = curhp / maxhp;
    if (k < 0.0f)
        k = 0.0f;
    if (k > 1.0f)
        k = 1.0f;
    const float r1 = 0.2f, g1 = 1.0f, b1 = 0.2f;
    const float r2 = 1.0f, g2 = 0.2f, b2 = 0.2f;
    const float r = r2 + (r1 - r2) * k;
    const float g = g2 + (g1 - g2) * k;
    const float b = b2 + (b1 - b2) * k;
    message[curMessage].c =
        (static_cast<int32_t>(r * 255.0f) << 16) | (static_cast<int32_t>(g * 255.0f) << 8) | static_cast<int32_t>(b * 255.0f);
}

// Draw bars above the enemy in this frame
void Location::DrawEnemyBars(const glm::vec3 &pos, float hp, float energy, float alpha)
{
    if (enemyBarsCount >= sizeof(enemyBar) / sizeof(enemyBar[0]))
    {
        return;
    }
    if (alpha < 1.0f / 255.0f)
    {
        return;
    }
    if (alpha > 1.0f)
        alpha = 1.0f;
    enemyBar[enemyBarsCount].p = pos;
    enemyBar[enemyBarsCount].alpha = alpha * 255.0f;
    enemyBar[enemyBarsCount].hp = hp;
    enemyBar[enemyBarsCount].energy = energy;
    enemyBarsCount++;
}

void Location::TestLocatorsInPatch(MESSAGE &message)
{
    char buf[4096];
    const std::string &bufStr = message.String();
    strcpy_s(buf, bufStr.c_str());
    LocatorArray *la = FindLocatorsGroup(buf);
    if (!la)
    {
        sprintf_s(buf + 2048, sizeof(buf) - 2048, "Warning: Locators group '%s' not found.", buf);
        buf[sizeof(buf) - 1] = 0;
        core.Event("LocatorsEventTrace", "lsss", 0, buf + 2048, buf, "");
        return;
    }
    const int32_t num = la->Num();
    if (num <= 0)
    {
        sprintf_s(buf, sizeof(buf), "Warning: Locators group '%s' not contain locators.", la->GetGroupName());
        buf[sizeof(buf) - 1] = 0;
        core.Event("LocatorsEventTrace", "lsss", 0, buf, la->GetGroupName(), "");
        return;
    }
    glm::vec3 pos;
    for (int32_t i = 0; i < num; i++)
    {
        la->GetLocatorPos(i, pos.x, pos.y, pos.z);
        float y = 0.0f;
        if (ptc.FindNode(pos, y) < 0)
        {
            sprintf_s(buf, sizeof(buf), "Error: Locator '%s':'%s' not in patch.", la->GetGroupName(),
                      la->LocatorName(i));
            buf[sizeof(buf) - 1] = 0;
            core.Event("LocatorsEventTrace", "lsss", 1, buf, la->GetGroupName(), la->LocatorName(i));
        }
        else
        {
            const float ldist = pos.y - y;
            if (fabsf(ldist) > 0.2f)
            {
                sprintf_s(buf, sizeof(buf), "Warning: Locator '%s':'%s' very far from patch: %fm", la->GetGroupName(),
                          la->LocatorName(i), ldist);
                buf[sizeof(buf) - 1] = 0;
                core.Event("LocatorsEventTrace", "lsss", 0, buf, la->GetGroupName(), la->LocatorName(i));
            }
        }
    }
}

// Drawing bars over characters
void Location::DrawEnemyBars()
{
    const float maxViewDist = 12.0f;
    const float alphaThresholdRelativeDist = 0.8f;
    // Current scene parameters
    static glm::mat4 mtx, view, prj;
    static D3DVIEWPORT9 vp;
    struct SortElement
    {
        storm::math::ProjectedPoint vrt;
        uint32_t color;
        float hp;
        float energy;
    } sort[sizeof(enemyBar) / sizeof(enemyBar[0])];
    SortElement *selements[sizeof(enemyBar) / sizeof(enemyBar[0])];
    int32_t sortCount = 0;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
    rs->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prj));
    mtx = prj * view;
    storm::math::invertRigid(view);
    rs->GetViewport(&vp);
    // Looping through all entries
    for (int32_t i = 0; i < enemyBarsCount; i++)
    {
        // Find a position of a point on the screen
        glm::vec3 &pos3D = enemyBar[i].p;
        const float dist = storm::math::lengthSquared(pos3D - storm::math::pos(view));
        if (dist >= maxViewDist * maxViewDist)
            continue;
        const float d = glm::dot(storm::math::vz(view), storm::math::pos(view));
        if (glm::dot(pos3D, storm::math::vz(view)) < d)
            continue;
        storm::math::ProjectedPoint vrt;
        storm::math::projectPoints(mtx, static_cast<glm::vec3 *>(&pos3D), &vrt, 1, vp.Width * 0.5f, vp.Height * 0.5f, sizeof(glm::vec3), sizeof(storm::math::ProjectedPoint));
        // Calculating transparency
        float k = sqrtf(dist) / maxViewDist;
        if (k > alphaThresholdRelativeDist)
        {
            k = 1.0f - (k - alphaThresholdRelativeDist) / (1.0f - alphaThresholdRelativeDist);
        }
        else
        {
            k = 1.0f;
        }
        uint32_t color = static_cast<int32_t>(k * enemyBar[i].alpha);
        if (!color)
            continue;
        color = (color << 24) | 0x007f7f7f;
        // Add to the rendering list
        sort[sortCount].vrt = vrt;
        sort[sortCount].color = color;
        sort[sortCount].hp = enemyBar[i].hp;
        sort[sortCount].energy = enemyBar[i].energy;
        selements[sortCount] = &sort[sortCount];
        sortCount++;
    }
    // Sort by distance
    for (int32_t i = 0; i < sortCount - 1; i++)
    {
        for (int32_t j = i + 1; j < sortCount; j++)
        {
            if (selements[i]->vrt.z < selements[j]->vrt.z)
            {
                SortElement *tmp = selements[i];
                selements[i] = selements[j];
                selements[j] = tmp;
            }
        }
    }
    // Rendering
    for (int32_t i = 0; i < sortCount; i++)
    {
        storm::math::ProjectedPoint &vrt = selements[i]->vrt;
        uint32_t &color = selements[i]->color;
        static BarVertex bar[18];
        float width = (256.0f * vrt.rhw) * 0.5f;
        float height = (64.0f * vrt.rhw) * 0.5f;
        if (width > vp.Width * 0.1f)
        {
            const float k = vp.Width * 0.1f / width;
            width *= k;
            height *= k;
        }
        // Background
        bar[0].p.x = vrt.x - width;
        bar[0].p.y = vrt.y - height;
        bar[0].u = 0.0f;
        bar[0].v = 0.0f;
        bar[1].p.x = vrt.x + width;
        bar[1].p.y = vrt.y - height;
        bar[1].u = 1.0f;
        bar[1].v = 0.0f;
        bar[2].p.x = vrt.x - width;
        bar[2].p.y = vrt.y + height;
        bar[2].u = 0.0f;
        bar[2].v = 0.5f;
        bar[3] = bar[2];
        bar[4] = bar[1];
        bar[5].p.x = vrt.x + width;
        bar[5].p.y = vrt.y + height;
        bar[5].u = 1.0f;
        bar[5].v = 0.5f;
        // A life
        bar[6].p.x = vrt.x - width;
        bar[6].p.y = vrt.y - height;
        bar[6].u = 0.0f;
        bar[6].v = 0.5f;
        bar[7].p.x = vrt.x + width;
        bar[7].p.y = vrt.y - height;
        bar[7].u = 1.0f;
        bar[7].v = 0.5f;
        bar[8].p.x = vrt.x - width;
        bar[8].p.y = vrt.y;
        bar[8].u = 0.0f;
        bar[8].v = 0.75f;
        bar[9] = bar[8];
        bar[10] = bar[7];
        bar[11].p.x = vrt.x + width;
        bar[11].p.y = vrt.y;
        bar[11].u = 1.0f;
        bar[11].v = 0.75f;
        // Energy
        bar[12].p.x = vrt.x - width;
        bar[12].p.y = vrt.y;
        bar[12].u = 0.0f;
        bar[12].v = 0.75f;
        bar[13].p.x = vrt.x + width;
        bar[13].p.y = vrt.y;
        bar[13].u = 1.0f;
        bar[13].v = 0.75f;
        bar[14].p.x = vrt.x - width;
        bar[14].p.y = vrt.y + height;
        bar[14].u = 0.0f;
        bar[14].v = 1.0f;
        bar[15] = bar[14];
        bar[16] = bar[13];
        bar[17].p.x = vrt.x + width;
        bar[17].p.y = vrt.y + height;
        bar[17].u = 1.0f;
        bar[17].v = 1.0f;
        // Common fields
        for (int32_t n = 0; n < sizeof(bar) / sizeof(bar[0]); n++)
        {
            bar[n].p.z = vrt.z;
            bar[n].rhw = vrt.rhw;
            bar[n].c = color;
        }
        // Adjusting the bars
        CorrectBar(selements[i]->hp, 0.03f, 0.97f, bar + 6);
        CorrectBar(selements[i]->energy, 0.03f, 0.97f, bar + 12);
        // Draw
        rs->TextureSet(0, enemyBarsTexture);
        rs->DrawPrimitiveUP(D3DPT_TRIANGLELIST, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 6, bar, sizeof(bar[0]),
                            "LocationModelBlend");
    }
}

void Location::CorrectBar(float v, float start, float end, BarVertex *vrt)
{
    end = start + (end - start) * v;
    const float dx = vrt[1].p.x - vrt[0].p.x;
    const float du = vrt[1].u - vrt[0].u;
    const float startX = vrt[0].p.x + dx * start;
    const float startU = vrt[0].u + du * start;
    const float endX = vrt[0].p.x + dx * end;
    const float endU = vrt[0].u + du * end;
    vrt[0].p.x = startX;
    vrt[0].u = startU;
    vrt[1].p.x = endX;
    vrt[1].u = endU;
    vrt[2].p.x = startX;
    vrt[2].u = startU;
    vrt[3].p.x = startX;
    vrt[3].u = startU;
    vrt[4].p.x = endX;
    vrt[4].u = endU;
    vrt[5].p.x = endX;
    vrt[5].u = endU;
}

bool Location::IsSwimming() const
{
    return bSwimming;
}

void Location::LoadCaustic() const
{
    bCausticEnable = false;

    ATTRIBUTES *pC = AttributesPointer->GetAttributeClass("Caustic");
    if (!pC)
        return;

    fCausticDelta = 0.0f;
    fCausticFrame = 0.0f;

    fCausticScale = pC->GetAttributeAsFloat("scale");
    fFogDensity = pC->GetAttributeAsFloat("fogdensity");
    fCausticDistance = pC->GetAttributeAsFloat("distance");
    v4CausticColor = storm::math::colorFromARGB(pC->GetAttributeAsDword("color"));
    fCausticSpeed = pC->GetAttributeAsFloat("speed");

    char tex[256];
    for (int32_t i = 0; i < 32; i++)
    {
        sprintf_s(tex, "weather\\caustic\\caustic%.2d.tga", i);
        iCausticTex[i] = rs->TextureCreate(tex);
    }
}
