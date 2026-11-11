/******************************************************************************
File:  shadow.cpp

Author:   Nick Chirkov
Copyright (C) 2000, 2001 Nick Chirkov

Comments:
dynamic shadow cpp file
******************************************************************************/
#include "shadow.h"
#include "live_list.hpp"
#include "storm/render/effects.hpp"
#ifdef __ANDROID__
#include "platform/android_graphics_effects.hpp"
#endif

#include "core.h"
#include "shared/messages.h"

CREATE_CLASS(Shadow)

static uint32_t HEAD_DENSITY = 0xFF606060;
static uint32_t DENSITY = 0xFF606040;
static const float nearBlend = 8.0f;
static const float farBlend = 16.0f;

static const int32_t vbuff_size = 1024;
static int32_t refcount = 0;
#define TEXTURE_SIZE 128
int32_t shTex = -1; // shared: the raw shadow map of the character being prepared (renderer texture id)
int32_t vbuff = -1;

// All shadows of a frame render their maps in one pass (prepareAll) at the first Shadow::Realize, each
// into its own blurred texture, so the frame leaves the back buffer once instead of once per character.
std::vector<Shadow *> Shadow::instances_;
uint32_t Shadow::epoch_ = 0, Shadow::preparedEpoch_ = UINT32_MAX;

namespace
{
int32_t createShadowTarget(VDX9RENDER *rs, D3DFORMAT format)
{
    return rs->TextureCreate(TEXTURE_SIZE, TEXTURE_SIZE, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT);
}

int32_t createShadowVertexBuffer(VDX9RENDER *rs)
{
    return rs->CreateVertexBuffer(SHADOW_FVF, sizeof(Shadow::SHADOW_VERTEX) * (vbuff_size + 128),
                                  D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);
}

void releaseShared(VDX9RENDER *rs)
{
    rs->ReleaseVertexBuffer(vbuff);
    vbuff = -1;
    rs->TextureRelease(shTex);
    shTex = -1;
}
} // namespace

Shadow::Shadow()
{
    shading = 1.0f;
    blendValue = 0xFFFFFFFF;
}

Shadow::~Shadow()
{
    std::erase(instances_, this);
    rs->TextureRelease(blurTex_);
    refcount--;
    if (refcount == 0)
        releaseShared(rs);
}

bool Shadow::Init()
{
#ifdef __ANDROID__
    (void)storm::android::CharacterShadowsEnabled();
#endif
    // GUARD(Shadow::SHADOW())

    col = static_cast<COLLIDE *>(core.GetService("coll"));
    if (col == nullptr)
        throw std::runtime_error("No service: COLLIDE");

    core.AddToLayer(REALIZE, GetId(), 900);

    rs = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
    if (!rs)
        throw std::runtime_error("No service: dx9render");

    if (refcount == 0)
    {
        shTex = createShadowTarget(rs, D3DFMT_R5G6B5);
        vbuff = createShadowVertexBuffer(rs);
    }
    refcount++;
    blurTex_ = createShadowTarget(rs, D3DFMT_X8R8G8B8);
    instances_.push_back(this);

    // UNGUARD
    return true;
}

glm::mat4 trans;
float perspective, atten_start, atten_end;
Shadow::SHADOW_VERTEX *shadvert;
int32_t tot_verts;
glm::vec3 lightPos, ObjPos, objPos;
glm::vec3 camPos;

