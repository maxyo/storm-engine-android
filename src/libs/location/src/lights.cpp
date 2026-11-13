//============================================================================================
//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    Lights
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#include "lights.h"

#include "flame_flicker.hpp"
#include "light_choice.hpp"

#include <cmath>

#include "core.h"
#include "storm/cosmetic_time.hpp"
#include "storm/render/effects.hpp"
#include "string_compare.hpp"

// ============================================================================================
// Construction, destruction
// ============================================================================================

Lights::Lights()
    : lighter_code(0), lampModels{}, buf{}
{
    rs = nullptr;
    collide = nullptr;
    numTypes = 0;
    maxTypes = 0;
    numLights = 0;
    maxLights = 0;
    for (int32_t i = 0; i < 8; i++)
    {
        lt[i].light = -1;
        lt[i].set = false;
    }
    numLampModels = 0;
}

Lights::~Lights()
{
    for (int32_t i = 0; i < numTypes; i++)
    {
        if (types[i].corona >= 0 && rs)
            rs->TextureRelease(types[i].corona);
        delete types[i].name;
    }
    if (rs)
        for (int32_t i = 1; i < 8; i++)
            rs->LightEnable(i, false);
}

// Initialization
bool Lights::Init()
{
    // renderer
    rs = static_cast<VRenderer *>(core.GetService("renderer"));
    if (!rs)
        throw std::runtime_error("No service: renderer");
    collide = static_cast<COLLIDE *>(core.GetService("COLL"));
    // read the parameters
    auto ini = fio->OpenIniFile("RESOURCE\\Ini\\lights.ini");
    if (!ini)
    {
        core.Trace("Location lights not inited -> RESOURCES\\Ini\\lights.ini not found");
        return false;
    }
    char lName[256];
    auto res = ini->GetSectionName(lName, sizeof(lName) - 1);
    while (res)
    {
        lName[sizeof(lName) - 1] = 0;
        int32_t i;
        for (i = 0; i < numTypes; i++)
        {
            if (storm::iEquals(lName, types[i].name))
            {
                core.Trace("Location lights redefinition light: %s", lName);
                break;
            }
        }
        if (i == numTypes)
        {
            types.push_back({});
            // Save the name
            const auto len = strlen(lName) + 1;
            types[numTypes].name = new char[len];
            memcpy(types[numTypes].name, lName, len);
            // Reading parameters
            types[numTypes].color.b = ini->GetFloat(lName, "b", 1.0f);
            types[numTypes].color.g = ini->GetFloat(lName, "g", 1.0f);
            types[numTypes].color.r = ini->GetFloat(lName, "r", 1.0f);
            types[numTypes].color.a = 1.0f;
            types[numTypes].dxLight.Type = D3DLIGHT_POINT;
            types[numTypes].dxLight.Diffuse = types[numTypes].color;
            types[numTypes].dxLight.Range = ini->GetFloat(lName, "range", 10.0f);
            types[numTypes].dxLight.Attenuation0 = ini->GetFloat(lName, "att0", 0.0f);
            types[numTypes].dxLight.Attenuation1 = ini->GetFloat(lName, "att1", 0.0f);
            types[numTypes].dxLight.Attenuation2 = ini->GetFloat(lName, "att2", 1.0f);
            types[numTypes].flicker = ini->GetFloat(lName, "flicker", 0.0f);
            types[numTypes].freq = ini->GetFloat(lName, "freq", 0.0f);
            types[numTypes].flickerSlow = ini->GetFloat(lName, "flickerSlow", 0.0f);
            types[numTypes].freqSlow = ini->GetFloat(lName, "freqSlow", 0.0f);
            types[numTypes].corona = -1;
            types[numTypes].coronaRange = ini->GetFloat(lName, "coronaRange", 20.0f);
            types[numTypes].coronaSize = ini->GetFloat(lName, "coronaSize", 1.0f);
            if (types[numTypes].coronaRange > 0.0f && types[numTypes].coronaSize > 0.0f)
            {
                types[numTypes].invCoronaRange = 1.0f / types[numTypes].coronaRange;
                char texture[256];
                if (ini->ReadString(lName, "corona", texture, sizeof(texture), ""))
                {
                    if (texture[0])
                    {
                        types[numTypes].corona = rs->TextureCreate(texture);
                    }
                }
            }
            types[numTypes].coronaRange2 = types[numTypes].coronaRange * types[numTypes].coronaRange;
            if (types[numTypes].flicker <= 0.0f)
                types[numTypes].freq = 0.0f;
            if (types[numTypes].flicker > 1.0f)
                types[numTypes].flicker = 1.0f;
            if (types[numTypes].freq <= 0.0f)
                types[numTypes].flicker = 0.0f;
            if (types[numTypes].freq > 0.0f)
                types[numTypes].p = 1.0f / types[numTypes].freq;
            if (types[numTypes].flickerSlow <= 0.0f)
                types[numTypes].freqSlow = 0.0f;
            if (types[numTypes].flickerSlow > 1.0f)
                types[numTypes].flickerSlow = 1.0f;
            if (types[numTypes].freqSlow <= 0.0f)
                types[numTypes].flickerSlow = 0.0f;
            if (types[numTypes].freqSlow > 0.0f)
                types[numTypes].pSlow = 1.0f / types[numTypes].freqSlow;
            numTypes++;
        }
        res = ini->GetSectionNameNext(lName, sizeof(lName) - 1);
    }
    if (numTypes == 0)
    {
        core.Trace("Location lights not inited -> 0 light types");
        return false;
    }
    // start executing
    core.SetLayerType(EXECUTE, layer_type_t::execute);
    core.AddToLayer(EXECUTE, GetId(), 10);
    core.SetLayerType(REALIZE, layer_type_t::realize);
    core.AddToLayer(REALIZE, GetId(), -1000);
    return true;
}

