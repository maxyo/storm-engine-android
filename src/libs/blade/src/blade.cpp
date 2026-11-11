/******************************************************************************
File:  blade.cpp

Author:   Nick Chirkov

Comments:
model binded to an animated locator
******************************************************************************/
#include "blade.h"

#include "animation.h"
#include "core.h"
#include "geometry.h"
#include "shared/messages.h"

CREATE_CLASS(BLADE)

static const char *handName = "Saber_hand";
static const char *beltName = "Saber_belt";
static const char *bloodName = "Saber_blood";
static const char *lightName = "Saber_light";
static const char *bladeStart = "start";
static const char *bladeEnd = "end";

static const char *gunHandName = "gun_hand";
static const char *gunBeltName = "gun_belt";
static const char *gunFire = "gun_fire";

static const char *sabergunHandName = "sabergun_hand";
static const char *sabergunBeltName = "sabergun_belt";
static const char *sabergunFire = "sabersabgun_fire";

BLADE::BLADE_INFO::BLADE_INFO()
    : eid(0), vrt{}
{
    locatorName = beltName;
    defLifeTime = 0.15f;
    lifeTime = 0.0f;
    color[0] = 0x80162EBE;
    color[1] = 0x00FF0000;
    time = 0.0f;
    for (int32_t v = 0; v < WAY_LENGTH; v++)
        vrtTime[v] = -1e20f;
}

BLADE::BLADE_INFO::~BLADE_INFO()
{
    core.EraseEntity(eid);
}