bool AddPoly(const glm::vec3 *vr, int32_t nverts)
{
    const auto norm = storm::math::normalizeOrZero(glm::cross(vr[1] - vr[0], vr[2] - vr[0]));

    const auto d = glm::dot(norm, vr[0]);
    const auto dc = glm::dot(norm, camPos) - d;
    const auto dl = glm::dot(norm, lightPos) - d;
    if (dc * dl < 0.0f)
        return true;

    const auto d0 = glm::dot(norm, objPos) - d;
    if (d0 * dl < 0.0f)
        return true;

    if (tot_verts + (nverts - 2) * 3 > vbuff_size)
        return false;
    int32_t v;
    for (v = 0; v < 3; v++)
    {
        shadvert[tot_verts].pos = vr[v];
        const auto z = (trans[0][2] * vr[v].x + trans[1][2] * vr[v].y + trans[2][2] * vr[v].z + trans[3][2]);
        const auto rhw = perspective * 0.5f / z;
        shadvert[tot_verts].tu =
            rhw * (trans[0][0] * vr[v].x + trans[1][0] * vr[v].y + trans[2][0] * vr[v].z + trans[3][0]) + 0.5f;
        shadvert[tot_verts].tv =
            -rhw * (trans[0][1] * vr[v].x + trans[1][1] * vr[v].y + trans[2][1] * vr[v].z + trans[3][1]) + 0.5f;
        tot_verts++;
    }
    const auto start = tot_verts - 3;
    for (; v < nverts; v++)
    {
        shadvert[tot_verts + 0] = shadvert[start];
        shadvert[tot_verts + 1] = shadvert[tot_verts - 1];
        tot_verts += 2;
        shadvert[tot_verts].pos = vr[v];
        const auto z = (trans[0][2] * vr[v].x + trans[1][2] * vr[v].y + trans[2][2] * vr[v].z + trans[3][2]);
        const auto rhw = perspective * 0.5f / z;
        shadvert[tot_verts].tu =
            rhw * (trans[0][0] * vr[v].x + trans[1][0] * vr[v].y + trans[2][0] * vr[v].z + trans[3][0]) + 0.5f;
        shadvert[tot_verts].tv =
            -rhw * (trans[0][1] * vr[v].x + trans[1][1] * vr[v].y + trans[2][1] * vr[v].z + trans[3][1]) + 0.5f;
        tot_verts++;
    }
    return true;
}

//------------------------------------------------------------------------------------
// realize
//------------------------------------------------------------------------------------
void Shadow::Realize(uint32_t Delta_Time)
{
    auto *obj = static_cast<MODEL *>(core.GetEntityPointer(entity));
    if (!obj)
        return;
    // fx contact shadow (renderer_bgfx/fx): a soft blob on the floor at the character's feet, besides this shadow.
    if (storm::render::CurrentEffectsWorld().contact_shadows)
    {
        const glm::vec3 feet = storm::math::pos(obj->mtx);
        storm::render::EffectsMatrix view;
        rs->GetTransform(D3DTS_VIEW, reinterpret_cast<D3DMATRIX *>(view.data()));
        storm::render::EffectsContactShadow({feet.x, feet.y, feet.z}, view);
    }
#ifdef __ANDROID__
    // Skip shadow rendering, blur AND projection together, before touching render state.
    // No other pass consumes the retained private shadow textures.
    if (!storm::android::CharacterShadowsEnabled()) return;
#endif
    // Each shadow is realized once per frame: meeting one that already ran in this epoch starts a new frame.
    if (preparedEpoch_ != epoch_ || lastEpoch_ == epoch_)
    {
        if (lastEpoch_ == epoch_)
            ++epoch_;
        prepareAll(Delta_Time);
        preparedEpoch_ = epoch_;
    }
    lastEpoch_ = epoch_;
    if (prepared_.visible)
        project();
}

void Shadow::prepareAll(uint32_t Delta_Time)
{
    glm::mat4 prev_view, prev_proj;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(prev_view));
    rs->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prev_proj));
    D3DVIEWPORT9 vp;
    rs->GetViewport(&vp);
    // prepare() pushes the frame's render target before it leaves it (sceneOpen goes false).
    bool sceneOpen = true;
    // prepare() runs script events and the character's realize, which may create or delete shadows.
    storm::shadow::forEachLive(instances_, [&](Shadow *shadow) { shadow->prepare(Delta_Time, prev_view, prev_proj, sceneOpen); });
    if (!sceneOpen)
    {
        rs->PopRenderTarget();
        rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(prev_view));
        rs->SetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prev_proj));
        rs->BeginScene();
    }
    rs->SetViewport(&vp);
}