// Execution
void Lights::Execute(uint32_t delta_time)
{
    if (core.Controls->GetDebugAsyncKeyState(VK_SHIFT) < 0 && core.Controls->GetDebugAsyncKeyState(VK_F11) < 0)
    {
        for (int32_t i = 0; i < numTypes; i++)
            UpdateLightTypes(i);
    }
    // The flames' flicker is only for the eye: real time, so the time scale changes neither its speed nor its strength.
    const float seconds = storm::CosmeticDeltaSeconds(delta_time, core.GetRDeltaTime());
    for (int32_t i = 0; i < numLights; i++)
    {
        auto &l = types[lights[i].type];
        if (l.flicker == 0.0f && l.flickerSlow == 0.0f)
            continue;
        const float k = storm::location::StepFlameFlicker(lights[i].flicker, {l.flicker, l.p, l.flickerSlow, l.freqSlow, l.pSlow},
                                                          seconds);
        lights[i].color.b = k * l.color.b;
        lights[i].color.g = k * l.color.g;
        lights[i].color.r = k * l.color.r;
    }
}

void Lights::Realize(uint32_t delta_time)
{
    // Camera position
    glm::vec3 pos, ang;
    rs->GetCamera(pos, ang, ang.x);

    // Graphics effects: carried lanterns are the only location lights not baked into the static models' colours
    // (src/libs/lighter); the per-pixel lighting adds them to that geometry.
    std::vector<storm::render::EffectsPointLight> dynamic;
    dynamic.reserve(aMovingLight.size());
    for (const MovingLight &moving : aMovingLight)
    {
        if (moving.light < 0 || moving.light >= numLights)
            continue;
        const Light &l = lights[moving.light];
        const D3DLIGHT9 &type = types[l.type].dxLight;
        dynamic.push_back({{l.pos.x, l.pos.y, l.pos.z},
                           {l.color.r, l.color.g, l.color.b},
                           type.Range,
                           type.Attenuation0,
                           type.Attenuation1,
                           type.Attenuation2});
    }
    storm::render::SetEffectsDynamicLights(dynamic);
    // The other lights stand still: the strongest of them cast the lamp shadows of interiors.
    std::vector<bool> moving(numLights, false);
    for (const MovingLight &m : aMovingLight)
        if (m.light >= 0 && m.light < numLights)
            moving[m.light] = true;
    std::vector<storm::render::EffectsPointLight> lamps;
    for (int32_t i = 0; i < numLights; ++i)
    {
        const Light &l = lights[i];
        if (moving[i])
            continue;
        // `color` carries the flicker.
        const D3DLIGHT9 &type = types[l.type].dxLight;
        lamps.push_back({{l.pos.x, l.pos.y, l.pos.z}, {l.color.r, l.color.g, l.color.b}, type.Range,
                         type.Attenuation0, type.Attenuation1, type.Attenuation2});
    }
    storm::render::SetEffectsLamps(lamps);

    ////////////////
    // Draw coronas
    glm::mat4 camMtx;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(camMtx));
    rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(glm::mat4()));
    rs->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(glm::mat4()));
    const auto camPDist = -(pos.x * storm::math::vx(camMtx).z + pos.y * storm::math::vy(camMtx).z + pos.z * storm::math::vz(camMtx).z);
    for (int32_t i = 0, n = 0; i < numLights; i++)
    {
        // Source
        auto &ls = lights[i];
        auto &l = types[ls.type];
        if (l.corona < 0)
            continue;
        // in the foreground
        auto dist = ls.pos.x * storm::math::vx(camMtx).z + ls.pos.y * storm::math::vy(camMtx).z + ls.pos.z * storm::math::vz(camMtx).z + camPDist;
        if (dist < -2.0f * l.coronaSize)
            continue;
        // Distance
        const auto dx = ls.pos.x - pos.x;
        const auto dy = ls.pos.y - pos.y;
        const auto dz = ls.pos.z - pos.z;
        auto d = dx * dx + dy * dy + dz * dz;
        auto isVisible = d < l.coronaRange2;
        if (!isVisible)
            continue;

        // Visibility
        if (collide)
        {
            const auto dist = collide->Trace(core.GetEntityIds(SUN_TRACE), pos,
                                             glm::vec3(ls.pos.x, ls.pos.y, ls.pos.z), lampModels, numLampModels);
            isVisible = dist > 1.0f;
        }
        ls.corona += isVisible ? 0.008f * delta_time : -0.008f * delta_time;
        if (ls.corona <= 0.0f)
        {
            ls.corona = 0.0f;
            continue;
        }
        if (ls.corona > 1.0f)
            ls.corona = 1.0f;
        // Distance
        dist = sqrtf(d);
        d = dist * l.invCoronaRange;
        if (d > 1.0f)
            d = 1.0f;
        auto alpha = 1.0f;
        if (d < 0.3f)
            alpha *= 0.2f + 0.8f * d / 0.3f;
        if (d > 0.4f)
        {
            alpha *= 1.0f - (d - 0.4f) / 0.6f;
            alpha *= alpha;
        }
        alpha *= ls.corona * 255.0f;
        // Deviation coefficient
        d = ls.flicker.i * 0.4f;
        if (d < -0.1f)
            d = -0.1f;
        if (d > 0.1f)
            d = 0.1f;
        d += 1.0f;
        // Current size
        const auto size = d * l.coronaSize;
        // Transparency
        alpha *= d;
        if (alpha < 0.0f)
            alpha = 0.0f;
        if (alpha > 255.0f)
            alpha = 255.0f;
        // Position
        auto pos = storm::math::transformPoint(camMtx, glm::vec3(ls.pos.x, ls.pos.y, ls.pos.z));
        // Colour
        auto c = static_cast<uint32_t>(alpha);
        c |= (c << 24) | (c << 16) | (c << 8);
        // Angle of rotation
        float cs, sn;
        if (dist > 0.0f)
        {
            auto _cs = (dx * storm::math::vx(camMtx).z + dz * storm::math::vz(camMtx).z);
            auto _sn = (dx * storm::math::vz(camMtx).z - dz * storm::math::vx(camMtx).z);
            auto kn = _cs * _cs + _sn * _sn;
            if (kn > 0.0f)
            {
                kn = 1.0f / sqrtf(kn);
                _cs *= kn;
                _sn *= kn;
                cs = (_cs * _cs - _sn * _sn);
                sn = 2.0f * _cs * _sn;
            }
            else
            {
                cs = 1.0f;
                sn = 0.0f;
            }
        }
        else
        {
            cs = 1.0f;
            sn = 0.0f;
        }
        // fill in
        buf[n * 6 + 0].pos = pos + glm::vec3(size * (-cs + sn), size * (sn + cs), 0.0f);
        buf[n * 6 + 0].color = c;
        buf[n * 6 + 0].u = 0.0f;
        buf[n * 6 + 0].v = 0.0f;
        buf[n * 6 + 1].pos = pos + glm::vec3(size * (-cs - sn), size * (sn - cs), 0.0f);
        buf[n * 6 + 1].color = c;
        buf[n * 6 + 1].u = 0.0f;
        buf[n * 6 + 1].v = 1.0f;
        buf[n * 6 + 2].pos = pos + glm::vec3(size * (cs + sn), size * (-sn + cs), 0.0f);
        buf[n * 6 + 2].color = c;
        buf[n * 6 + 2].u = 1.0f;
        buf[n * 6 + 2].v = 0.0f;
        buf[n * 6 + 3].pos = buf[n * 6 + 2].pos;
        buf[n * 6 + 3].color = c;
        buf[n * 6 + 3].u = 1.0f;
        buf[n * 6 + 3].v = 0.0f;
        buf[n * 6 + 4].pos = buf[n * 6 + 1].pos;
        buf[n * 6 + 4].color = c;
        buf[n * 6 + 4].u = 0.0f;
        buf[n * 6 + 4].v = 1.0f;
        buf[n * 6 + 5].pos = pos + glm::vec3(size * (cs - sn), size * (-sn - cs), 0.0f);
        buf[n * 6 + 5].color = c;
        buf[n * 6 + 5].u = 1.0f;
        buf[n * 6 + 5].v = 1.0f;
        n++;
        rs->TextureSet(0, l.corona);
        rs->DrawPrimitiveUP(D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, n * 2, buf, sizeof(Vertex),
                            "Coronas");
        n = 0;
    }
    rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(camMtx));

    // Debug
    if (core.Controls->GetDebugAsyncKeyState(VK_SHIFT) < 0 && core.Controls->GetDebugAsyncKeyState(VK_SPACE) < 0)
    {
        PrintDebugInfo();
    }
}