void BLADE::BLADE_INFO::DrawBlade(VRenderer *rs, unsigned int blendValue, MODEL *mdl, NODE *manNode)
{
    auto *obj = static_cast<MODEL *>(core.GetEntityPointer(eid));
    if (obj != nullptr)
    {
        glm::mat4 perMtx;

        auto *bladeNode = obj->GetNode(0);
        if ((blendValue & 0xff000000) == 0xff000000)
        {
            bladeNode->SetTechnique("EnvAmmoShader");
        }
        else
        {
            bladeNode->SetTechnique("AnimationBlend");
        }
        int32_t sti = -1;
        auto idBlade = manNode->geo->FindName(locatorName);

        if ((sti = manNode->geo->FindLabelN(sti + 1, idBlade)) > -1)
        {
            auto *ani = mdl->GetAnimation();
            auto *bones = &ani->GetAnimationMatrix(0);

            GEOS::LABEL lb;
            manNode->geo->GetLabel(sti, lb);
            glm::mat4 mt;
            storm::math::vx(mt) = glm::vec3(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            storm::math::vy(mt) = glm::vec3(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            storm::math::vz(mt) = glm::vec3(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            storm::math::pos(mt) = glm::vec3(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            auto mbn = bones[lb.bones[0]] * mt;
            storm::math::pos(mbn).x *= -1.0f;
            storm::math::vx(mbn).x *= -1.0f;
            storm::math::vy(mbn).x *= -1.0f;
            storm::math::vz(mbn).x *= -1.0f;
            glm::mat4 scl;
            storm::math::vx(scl).x = -1.0f;
            storm::math::vy(scl).y = 1.0f;
            storm::math::vz(scl).z = 1.0f;
            mbn = glm::mat4(mbn) * scl;

            perMtx = mdl->mtx * mbn;
        }
        obj->mtx = perMtx;
        obj->ProcessStage(Stage::realize, 0);

        //--------------------------------------------------------------------------
        rs->TextureSet(0, -1);
        rs->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(glm::mat4()));

        // move to the beginning
        int32_t first = 0; // end vertex 2 draw
        for (int32_t v = WAY_LENGTH - 2; v > -1; v--)
        {
            vrt[v * 2 + 2] = vrt[v * 2 + 0];
            vrt[v * 2 + 3] = vrt[v * 2 + 1];
            vrtTime[v + 1] = vrtTime[v + 0];

            if (vrtTime[v + 1] + lifeTime <= time)
                continue;

            first++;

            auto blend = (time - vrtTime[v + 1]) / lifeTime;

            auto fcol0 = static_cast<float>((color[0] >> 24) & 0xFF);
            auto fcol1 = static_cast<float>((color[1] >> 24) & 0xFF);
            auto a = static_cast<uint32_t>(fcol0 + blend * (fcol1 - fcol0));

            fcol0 = static_cast<float>((color[0] >> 16) & 0xFF);
            fcol1 = static_cast<float>((color[1] >> 16) & 0xFF);
            auto r = static_cast<uint32_t>(fcol0 + blend * (fcol1 - fcol0));

            fcol0 = static_cast<float>((color[0] >> 8) & 0xFF);
            fcol1 = static_cast<float>((color[1] >> 8) & 0xFF);
            auto g = static_cast<uint32_t>(fcol0 + blend * (fcol1 - fcol0));

            fcol0 = static_cast<float>((color[0] >> 0) & 0xFF);
            fcol1 = static_cast<float>((color[1] >> 0) & 0xFF);
            auto b = static_cast<uint32_t>(fcol0 + blend * (fcol1 - fcol0));

            vrt[v * 2 + 2].diffuse = vrt[v * 2 + 3].diffuse = (a << 24) | (r << 16) | (g << 8) | b;
        }

        // search for start
        sti = bladeNode->geo->FindLabelN(0, bladeNode->geo->FindName(bladeStart));
        GEOS::LABEL lb;
        if (sti >= 0)
        {
            bladeNode->geo->GetLabel(sti, lb);
            vrt[0].pos = storm::math::transformPoint(perMtx, glm::vec3(lb.m[3][0], lb.m[3][1], lb.m[3][2]));
            vrt[0].diffuse = color[0];
            sti = bladeNode->geo->FindLabelN(0, bladeNode->geo->FindName(bladeEnd));
            if (sti >= 0)
            {
                bladeNode->geo->GetLabel(sti, lb);
                vrt[1].pos = storm::math::transformPoint(perMtx, glm::vec3(lb.m[3][0], lb.m[3][1], lb.m[3][2]));
                vrt[1].diffuse = color[0];
                vrtTime[0] = time;

                auto bDraw = rs->TechniqueExecuteStart("Blade");
                if (bDraw)
                {
                    if (first > 0)
                        rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, FVF, first * 2, &vrt[0], sizeof vrt[0]);
                }
                while (rs->TechniqueExecuteNext())
                {
                }
            }
            else
            {
                core.Trace("BLADE::Realize -> no find locator \"%s\", model \"%s\"", bladeEnd, bladeNode->GetName());
            }
        }
        else
        {
            core.Trace("BLADE::Realize -> no find locator \"%s\", model \"%s\"", bladeStart, bladeNode->GetName());
        }
    }
}

bool BLADE::BLADE_INFO::LoadBladeModel(MESSAGE &message)
{
    core.EraseEntity(eid);

    // model name
    const std::string &mdlName = message.String();
    if (!mdlName.empty())
    {
        // path of the model
        char path[256];
        strcpy_s(path, "Ammo\\");
        strcat_s(path, mdlName.c_str());
        // path of the textures
        auto *gs = static_cast<VGEOMETRY *>(core.GetService("geometry"));
        if (gs)
            gs->SetTexturePath("Ammo\\");
        // Create a model
        eid = core.CreateEntity("modelr");
        if (!core.Send_Message(eid, "ls", MSG_MODEL_LOAD_GEO, path))
        {
            core.EraseEntity(eid);
            if (gs)
                gs->SetTexturePath("");
            return false;
        }
        if (gs)
            gs->SetTexturePath("");
        // trace parameters
        defLifeTime = message.Float();
        color[0] = message.Long();
        color[1] = message.Long();
    }
    else
        return false;
    return true;
}

BLADE::BLADE()
{
    gunLocName = gunBeltName;
    blendValue = 0xFFFFFFFF;
}

BLADE::~BLADE()
{
    core.EraseEntity(gun);

    for (int32_t i = 0; i < ITEMS_INFO_QUANTITY; i++)
        items[i].Release();
}

bool BLADE::Init()
{
    // GUARD(BLADE::BLADE())

    col = static_cast<COLLIDE *>(core.GetService("coll"));
    if (col == nullptr)
        throw std::runtime_error("No service: COLLIDE");

    core.AddToLayer(REALIZE, GetId(), 65550);

    rs = static_cast<VRenderer *>(core.GetService("dx9render"));
    if (!rs)
        throw std::runtime_error("No service: dx9render");

    // UNGUARD
    return true;
}

//------------------------------------------------------------------------------------
// realize
//------------------------------------------------------------------------------------
void BLADE::Realize(uint32_t Delta_Time)
{
    blade[0].time += 0.001f * (Delta_Time);

    auto *mdl = static_cast<MODEL *>(core.GetEntityPointer(man));
    if (!mdl)
        return;

    auto *manNode = mdl->GetNode(0);
    rs->TextureSet(0, -1);
    rs->TextureSet(1, -1);
    rs->TextureSet(2, -1);
    rs->TextureSet(3, -1);

    glm::mat4 mtx;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(mtx));
    storm::math::invertRigid(mtx);
    storm::math::pos(mtx) = glm::vec3(0.0f);
    rs->SetTransform(D3DTS_TEXTURE1, storm::math::d3dMatrix(mtx));
    rs->SetRenderState(D3DRS_TEXTUREFACTOR, blendValue);

    //------------------------------------------------------
    // draw gun
    glm::mat4 perMtx;
    int32_t sti;
    auto *obj = static_cast<MODEL *>(core.GetEntityPointer(gun));
    if (obj != nullptr)
    {
        auto *gunNode = obj->GetNode(0);
        if ((blendValue & 0xff000000) == 0xff000000)
        {
            gunNode->SetTechnique("EnvAmmoShader");
        }
        else
        {
            gunNode->SetTechnique("AnimationBlend");
        }
        sti = -1;
        auto idGun = manNode->geo->FindName(gunLocName);

        if ((sti = manNode->geo->FindLabelN(sti + 1, idGun)) > -1)
        {
            auto *ani = mdl->GetAnimation();
            auto *bones = &ani->GetAnimationMatrix(0);

            GEOS::LABEL lb;
            manNode->geo->GetLabel(sti, lb);
            glm::mat4 mt;
            storm::math::vx(mt) = glm::vec3(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            storm::math::vy(mt) = glm::vec3(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            storm::math::vz(mt) = glm::vec3(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            storm::math::pos(mt) = glm::vec3(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            auto mbn = bones[lb.bones[0]] * mt;
            storm::math::pos(mbn).x *= -1.0f;
            storm::math::vx(mbn).x *= -1.0f;
            storm::math::vy(mbn).x *= -1.0f;
            storm::math::vz(mbn).x *= -1.0f;
            glm::mat4 scl;
            storm::math::vx(scl).x = -1.0f;
            storm::math::vy(scl).y = 1.0f;
            storm::math::vz(scl).z = 1.0f;
            mbn = glm::mat4(mbn) * scl;

            perMtx = mdl->mtx * mbn;
        }
        obj->mtx = perMtx;

        obj->ProcessStage(Stage::realize, 0);
    }

    //------------------------------------------------------
    // draw saber
    blade[0].DrawBlade(rs, blendValue, mdl, manNode);
    blade[1].DrawBlade(rs, blendValue, mdl, manNode);

    //------------------------------------------------------
    // draw tied items
    for (int32_t n = 0; n < ITEMS_INFO_QUANTITY; n++)
        if (items[n].nItemIndex != -1)
            items[n].DrawItem(rs, blendValue, mdl, manNode);

    mtx = glm::mat4(1.0f);
    rs->SetTransform(D3DTS_TEXTURE1, storm::math::d3dMatrix(mtx));
}

bool BLADE::LoadBladeModel(MESSAGE &message)
{
    const auto nBladeIdx = message.Long();
    if (nBladeIdx < 0 || nBladeIdx >= BLADE_INFO_QUANTITY)
        return false;

    man = message.EntityID();

    if (nBladeIdx == 1)
        blade[nBladeIdx].locatorName = sabergunBeltName;
    else
        blade[nBladeIdx].locatorName = beltName;

    return blade[nBladeIdx].LoadBladeModel(message);
}

bool BLADE::LoadGunModel(MESSAGE &message)
{
    core.EraseEntity(gun);
    man = message.EntityID();
    // model name
    const std::string &mdlName = message.String();
    if (!mdlName.empty())
    {
        // path of the model
        char path[256];
        strcpy_s(path, "Ammo\\");
        strcat_s(path, mdlName.c_str());
        // path of the textures
        auto *gs = static_cast<VGEOMETRY *>(core.GetService("geometry"));
        if (gs)
            gs->SetTexturePath("Ammo\\");
        // Create a model
        gun = core.CreateEntity("modelr");
        if (!core.Send_Message(gun, "ls", MSG_MODEL_LOAD_GEO, path))
        {
            core.EraseEntity(gun);
            if (gs)
                gs->SetTexturePath("");
            return false;
        }
        if (gs)
            gs->SetTexturePath("");
    }
    else
        return false;
    return true;
}

void BLADE::GunFire()
{
    auto *mdl = static_cast<MODEL *>(core.GetEntityPointer(man));
    auto *manNode = mdl->GetNode(0);

    //------------------------------------------------------
    // search gunfire
    glm::mat4 perMtx;
    int32_t sti;

    const char *currentGunLocName = gunLocName;

    auto *obj = static_cast<MODEL *>(core.GetEntityPointer(gun));
    if (obj == nullptr) // no pistol - look for saber pistol
    {
        obj = static_cast<MODEL *>(core.GetEntityPointer(blade[1].eid));
        currentGunLocName = blade[1].locatorName;
    }

    if (obj != nullptr)
    {
        auto *gunNode = obj->GetNode(0);
        sti = -1;
        auto idGun = manNode->geo->FindName(currentGunLocName);

        if ((sti = manNode->geo->FindLabelN(sti + 1, idGun)) > -1)
        {
            auto *ani = mdl->GetAnimation();
            auto *bones = &ani->GetAnimationMatrix(0);

            GEOS::LABEL lb;
            manNode->geo->GetLabel(sti, lb);
            glm::mat4 mt;
            storm::math::vx(mt) = glm::vec3(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            storm::math::vy(mt) = glm::vec3(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            storm::math::vz(mt) = glm::vec3(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            storm::math::pos(mt) = glm::vec3(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            auto mbn = bones[lb.bones[0]] * mt;
            storm::math::pos(mbn).x *= -1.0f;
            storm::math::vx(mbn).x *= -1.0f;
            storm::math::vy(mbn).x *= -1.0f;
            storm::math::vz(mbn).x *= -1.0f;
            perMtx = mdl->mtx * mbn;
        }

        // search for start
        sti = gunNode->geo->FindLabelN(0, gunNode->geo->FindName(gunFire));
        if (sti >= 0)
        {
            GEOS::LABEL lb;
            gunNode->geo->GetLabel(sti, lb);
            glm::mat4 resm;
            resm = (*(glm::mat4 *)&lb.m) * perMtx;
            auto rp = storm::math::transformPoint(perMtx, glm::vec3(lb.m[3][0], lb.m[3][1], lb.m[3][2]));

            core.Send_Message(core.GetEntityId("particles"), "lsffffffl", PS_CREATEX, "gunfire", rp.x, rp.y,
                              rp.z, storm::math::vz(resm).x, storm::math::vz(resm).y, storm::math::vz(resm).z, 0);
        }
        else
            core.Trace("MSG_BLADE_GUNFIRE Can't find gun_fire locator");
    }
}

uint64_t BLADE::ProcessMessage(MESSAGE &message)
{
    int32_t n;

    switch (message.Long())
    {
    case MSG_BLADE_SET:
        return LoadBladeModel(message);
        break;

    case MSG_BLADE_BELT:
        n = message.Long();
        if (n == 0)
        {
            blade[n].locatorName = beltName;
            blade[n].lifeTime = 0.0f;
        }
        else if (n == 1)
        {
            blade[n].locatorName = sabergunBeltName;
            blade[n].lifeTime = 0.0f;
        }
        // core.Trace("MSG_BLADE_BELT::%s", beltName);
        break;

    case MSG_BLADE_HAND:
        n = message.Long();
        if (n == 0)
        {
            blade[n].locatorName = handName;
        }
        else if (n == 1)
        {
            blade[n].locatorName = sabergunHandName;
        }
        // core.Trace("MSG_BLADE_HAND::%s", handName);
        break;

    case MSG_BLADE_GUNSET:
        return LoadGunModel(message);
        break;
    case MSG_BLADE_GUNBELT:
        gunLocName = gunBeltName;
        // core.Trace("MSG_BLADE_GUNBELT::%s", gunLocName);
        break;
    case MSG_BLADE_GUNHAND:
        gunLocName = gunHandName;
        // core.Trace("MSG_BLADE_GUNHAND::%s", gunLocName);
        break;
    case MSG_BLADE_GUNFIRE:
        GunFire();
        // core.Trace("MSG_BLADE_GUNFIRE::%s", handName);
        break;

    case MSG_BLADE_TRACE_ON:
        n = message.Long();
        if (n >= 0 && n < BLADE_INFO_QUANTITY)
        {
            blade[0].lifeTime = blade[0].defLifeTime;
        }
        // core.Trace("MSG_BLADE_TRACE_ON::%f", lifeTime);
        break;

    case MSG_BLADE_TRACE_OFF:
        n = message.Long();
        if (n >= 0 && n < BLADE_INFO_QUANTITY)
        {
            blade[0].lifeTime = 0.0f;
        }
        // core.Trace("MSG_BLADE_TRACE_OFF");
        break;

    case MSG_BLADE_BLOOD:
        // core.Trace("MSG_BLADE_BLOOD");
        break;

    case MSG_BLADE_LIGHT:
        // core.Trace("MSG_BLADE_LIGHT");
        break;
    case MSG_BLADE_ALPHA:
        blendValue = message.Long();
        break;

    case 1001:
        man = message.EntityID();
        AddTieItem(message);
        break;
    case 1002:
        DelTieItem(message);
        break;
    case 1003:
        DelAllTieItem();
        break;
    }
    return 0;
}

void BLADE::AddTieItem(MESSAGE &message)
{
    const auto nItemIdx = message.Long();

    const std::string &mdlName = message.String();
    const std::string &locName = message.String();

    auto n = FindTieItemByIndex(nItemIdx);
    if (n >= 0)
    {
        core.Trace("Warning! BLADE::AddTieItem(%d,%s,%s) already set that item", nItemIdx, mdlName.c_str(),
                   locName.c_str());
    }
    else
    {
        for (n = 0; n < ITEMS_INFO_QUANTITY; n++)
            if (items[n].nItemIndex == -1)
                break;
        if (n < ITEMS_INFO_QUANTITY)
        {
            items[n].nItemIndex = nItemIdx;
            items[n].LoadItemModel(mdlName.c_str(), locName.c_str());
        }
        else
        {
            core.Trace("Warning! BLADE::AddTieItem(%d,%s,%s) very mach items already set", nItemIdx, mdlName.c_str(),
                       locName.c_str());
        }
    }
}

void BLADE::DelTieItem(MESSAGE &message)
{
    const auto nItemIdx = message.Long();
    const int32_t n = FindTieItemByIndex(nItemIdx);
    if (n >= 0)
        items[n].Release();
}

void BLADE::DelAllTieItem()
{
    for (int32_t i = 0; i < ITEMS_INFO_QUANTITY; i++)
        if (items[i].nItemIndex != -1)
            items[i].Release();
}

int32_t BLADE::FindTieItemByIndex(int32_t n)
{
    if (n < 0)
        return -1;
    for (int32_t i = 0; i < ITEMS_INFO_QUANTITY; i++)
        if (items[i].nItemIndex == n)
            return i;
    return -1;
}

void BLADE::TIEITEM_INFO::Release()
{
    if (nItemIndex != -1)
    {
        nItemIndex = -1;
        core.EraseEntity(eid);
        delete locatorName;
        locatorName = nullptr;
    }
}

void BLADE::TIEITEM_INFO::DrawItem(VRenderer *rs, unsigned int blendValue, MODEL *mdl, NODE *manNode)
{
    auto *obj = static_cast<MODEL *>(core.GetEntityPointer(eid));
    if (obj != nullptr)
    {
        glm::mat4 perMtx;

        NODE *mdlNode = obj->GetNode(0);
        if ((blendValue & 0xff000000) == 0xff000000)
        {
            mdlNode->SetTechnique("EnvAmmoShader");
        }
        else
        {
            mdlNode->SetTechnique("AnimationBlend");
        }
        int32_t sti = -1;
        auto idLoc = manNode->geo->FindName(locatorName);

        if ((sti = manNode->geo->FindLabelN(sti + 1, idLoc)) > -1)
        {
            Animation *ani = mdl->GetAnimation();
            glm::mat4 *bones = &ani->GetAnimationMatrix(0);

            GEOS::LABEL lb;
            manNode->geo->GetLabel(sti, lb);
            glm::mat4 mt;
            storm::math::vx(mt) = glm::vec3(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            storm::math::vy(mt) = glm::vec3(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            storm::math::vz(mt) = glm::vec3(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            storm::math::pos(mt) = glm::vec3(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            glm::mat4 mbn = bones[lb.bones[0]] * mt;
            storm::math::pos(mbn).x *= -1.0f;
            storm::math::vx(mbn).x *= -1.0f;
            storm::math::vy(mbn).x *= -1.0f;
            storm::math::vz(mbn).x *= -1.0f;
            glm::mat4 scl;
            storm::math::vx(scl).x = -1.0f;
            storm::math::vy(scl).y = 1.0f;
            storm::math::vz(scl).z = 1.0f;
            mbn = glm::mat4(mbn) * scl;

            perMtx = mdl->mtx * mbn;
        }
        obj->mtx = perMtx;
        obj->ProcessStage(Stage::realize, 0);
    }
}

bool BLADE::TIEITEM_INFO::LoadItemModel(const char *mdlName, const char *locName)
{
    core.EraseEntity(eid);
    delete locatorName;
    locatorName = nullptr;

    if (!locName || !mdlName)
        return false;

    const auto len = strlen(locName) + 1;
    locatorName = new char[len];
    Assert(locatorName);
    memcpy(locatorName, locName, len);

    // path of the model
    char path[256];
    strcpy_s(path, "Ammo\\");
    strcat_s(path, mdlName);
    // path of the textures
    auto *gs = static_cast<VGEOMETRY *>(core.GetService("geometry"));
    if (gs)
        gs->SetTexturePath("Ammo\\");
    // Create a model
    eid = core.CreateEntity("modelr");
    if (!core.Send_Message(eid, "ls", MSG_MODEL_LOAD_GEO, path))
    {
        core.EraseEntity(eid);
        if (gs)
            gs->SetTexturePath("");
        return false;
    }
    if (gs)
        gs->SetTexturePath("");

    return true;
}