void Shadow::prepare(uint32_t Delta_Time, const glm::mat4 &visView, const glm::mat4 &visPoj, bool &sceneOpen)
{
    prepared_.visible = false;
    auto *obj = static_cast<MODEL *>(core.GetEntityPointer(entity));
    if (!obj)
        return;

    auto *pV = core.Event("EWhr_GetShadowDensity");
    HEAD_DENSITY = ((VDATA *)pV->GetArrayElement(0))->GetInt();
    DENSITY = ((VDATA *)pV->GetArrayElement(1))->GetInt();

    pV = core.Event("EWhr_GetFogDensity");
    auto fogDensity = pV->GetFloat();

    auto *node = obj->GetNode(0);
    GEOS::INFO gi;
    node->geo->GetInfo(gi);
    objPos = storm::math::pos(obj->mtx);
    ObjPos = objPos;
    objPos.y += gi.radius;
    auto headPos = objPos;
    headPos.y += gi.radius;

    D3DLIGHT9 dLight;
    BOOL bOk = false;
    rs->GetLightEnable(0, &bOk);
    if (bOk)
        rs->GetLight(0, &dLight);
    else
        return;

    dLight.Position.y = lightPos.y;

    auto light_pos = storm::math::normalizeOrZero(glm::vec3(dLight.Direction.x, dLight.Direction.y, dLight.Direction.z));
    if (light_pos.y > -0.6f)
        light_pos.y = -0.6f;
    light_pos = -1000.0f * (storm::math::normalizeOrZero(light_pos));

    light_pos = objPos - storm::math::normalizeOrZero(objPos - light_pos) * 100.0f;

    glm::vec3 dir = storm::math::normalizeOrZero(objPos - light_pos);
    lightPos = light_pos;

    // check visibility of shadow
    camPos.x = -visView[3][0] * visView[0][0] - visView[3][1] * visView[0][1] - visView[3][2] * visView[0][2];
    camPos.y = -visView[3][0] * visView[1][0] - visView[3][1] * visView[1][1] - visView[3][2] * visView[1][2];
    camPos.z = -visView[3][0] * visView[2][0] - visView[3][1] * visView[2][1] - visView[3][2] * visView[2][2];

    FindPlanes(visView, visPoj);

    const auto its = core.GetEntityIds(SHADOW);

    glm::vec3 hdest = headPos + storm::math::normalizeOrZero(headPos - light_pos) * 100.0f;
    float ray = col->Trace(its, headPos, hdest, nullptr, 0);
    glm::vec3 cen;
    float radius;
    if (ray <= 1.0f)
    {
        if (ray < 4.0f / 100.0f)
            ray = 4.0f / 100.0f;
        if (ray > 10.0f / 100.0f)
            ray = 10.0f / 100.0f;
        cen = headPos + 0.5f * ray * (hdest - headPos);
        radius = ray * 50.0f;
    }
    else
    {
        cen = headPos + 4.0f * storm::math::normalizeOrZero(hdest - headPos);
        radius = 8.0f;
    }

    int32_t p;
    for (p = 0; p < 4; p++)
    {
        float dist = cen.x * planes[p].Nx + cen.y * planes[p].Ny + cen.z * planes[p].Nz - planes[p].D;
        if (dist > radius)
            break;
    }
    if (p < 4)
    {
        return;
    }

    float minVal = 0.0f;
    for (int32_t it = 0; it < 10; it++)
    {
        glm::vec3 ps = ObjPos;
        ps.y += gi.radius * 0.111f * static_cast<float>(it);
        if (col->Trace(its, ps, lightPos, nullptr, 0) > 1.0f)
            minVal += 0.1f;
    }

    float dtime = Delta_Time * 0.001f;
    if (minVal <= 0.5f)
        shading -= dtime;
    else
        shading += dtime;

    shading = std::max(0.2f, std::max(minVal, std::min(shading, 1.0f)));
    shading *= (blendValue >> 24) / 255.0f;

    {
        float dist = sqrtf(storm::math::lengthSquared(cen - camPos));
        if (dist > farBlend) // too far
        {
            return;
        }
        if (dist > nearBlend) // blend
            shading *= 1.0f - (dist - nearBlend) / (farBlend - nearBlend);
    }

    // view matrix-------------------------------------
    glm::mat4 lightmtx;
    storm::math::viewMatrix(lightmtx, glm::vec3(0.0f, 0.0f, 0.0f), dir, glm::vec3(0.0f, 1.0f, 0.0f));
    storm::math::setInversePosition(lightmtx, light_pos.x, light_pos.y, light_pos.z);
    // projection matrix-------------------------------------
    glm::mat4 proj;
    float tanfov = sqrtf(storm::math::lengthSquared(objPos - light_pos)) / (1.2f * gi.radius);
    float fov = 2.0f * atanf(1.0f / tanfov);

    proj = storm::math::projectionMatrix(fov, TEXTURE_SIZE, TEXTURE_SIZE, 0.1f, 1000.0f);
    // the projector's frustum: planes 0-3 of the clip in project()
    FindPlanes(lightmtx, proj);
    std::copy(planes, planes + 4, prepared_.planes);

    //---------------------------------------------------------------
    // draw object into the shared shadow map, blur it into this shadow's texture
    if (sceneOpen)
    {
        rs->EndScene();
        rs->PushRenderTarget();
        sceneOpen = false;
    }
    rs->SetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(proj));
    rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(lightmtx));

    rs->SetRenderTarget(shTex, 0, 0, {});

    rs->Clear(0L, nullptr, D3DCLEAR_TARGET, 0, 0.0f, 0L);
    rs->BeginScene();

    rs->SetRenderState(D3DRS_TEXTUREFACTOR, DENSITY);
    char tech[256];
    strcpy_s(tech, node->GetTechnique());
    node->SetTechnique("shadow_model");
    node->flags &= ~NODE::VISIBLE_TREE;

    rs->SetRenderState(D3DRS_ZENABLE, FALSE);
    obj->ProcessStage(Stage::realize, 0);
    rs->SetRenderState(D3DRS_ZENABLE, TRUE);

    node->flags |= NODE::VISIBLE_TREE;
    node->SetTechnique(tech);

    rs->EndScene();

    Smooth();

    // what the projection in Realize needs (planes were stored above)
    Prepared &out = prepared_;
    out.visible = true;
    out.trans = lightmtx;
    out.perspective = std::max(proj[0][0], proj[1][1]);
    out.lightmtx = lightmtx;
    out.dir = dir;
    out.cen = cen;
    out.radius = radius;
    out.objPos = objPos;
    out.ObjPos = ObjPos;
    out.lightPos = lightPos;
    out.camPos = camPos;
    float dist = 3.0f * sqrtf(storm::math::lengthSquared(cen - camPos));
    shading *= powf(2.71f, -fogDensity * dist);
    out.shade = static_cast<int32_t>(fabsf(shading * 255.0f));
}