// Find source index
int32_t Lights::FindLight(const char *name)
{
    if (!name || !name[0])
        return -1;
    for (int32_t i = 0; i < numTypes; i++)
        if (storm::iEquals(name, types[i].name))
            return i;
    return -1;
}

// Add source to location
void Lights::AddLight(int32_t index, const glm::vec3 &pos)
{
    if (index == -1)
        return;
    Assert(index >= 0 && index < numTypes);
    if (numLights >= maxLights)
    {
        maxLights += 32;
        lights.resize(maxLights);
    }
    lights[numLights].color = types[index].color;
    lights[numLights].pos.x = pos.x;
    lights[numLights].pos.y = pos.y;
    lights[numLights].pos.z = pos.z;
    lights[numLights].flicker = {};
    lights[numLights].type = index;

    // Send a message to the lighter
    if (const auto eid = core.GetEntityId("Lighter"))
    {
        core.Send_Message(eid, "sffffffffffs", "AddLight", pos.x, pos.y, pos.z, types[index].color.r,
                          types[index].color.g, types[index].color.b, types[index].dxLight.Attenuation0,
                          types[index].dxLight.Attenuation1, types[index].dxLight.Attenuation2,
                          types[index].dxLight.Range, types[index].name);
    }

    numLights++;
}

// Add lantern model
bool Lights::AddLampModel(const entid_t lampModel)
{
    if (numLampModels >= sizeof(lampModels) / sizeof(entid_t))
        return false;
    lampModels[numLampModels++] = lampModel;
    return true;
}