void Shadow::project()
{
    const Prepared &in = prepared_;
    // AddPoly reads these while col->Clip runs
    trans = in.trans;
    perspective = in.perspective;
    atten_start = 1200.0f;
    atten_end = 1300.0f;
    objPos = in.objPos;
    ObjPos = in.ObjPos;
    lightPos = in.lightPos;
    camPos = in.camPos;

    glm::mat4 lightmtx = in.lightmtx;
    std::copy(in.planes, in.planes + 4, planes);

    // create last plane
    storm::math::transpose3x3(lightmtx);
    glm::vec3 pdir = -storm::math::normalizeOrZero(glm::vec3(in.dir.x, 0.0f, in.dir.z));
    planes[4].Nx = pdir.x;
    planes[4].Ny = pdir.y;
    planes[4].Nz = pdir.z;
    planes[4].D = glm::dot(pdir, objPos + 0.5f * pdir);
    planes[5].Nx = -pdir.x;
    planes[5].Ny = -pdir.y;
    planes[5].Nz = -pdir.z;
    planes[5].D = glm::dot(-pdir, objPos - 3.5f * pdir);

    rs->TextureSet(0, blurTex_);
    rs->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(glm::mat4()));

    rs->SetRenderState(D3DRS_TEXTUREFACTOR, (in.shade << 16) | (in.shade << 8) | (in.shade << 0));
    const auto its = core.GetEntityIds(SHADOW);
    tot_verts = 0;
    shadvert = static_cast<SHADOW_VERTEX *>(rs->LockVertexBuffer(vbuff, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK));
    if (!shadvert)
        return;
    col->Clip(its, &planes[0], 5, in.cen, in.radius, AddPoly, &entity, 1);

    rs->UnLockVertexBuffer(vbuff);

    if (tot_verts >= 3)
        rs->DrawPrimitive(D3DPT_TRIANGLELIST, vbuff, sizeof(SHADOW_VERTEX), 0, tot_verts / 3, "shadow_draw");
}

void Shadow::FindPlanes(const glm::mat4 &view, const glm::mat4 &proj)
{
    glm::vec3 v[4];
    // left
    v[0].x = proj[0][0];
    v[0].y = 0.0f;
    v[0].z = 1.0f;
    // right
    v[1].x = -proj[0][0];
    v[1].y = 0.0f;
    v[1].z = 1.0f;
    // top
    v[2].x = 0.0f;
    v[2].y = -proj[1][1];
    v[2].z = 1.0f;
    // bottom
    v[3].x = 0.0f;
    v[3].y = proj[1][1];
    v[3].z = 1.0f;
    v[0] = storm::math::normalizeOrZero(v[0]);
    v[1] = storm::math::normalizeOrZero(v[1]);
    v[2] = storm::math::normalizeOrZero(v[2]);
    v[3] = storm::math::normalizeOrZero(v[3]);

    glm::vec3 pos;

    pos.x = -view[3][0] * view[0][0] - view[3][1] * view[0][1] - view[3][2] * view[0][2];
    pos.y = -view[3][0] * view[1][0] - view[3][1] * view[1][1] - view[3][2] * view[1][2];
    pos.z = -view[3][0] * view[2][0] - view[3][1] * view[2][1] - view[3][2] * view[2][2];

    planes[0].Nx = v[0].x * view[0][0] + v[0].y * view[0][1] + v[0].z * view[0][2];
    planes[0].Ny = v[0].x * view[1][0] + v[0].y * view[1][1] + v[0].z * view[1][2];
    planes[0].Nz = v[0].x * view[2][0] + v[0].y * view[2][1] + v[0].z * view[2][2];

    planes[1].Nx = v[1].x * view[0][0] + v[1].y * view[0][1] + v[1].z * view[0][2];
    planes[1].Ny = v[1].x * view[1][0] + v[1].y * view[1][1] + v[1].z * view[1][2];
    planes[1].Nz = v[1].x * view[2][0] + v[1].y * view[2][1] + v[1].z * view[2][2];

    planes[2].Nx = v[2].x * view[0][0] + v[2].y * view[0][1] + v[2].z * view[0][2];
    planes[2].Ny = v[2].x * view[1][0] + v[2].y * view[1][1] + v[2].z * view[1][2];
    planes[2].Nz = v[2].x * view[2][0] + v[2].y * view[2][1] + v[2].z * view[2][2];

    planes[3].Nx = v[3].x * view[0][0] + v[3].y * view[0][1] + v[3].z * view[0][2];
    planes[3].Ny = v[3].x * view[1][0] + v[3].y * view[1][1] + v[3].z * view[1][2];
    planes[3].Nz = v[3].x * view[2][0] + v[3].y * view[2][1] + v[3].z * view[2][2];

    planes[0].Nx = -planes[0].Nx;
    planes[0].Ny = -planes[0].Ny;
    planes[0].Nz = -planes[0].Nz;
    planes[1].Nx = -planes[1].Nx;
    planes[1].Ny = -planes[1].Ny;
    planes[1].Nz = -planes[1].Nz;
    planes[2].Nx = -planes[2].Nx;
    planes[2].Ny = -planes[2].Ny;
    planes[2].Nz = -planes[2].Nz;
    planes[3].Nx = -planes[3].Nx;
    planes[3].Ny = -planes[3].Ny;
    planes[3].Nz = -planes[3].Nz;

    planes[0].D = (pos.x * planes[0].Nx + pos.y * planes[0].Ny + pos.z * planes[0].Nz);
    planes[1].D = (pos.x * planes[1].Nx + pos.y * planes[1].Ny + pos.z * planes[1].Nz);
    planes[2].D = (pos.x * planes[2].Nx + pos.y * planes[2].Ny + pos.z * planes[2].Nz);
    planes[3].D = (pos.x * planes[3].Nx + pos.y * planes[3].Ny + pos.z * planes[3].Nz);
}