void Lights::DelAllLights()
{
    aMovingLight.clear();
    numLights = 0;
}

// Add portable source
int32_t Lights::AddMovingLight(const char *type, const glm::vec3 &pos)
{
    int32_t idx;
    for (idx = 0; idx < 1000; idx++)
    {
        int32_t n;
        for (n = 0; n < aMovingLight.size(); n++)
            if (aMovingLight[n].id == idx)
                break;
        if (n == aMovingLight.size())
            break;
    }
    if (idx == 1000)
        return -1;

    const auto nType = FindLight(type);
    if (nType < 0)
        return -1;

    aMovingLight.emplace_back(MovingLight{idx, static_cast<int32_t>(numLights)});
    AddLight(nType, pos);
    return idx;
}

// Put portable source in new position
void Lights::UpdateMovingLight(int32_t id, const glm::vec3 &pos)
{
    for (int32_t n = 0; n < aMovingLight.size(); n++)
    {
        if (aMovingLight[n].id == id)
        {
            const auto i = aMovingLight[n].light;
            if (i >= 0 && i < numLights)
                lights[i].pos = *(D3DVECTOR *)&pos;
            return;
        }
    }
}

// Remove portable source
void Lights::DelMovingLight(int32_t id)
{
    for (int32_t n = 0; n < aMovingLight.size(); n++)
        if (aMovingLight[n].id == id)
        {
            numLights--;
            for (auto i = aMovingLight[n].light; i < numLights; i++)
                lights[i] = lights[i + 1];
            aMovingLight.erase(aMovingLight.begin() + n);
            return;
        }
}

// Set the light sources for a model at `pos` (a character, a lit model of the location)
void Lights::SetLightsAt(const glm::vec3 &pos)
{
    UnsetLights();
    uint32_t d3d_light_index = max_d3d_lights - max_d3d_custom_lights;
    // The sources that are strongest at `pos` (light_choice.hpp): the camera has no part in the choice.
    const auto strength = [&](size_t i) {
        // The type's steady colour: a flicker must not reorder the sources.
        const D3DCOLORVALUE &c = types[lights[i].type].color;
        const D3DLIGHT9 &l = types[lights[i].type].dxLight;
        const float dx = pos.x - lights[i].pos.x, dy = pos.y - lights[i].pos.y, dz = pos.z - lights[i].pos.z;
        return storm::location::LightStrength(0.299f * c.r + 0.587f * c.g + 0.114f * c.b,
                                              l.Attenuation0, l.Attenuation1, l.Attenuation2, l.Range,
                                              std::sqrt(dx * dx + dy * dy + dz * dz));
    };
    for (const uint32_t index : storm::location::ChooseLights(numLights, strength, max_d3d_custom_lights))
    {
        D3DLIGHT9 light = types[lights[index].type].dxLight;
        light.Diffuse.r = lights[index].color.r;
        light.Diffuse.g = lights[index].color.g;
        light.Diffuse.b = lights[index].color.b;
        light.Position = lights[index].pos;

        rs->SetLight(d3d_light_index, &light);
        rs->LightEnable(d3d_light_index, true);
        lt[d3d_light_index].light = index;
        lt[d3d_light_index].set = true;

        d3d_light_index++;
    }
}

void Lights::UnsetLights()
{
    for (auto i = max_d3d_lights - max_d3d_custom_lights; i < max_d3d_lights; i++)
    {
        if (lt[i].set)
        {
            lt[i].set = false;
            rs->LightEnable(i, false);
        }
    }
}

// Update source types
void Lights::UpdateLightTypes(int32_t i)
{
    auto ini = fio->OpenIniFile("RESOURCE\\Ini\\lights.ini");
    if (!ini)
        return;
    // Source name
    char *lName = types[i].name;
    // Reading parameters
    types[i].color.b = ini->GetFloat(lName, "b", 1.0f);
    types[i].color.g = ini->GetFloat(lName, "g", 1.0f);
    types[i].color.r = ini->GetFloat(lName, "r", 1.0f);
    types[i].color.a = 1.0f;
    types[i].dxLight.Type = D3DLIGHT_POINT;
    types[i].dxLight.Diffuse = types[i].color;
    types[i].dxLight.Range = ini->GetFloat(lName, "range", 10.0f);
    types[i].dxLight.Attenuation0 = ini->GetFloat(lName, "att0", 0.0f);
    types[i].dxLight.Attenuation1 = ini->GetFloat(lName, "att1", 0.0f);
    types[i].dxLight.Attenuation2 = ini->GetFloat(lName, "att2", 1.0f);
    types[i].flicker = ini->GetFloat(lName, "flicker", 0.0f);
    types[i].freq = ini->GetFloat(lName, "freq", 0.0f);
    types[i].flickerSlow = ini->GetFloat(lName, "flickerSlow", 0.0f);
    types[i].freqSlow = ini->GetFloat(lName, "freqSlow", 0.0f);
    types[i].corona = -1;
    types[i].coronaRange = ini->GetFloat(lName, "coronaRange", 20.0f);
    types[i].coronaSize = ini->GetFloat(lName, "coronaSize", 1.0f);
    if (types[i].coronaRange > 0.0f && types[i].coronaSize > 0.0f)
    {
        types[i].invCoronaRange = 1.0f / types[i].coronaRange;
        char texture[256];
        if (ini->ReadString(lName, "corona", texture, sizeof(texture), ""))
        {
            if (texture[0])
            {
                types[i].corona = rs->TextureCreate(texture);
            }
        }
    }
    types[i].coronaRange2 = types[i].coronaRange * types[i].coronaRange;
    if (types[i].flicker <= 0.0f)
        types[i].freq = 0.0f;
    if (types[i].flicker > 1.0f)
        types[i].flicker = 1.0f;
    if (types[i].freq <= 0.0f)
        types[i].flicker = 0.0f;
    if (types[i].freq > 0.0f)
        types[i].p = 1.0f / types[i].freq;
    if (types[i].flickerSlow <= 0.0f)
        types[i].freqSlow = 0.0f;
    if (types[i].flickerSlow > 1.0f)
        types[i].flickerSlow = 1.0f;
    if (types[i].freqSlow <= 0.0f)
        types[i].flickerSlow = 0.0f;
    if (types[i].freqSlow > 0.0f)
        types[i].pSlow = 1.0f / types[i].freqSlow;
    for (int32_t c = 0; c < numLights; c++)
    {
        if (lights[c].type != i)
            continue;
        lights[c].color = types[i].color;
    }
}