void Shadow::Smooth()
{
    struct SMOOTHVRT
    {
        float sx, sy, sz, rhw;
        int32_t diffuse;
        float tu, tv;
    };

    float dt = 0.0f;
    SMOOTHVRT vrt[4];
    vrt[0].sx = 0.0f;
    vrt[0].sy = 0.0f;
    vrt[0].sz = 0.5f;
    vrt[0].rhw = 0.5f;
    vrt[0].tu = 0.0f;
    vrt[0].tv = 0.0f;
    vrt[1].sx = 0.0f;
    vrt[1].sy = TEXTURE_SIZE - 1.0f;
    vrt[1].sz = 0.5f;
    vrt[1].rhw = 0.5f;
    vrt[1].tu = 0.0f;
    vrt[1].tv = 1.0f;
    vrt[2].sx = TEXTURE_SIZE - 1.0f;
    vrt[2].sy = TEXTURE_SIZE - 1.0f;
    vrt[2].sz = 0.5f;
    vrt[2].rhw = 0.5f;
    vrt[2].tu = 1.0f;
    vrt[2].tv = 1.0f;
    vrt[3].sx = TEXTURE_SIZE - 1.0f;
    vrt[3].sy = 0.0f;
    vrt[3].sz = 0.5f;
    vrt[3].rhw = 0.5f;
    vrt[3].tu = 1.0f;
    vrt[3].tv = 0.0f;
    vrt[0].diffuse = vrt[3].diffuse = HEAD_DENSITY;
    vrt[1].diffuse = vrt[2].diffuse = 0xFFFFFFFF;

    rs->SetRenderTarget(blurTex_, 0, 0, {});

    rs->Clear(0L, nullptr, D3DCLEAR_TARGET, 0, 0.0f, 0L);
    rs->BeginScene();

    rs->TextureSet(0, shTex);

    if (rs->TechniqueExecuteStart("shadow_smooth"))
        do
        {
            static const int32_t nIterations = 3;

            for (int32_t u = 0; u < nIterations; u++)
                for (int32_t v = 0; v < nIterations; v++)
                {
                    const float ud = 1.5f * (u / (nIterations - 1.0f) - 0.5f) / (TEXTURE_SIZE - 1.0f) * 2.0f;
                    const float vd = 1.5f * (v / (nIterations - 1.0f) - 0.5f) / (TEXTURE_SIZE - 1.0f) * 2.0f;
                    vrt[0].tu = 0.0f + ud;
                    vrt[0].tv = 0.0f + vd;
                    vrt[1].tu = 0.0f + ud;
                    vrt[1].tv = 1.0f + vd;
                    vrt[2].tu = 1.0f + ud;
                    vrt[2].tv = 1.0f + vd;
                    vrt[3].tu = 1.0f + ud;
                    vrt[3].tv = 0.0f + vd;
                    const int32_t col = static_cast<int32_t>(1.0f / (nIterations * nIterations) * 255.0f);
                    rs->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFF000000 | (col << 16) | (col << 8) | (col << 0));
                    rs->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,
                                        D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_DIFFUSE | D3DFVF_TEXTUREFORMAT2, 2, &vrt,
                                        sizeof(SMOOTHVRT));
                }
        } while (rs->TechniqueExecuteNext());

    rs->EndScene();
}

uint64_t Shadow::ProcessMessage(MESSAGE &message)
{
    const int32_t code = message.Long();
    switch (code)
    {
    case 0:
        entity = message.EntityID();
        break;

    case MSG_BLADE_ALPHA:
        blendValue = message.Long();
        break;
    }
    return 0;
}

void Shadow::LostRender()
{
    rs->TextureRelease(blurTex_);
    blurTex_ = -1;
    if (--refcount == 0)
        releaseShared(rs);
}

void Shadow::RestoreRender()
{
    if (refcount++ == 0)
    {
        shTex = createShadowTarget(rs, D3DFMT_R5G6B5);
        vbuff = createShadowVertexBuffer(rs);
    }
    blurTex_ = createShadowTarget(rs, D3DFMT_X8R8G8B8);
}