void Lights::PrintDebugInfo()
{
    for (uint32_t i = 0; i < numLights; ++i)
    {
        auto lightPos = *(glm::vec3*)&lights[i].pos;
        auto scale = 1.0f;

        static glm::mat4 mtx, view, prj;
        static D3DVIEWPORT9 vp;
        storm::math::ProjectedPoint vrt;
        rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
        rs->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prj));
        mtx = prj * view;
        storm::math::invertRigid(view);
        rs->GetViewport(&vp);
        storm::math::projectPoints(mtx, &lightPos, &vrt, 1, vp.Width * 0.5f, vp.Height * 0.5f, sizeof(glm::vec3), sizeof(storm::math::ProjectedPoint));
        vrt.y -= rs->CharHeight(FONT_DEFAULT) / 2;

        glm::vec3 pos, ang;
        rs->GetCamera(pos, ang, ang.x);
        // visibility
        const auto camPDist = -(pos.x * storm::math::vx(view).z + pos.y * storm::math::vy(view).z + pos.z * storm::math::vz(view).z);

        auto &ls = lights[i];
        auto &l = types[ls.type];
        if (l.corona < 0)
            continue;
        // in the foreground
        auto dist = ls.pos.x * storm::math::vx(view).z + ls.pos.y * storm::math::vy(view).z + ls.pos.z * storm::math::vz(view).z + camPDist;
        if (dist < -2.0f * l.coronaSize)
            continue;
        // Distance
        const auto dx = ls.pos.x - pos.x;
        const auto dy = ls.pos.y - pos.y;
        const auto dz = ls.pos.z - pos.z;
        auto d = dx * dx + dy * dy + dz * dz;
        auto isVisible = d < l.coronaRange2;
        if (!isVisible)
            continue;

        rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(glm::mat4()));
        rs->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(glm::mat4()));

        rs->ExtPrint(FONT_DEFAULT, D3DCOLOR_ARGB(255, 255, 255, 255), 0x00000000, PR_ALIGN_CENTER, 
            false, scale, 0, 0,
                     static_cast<int32_t>(vrt.x), static_cast<int32_t>(vrt.y), fmt::format("{}", d).c_str());

        // print idx
        auto color = D3DCOLOR_ARGB(255, 233, 30, 30);
        vrt.y -= 1.5 * rs->CharHeight(FONT_DEFAULT) / 2;
        for (const auto [_, light] : lt)
        {
            if (i == light)
            {
                color = D3DCOLOR_ARGB(255, 30, 233, 30);
            }
        }
        rs->ExtPrint(FONT_DEFAULT, color, 0x00000000, PR_ALIGN_CENTER, false, scale, 0, 0, static_cast<int32_t>(vrt.x),
                     static_cast<int32_t>(vrt.y),
                     fmt::format("{}", i).c_str());

        
        rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
    }
}
