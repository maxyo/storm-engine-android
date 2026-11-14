/******************************************************************************
File:  shadow.cpp

Author:   Nick Chirkov
Copyright (C) 2000, 2001 Nick Chirkov

Comments:
dynamic shadow cpp file
******************************************************************************/
#include "shadow.h"
#include "live_list.hpp"
#include "polygon_clip.hpp"
#include "storm/render/effects.hpp"

#include "core.h"
#include "shared/messages.h"

#include <algorithm>
#include <cmath>

CREATE_CLASS(Shadow)

static uint32_t HEAD_DENSITY = 0xFF606060;
static uint32_t DENSITY = 0xFF606040;
static const float nearBlend = 8.0f;
static const float farBlend = 16.0f;

static const int32_t vbuff_size = 1024;
static int32_t refcount = 0;
#define TEXTURE_SIZE 128
// The shadow maps of a frame share two atlases (raw model, blurred), one tile per visible shadow: two render-target
// switches per frame instead of two per shadow. A tile is TEXTURE_SIZE square inside a cell with a cleared gutter,
// so the blur's ±1.5 texel taps and the projection's bilinear reads stay black at the edge, as at the border of the
// former per-shadow targets.
static constexpr int32_t kAtlasGutter = 2;
static constexpr int32_t kAtlasCell = TEXTURE_SIZE + 2 * kAtlasGutter;
static constexpr int32_t kAtlasColumns = 8;
static constexpr int32_t kAtlasWidth = kAtlasCell * kAtlasColumns;
int32_t shAtlas = -1;    // shared: the models of this frame's visible shadows (renderer texture id)
int32_t blurAtlas = -1;  // shared: their blurred maps, read by project()
int32_t atlasRows = 0;   // rows of both atlases (kAtlasColumns tiles each)
int32_t vbuff = -1;

// All shadows of a frame render their maps in one pass (prepareAll) at the first Shadow::Realize, each
// into its own blurred texture, so the frame leaves the back buffer once instead of once per character.
std::vector<Shadow *> Shadow::instances_;
uint32_t Shadow::epoch_ = 0, Shadow::preparedEpoch_ = UINT32_MAX;
float Shadow::fogDensity_ = 0.0f;
// Frames a shadow keeps its occlusion rays while neither it nor the light moved (other characters walking through the
// rays are seen this much later; the shading fades at 1 per second anyway).
static constexpr uint32_t kRayRefresh = 8;
// A character that moved this far (metres), or a light that turned this much (length of the direction difference),
// wants new rays.
static constexpr float kRayMove = 0.25f;
static constexpr float kRayTurn = 0.02f;
// Shadows granted a ray cast (head ray and ten body rays) per frame, nearest to the camera first; a shadow whose rays
// are kRayMaxAge frames old is granted one whatever the budget.
static constexpr size_t kRaysPerFrame = 4;
static constexpr uint32_t kRayMaxAge = 16;
// The fan of polygons (Shadow::Fan): widening of the projector's volume (metres), the distance from the character at
// which a turn of the light moves the volume as far as a step of the character, and the frames after which the
// location is read again (for models that move).
static constexpr float kFanMargin = 1.0f;
static constexpr float kFanReach = 16.0f;
static constexpr uint32_t kFanRefresh = 20;
// How far from a head position its shadow's sphere (centre, radius) can reach (prepare: a blocked head ray gives at
// most 5 + 5, a free one 4 + 8).
static constexpr float kShadowReach = 12.0f;

namespace
{
void releaseAtlases(VRenderer *rs)
{
    if (shAtlas >= 0)
        rs->TextureRelease(shAtlas);
    if (blurAtlas >= 0)
        rs->TextureRelease(blurAtlas);
    shAtlas = blurAtlas = -1;
    atlasRows = 0;
}

// Room for `tiles` shadows; the atlases grow by rows (a busy boarding or town square) and stay at that size.
void ensureAtlases(VRenderer *rs, int32_t tiles)
{
    const int32_t rows = (tiles + kAtlasColumns - 1) / kAtlasColumns;
    if (rows <= atlasRows && shAtlas >= 0)
        return;
    releaseAtlases(rs);
    atlasRows = std::max(rows, 2);
    const int32_t height = atlasRows * kAtlasCell;
    shAtlas = rs->TextureCreate(kAtlasWidth, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R5G6B5, D3DPOOL_DEFAULT);
    blurAtlas = rs->TextureCreate(kAtlasWidth, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT);
    if (shAtlas < 0 || blurAtlas < 0)
        throw std::runtime_error("Shadow: could not create the shadow atlases");
    core.Trace("Shadow: atlas %dx%d (%d tiles)", kAtlasWidth, height, atlasRows * kAtlasColumns);
}

// Top-left pixel of a tile's map in the atlases.
void tileOrigin(int32_t tile, int32_t &x, int32_t &y)
{
    x = (tile % kAtlasColumns) * kAtlasCell + kAtlasGutter;
    y = (tile / kAtlasColumns) * kAtlasCell + kAtlasGutter;
}

int32_t createShadowVertexBuffer(VRenderer *rs)
{
    return rs->CreateVertexBuffer(SHADOW_FVF, sizeof(Shadow::SHADOW_VERTEX) * (vbuff_size + 128),
                                  D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);
}

void releaseShared(VRenderer *rs)
{
    rs->ReleaseVertexBuffer(vbuff);
    vbuff = -1;
    releaseAtlases(rs);
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
    refcount--;
    if (refcount == 0)
        releaseShared(rs);
}

bool Shadow::Init()
{
    // GUARD(Shadow::SHADOW())

    col = static_cast<COLLIDE *>(core.GetService("coll"));
    if (col == nullptr)
        throw std::runtime_error("No service: COLLIDE");

    core.AddToLayer(REALIZE, GetId(), 900);

    rs = static_cast<VRenderer *>(core.GetService("renderer"));
    if (!rs)
        throw std::runtime_error("No service: renderer");

    if (refcount == 0)
        vbuff = createShadowVertexBuffer(rs);
    refcount++;
    instances_.push_back(this);

    // UNGUARD
    return true;
}

glm::mat4 trans;
float perspective, atten_start, atten_end;
float atlasU0, atlasV0, atlasSU, atlasSV; // the projected shadow's tile in the blurred atlas
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
            atlasU0 + atlasSU * (rhw * (trans[0][0] * vr[v].x + trans[1][0] * vr[v].y + trans[2][0] * vr[v].z + trans[3][0]) + 0.5f);
        shadvert[tot_verts].tv =
            atlasV0 + atlasSV * (-rhw * (trans[0][1] * vr[v].x + trans[1][1] * vr[v].y + trans[2][1] * vr[v].z + trans[3][1]) + 0.5f);
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
            atlasU0 + atlasSU * (rhw * (trans[0][0] * vr[v].x + trans[1][0] * vr[v].y + trans[2][0] * vr[v].z + trans[3][0]) + 0.5f);
        shadvert[tot_verts].tv =
            atlasV0 + atlasSV * (-rhw * (trans[0][1] * vr[v].x + trans[1][1] * vr[v].y + trans[2][1] * vr[v].z + trans[3][1]) + 0.5f);
        tot_verts++;
    }
    return true;
}

namespace
{
// The polygons of a fan being recorded (Shadow::rebuildFan) and the most of them kept.
std::vector<glm::vec3> *recordVerts = nullptr;
std::vector<uint8_t> *recordCounts = nullptr;
std::vector<glm::vec4> *recordPlanes = nullptr;
constexpr size_t kFanMaxVerts = 16384;

// Collision clip callback: keeps the polygon instead of drawing it.
bool recordPoly(const glm::vec3 *vr, int32_t nverts)
{
    if (recordVerts->size() + nverts > kFanMaxVerts)
        return false;
    recordVerts->insert(recordVerts->end(), vr, vr + nverts);
    recordCounts->push_back(static_cast<uint8_t>(nverts));
    // as AddPoly and the location's tree see the polygon's plane
    const auto normal = storm::math::normalizeOrZero(glm::cross(vr[1] - vr[0], vr[2] - vr[0]));
    recordPlanes->emplace_back(normal, glm::dot(normal, vr[0]));
    return true;
}
} // namespace

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
    // Interiors with fx lamp shadows: the character's shadow comes from the room's lamps (renderer_bgfx effects).
    if (storm::render::CurrentEffectsWorld().lamp_character_shadows)
        return;
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
    // The weather's shadow and fog densities are the same for every shadow of the frame.
    auto *pV = core.Event("EWhr_GetShadowDensity");
    HEAD_DENSITY = ((VDATA *)pV->GetArrayElement(0))->GetInt();
    DENSITY = ((VDATA *)pV->GetArrayElement(1))->GetInt();
    fogDensity_ = core.Event("EWhr_GetFogDensity")->GetFloat();
    // Which shadows are in view, and which of them are due for new rays: the nearest few get them now.
    std::vector<Shadow *> due;
    storm::shadow::forEachLive(instances_, [&](Shadow *shadow) {
        shadow->setup(prev_view, prev_proj);
        shadow->rayGranted_ = false;
        if (shadow->pending_ && shadow->raysDue())
            due.push_back(shadow);
    });
    std::sort(due.begin(), due.end(), [](const Shadow *a, const Shadow *b) { return a->camDist2_ < b->camDist2_; });
    for (size_t i = 0; i < due.size(); i++)
        due[i]->rayGranted_ = i < kRaysPerFrame || due[i]->rays_.age >= kRayMaxAge;
    int32_t tiles = 0;
    storm::shadow::forEachLive(instances_, [&](Shadow *shadow) {
        shadow->finish(Delta_Time);
        if (shadow->prepared_.visible)
            shadow->prepared_.tile = tiles++;
    });
    if (tiles > 0)
    {
        ensureAtlases(rs, tiles);
        rs->EndScene();
        rs->PushRenderTarget();
        // Pass 1: every visible character's model into its tile of the raw atlas.
        rs->SetRenderTarget(shAtlas, 0, 0, {});
        rs->Clear(0L, nullptr, D3DCLEAR_TARGET, 0, 0.0f, 0L);
        rs->BeginScene();
        rs->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        // The character's realize may create or delete shadows.
        storm::shadow::forEachLive(instances_, [&](Shadow *shadow) {
            if (shadow->prepared_.visible)
                shadow->renderModel();
        });
        rs->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        rs->EndScene();
        // Pass 2: the blur of every tile into the blurred atlas.
        rs->SetRenderTarget(blurAtlas, 0, 0, {});
        rs->Clear(0L, nullptr, D3DCLEAR_TARGET, 0, 0.0f, 0L);
        rs->BeginScene();
        rs->TextureSet(0, shAtlas);
        storm::shadow::forEachLive(instances_, [&](Shadow *shadow) {
            if (shadow->prepared_.visible)
                shadow->Smooth();
        });
        rs->EndScene();
        rs->PopRenderTarget();
        rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(prev_view));
        rs->SetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prev_proj));
        rs->BeginScene();
    }
    rs->SetViewport(&vp);
}

// The cheap part of a shadow's frame: where the character and the light are, and whether the shadow can be in view
// at all (its sphere is within kShadowReach of the head, so no ray is cast for a character far off screen).
void Shadow::setup(const glm::mat4 &visView, const glm::mat4 &visPoj)
{
    prepared_.visible = false;
    pending_ = false;
    auto *obj = static_cast<MODEL *>(core.GetEntityPointer(entity));
    if (!obj)
        return;

    auto *node = obj->GetNode(0);
    GEOS::INFO gi;
    node->geo->GetInfo(gi);
    Setup &st = setup_;
    st.radius = gi.radius;
    st.ObjPos = storm::math::pos(obj->mtx);
    st.objPos = st.ObjPos;
    st.objPos.y += gi.radius;
    st.headPos = st.objPos;
    st.headPos.y += gi.radius;

    D3DLIGHT9 dLight;
    BOOL bOk = false;
    rs->GetLightEnable(0, &bOk);
    if (bOk)
        rs->GetLight(0, &dLight);
    else
        return;

    auto light_pos = storm::math::normalizeOrZero(glm::vec3(dLight.Direction.x, dLight.Direction.y, dLight.Direction.z));
    if (light_pos.y > -0.6f)
        light_pos.y = -0.6f;
    light_pos = -1000.0f * (storm::math::normalizeOrZero(light_pos));

    light_pos = st.objPos - storm::math::normalizeOrZero(st.objPos - light_pos) * 100.0f;

    st.dir = storm::math::normalizeOrZero(st.objPos - light_pos);
    st.lightPos = light_pos;

    // check visibility of shadow
    st.camPos.x = -visView[3][0] * visView[0][0] - visView[3][1] * visView[0][1] - visView[3][2] * visView[0][2];
    st.camPos.y = -visView[3][0] * visView[1][0] - visView[3][1] * visView[1][1] - visView[3][2] * visView[1][2];
    st.camPos.z = -visView[3][0] * visView[2][0] - visView[3][1] * visView[2][1] - visView[3][2] * visView[2][2];

    camDist2_ = storm::math::lengthSquared(st.headPos - st.camPos);
    const float farReach = farBlend + kShadowReach;
    if (camDist2_ > farReach * farReach)
        return;
    FindPlanes(visView, visPoj);
    for (int32_t p = 0; p < 4; p++)
    {
        const float dist = st.headPos.x * planes[p].Nx + st.headPos.y * planes[p].Ny + st.headPos.z * planes[p].Nz - planes[p].D;
        if (dist > kShadowReach)
            return;
    }
    pending_ = true;
}

// New rays are wanted when there are none, when the character, the light or the model size moved since they were
// cast, or when they are kRayRefresh frames old.
bool Shadow::raysDue() const
{
    const Rays &r = rays_;
    return r.headRay < 0.0f || r.radius != setup_.radius || r.age >= kRayRefresh ||
           storm::math::lengthSquared(r.ObjPos - setup_.ObjPos) > kRayMove * kRayMove ||
           storm::math::lengthSquared(r.dir - setup_.dir) > kRayTurn * kRayTurn;
}

void Shadow::finish(uint32_t Delta_Time)
{
    if (!pending_)
        return;
    const Setup &st = setup_;
    const auto &its = core.GetEntityIds(SHADOW);

    // The first rays are cast whatever the budget; later ones when granted. The refreshes of shadows that stand still
    // are spread over the frames.
    const bool first = rays_.headRay < 0.0f;
    if (first || rayGranted_)
    {
        const uint32_t age =
            first ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this) / sizeof(void *)) % kRayRefresh : 0;
        rays_ = Rays{};
        rays_.ObjPos = st.ObjPos;
        rays_.dir = st.dir;
        rays_.radius = st.radius;
        rays_.age = age;
    }
    else
        rays_.age++;

    glm::vec3 hdest = st.headPos + storm::math::normalizeOrZero(st.headPos - st.lightPos) * 100.0f;
    if (rays_.headRay < 0.0f)
        rays_.headRay = col->Trace(its, st.headPos, hdest, nullptr, 0);
    float ray = rays_.headRay;
    glm::vec3 cen;
    float radius;
    if (ray <= 1.0f)
    {
        if (ray < 4.0f / 100.0f)
            ray = 4.0f / 100.0f;
        if (ray > 10.0f / 100.0f)
            ray = 10.0f / 100.0f;
        cen = st.headPos + 0.5f * ray * (hdest - st.headPos);
        radius = ray * 50.0f;
    }
    else
    {
        cen = st.headPos + 4.0f * storm::math::normalizeOrZero(hdest - st.headPos);
        radius = 8.0f;
    }

    // The view's planes of setup() are still this shadow's own; the projector's frustum below replaces them.
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

    if (rays_.minVal < 0.0f)
    {
        float minVal = 0.0f;
        for (int32_t it = 0; it < 10; it++)
        {
            glm::vec3 ps = st.ObjPos;
            ps.y += st.radius * 0.111f * static_cast<float>(it);
            if (col->Trace(its, ps, st.lightPos, nullptr, 0) > 1.0f)
                minVal += 0.1f;
        }
        rays_.minVal = minVal;
    }
    const float minVal = rays_.minVal;

    float dtime = Delta_Time * 0.001f;
    if (minVal <= 0.5f)
        shading -= dtime;
    else
        shading += dtime;

    shading = std::max(0.2f, std::max(minVal, std::min(shading, 1.0f)));
    shading *= (blendValue >> 24) / 255.0f;

    {
        float dist = sqrtf(storm::math::lengthSquared(cen - st.camPos));
        if (dist > farBlend) // too far
        {
            return;
        }
        if (dist > nearBlend) // blend
            shading *= 1.0f - (dist - nearBlend) / (farBlend - nearBlend);
    }

    // view matrix-------------------------------------
    glm::mat4 lightmtx;
    storm::math::viewMatrix(lightmtx, glm::vec3(0.0f, 0.0f, 0.0f), st.dir, glm::vec3(0.0f, 1.0f, 0.0f));
    storm::math::setInversePosition(lightmtx, st.lightPos.x, st.lightPos.y, st.lightPos.z);
    // projection matrix-------------------------------------
    glm::mat4 proj;
    float tanfov = sqrtf(storm::math::lengthSquared(st.objPos - st.lightPos)) / (1.2f * st.radius);
    float fov = 2.0f * atanf(1.0f / tanfov);

    proj = storm::math::projectionMatrix(fov, TEXTURE_SIZE, TEXTURE_SIZE, 0.1f, 1000.0f);
    // the projector's frustum: planes 0-3 of the clip in project()
    FindPlanes(lightmtx, proj);
    std::copy(planes, planes + 4, prepared_.planes);

    // what the map render (renderModel, Smooth) and the projection in Realize need (planes were stored above)
    Prepared &out = prepared_;
    out.visible = true;
    out.proj = proj;
    out.trans = lightmtx;
    out.perspective = std::max(proj[0][0], proj[1][1]);
    out.lightmtx = lightmtx;
    out.dir = st.dir;
    out.cen = cen;
    out.radius = radius;
    out.objPos = st.objPos;
    out.ObjPos = st.ObjPos;
    out.lightPos = st.lightPos;
    out.camPos = st.camPos;
    float dist = 3.0f * sqrtf(storm::math::lengthSquared(cen - st.camPos));
    shading *= powf(2.71f, -fogDensity_ * dist);
    out.shade = static_cast<int32_t>(fabsf(shading * 255.0f));
}

// The character's model into its tile of the raw atlas: the light's projection mapped onto the tile (clip space
// scaled and offset, D3D row vectors) and the scissor at the tile, so the map is the one of a TEXTURE_SIZE target.
void Shadow::renderModel()
{
    auto *obj = static_cast<MODEL *>(core.GetEntityPointer(entity));
    if (!obj)
    {
        prepared_.visible = false;
        return;
    }
    auto *node = obj->GetNode(0);
    int32_t x, y;
    tileOrigin(prepared_.tile, x, y);
    const float height = static_cast<float>(atlasRows * kAtlasCell);
    glm::mat4 tile; // p' = p * proj * tile
    tile[0][0] = TEXTURE_SIZE / static_cast<float>(kAtlasWidth);
    tile[1][1] = TEXTURE_SIZE / height;
    tile[3][0] = -1.0f + (2.0f * x + TEXTURE_SIZE) / kAtlasWidth;
    tile[3][1] = 1.0f - (2.0f * y + TEXTURE_SIZE) / height;
    const glm::mat4 proj = tile * prepared_.proj;
    rs->SetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(proj));
    rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(prepared_.lightmtx));
    const RECT scissor{x, y, x + TEXTURE_SIZE, y + TEXTURE_SIZE};
    rs->SetScissorRect(&scissor);

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

    rs->TextureSet(0, blurAtlas);
    int32_t x, y;
    tileOrigin(in.tile, x, y);
    atlasU0 = static_cast<float>(x) / kAtlasWidth;
    atlasV0 = static_cast<float>(y) / (atlasRows * kAtlasCell);
    atlasSU = static_cast<float>(TEXTURE_SIZE) / kAtlasWidth;
    atlasSV = static_cast<float>(TEXTURE_SIZE) / (atlasRows * kAtlasCell);
    rs->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(glm::mat4()));

    rs->SetRenderState(D3DRS_TEXTUREFACTOR, (in.shade << 16) | (in.shade << 8) | (in.shade << 0));
    tot_verts = 0;
    shadvert = static_cast<SHADOW_VERTEX *>(rs->LockVertexBuffer(vbuff, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK));
    if (!shadvert)
        return;
    if (fanFits())
        fan_.age++;
    else
        rebuildFan();
    // The exact clip of the location's polygons around the character
    size_t first = 0;
    glm::vec3 poly[32];
    for (size_t i = 0; i < fan_.counts.size(); i++)
    {
        const size_t count = fan_.counts[i];
        const glm::vec3 *from = fan_.verts.data() + first;
        first += count;
        const glm::vec4 &plane = fan_.planes[i];
        const float side = glm::dot(glm::vec3(plane), in.cen) - plane.w;
        if (side * side >= in.radius * in.radius)
            continue; // the location's tree would not have offered it
        std::copy(from, from + count, poly);
        const int32_t n = storm::shadow::clipPolygon(planes, 5, poly, count);
        if (n >= 3 && !AddPoly(poly, n))
            break;
    }

    rs->UnLockVertexBuffer(vbuff);

    if (tot_verts >= 3)
        rs->DrawPrimitive(D3DPT_TRIANGLELIST, vbuff, sizeof(SHADOW_VERTEX), 0, tot_verts / 3, "shadow_draw");
}

// Is the projector's volume of this frame inside what the fan was cut for: the character and the light moved less than
// the margin, and the blob's sphere lies within the fan's sphere?
bool Shadow::fanFits() const
{
    const Prepared &in = prepared_;
    const Fan &fan = fan_;
    return fan.valid && fan.age < kFanRefresh && std::abs(fan.perspective - in.perspective) <= 1e-3f * fan.perspective &&
           glm::length(in.ObjPos - fan.ObjPos) + kFanReach * glm::length(in.dir - fan.dir) <= kFanMargin &&
           glm::length(in.cen - fan.cen) + in.radius <= fan.sphereRadius;
}

// Reads the location's polygons within the projector's volume widened by kFanMargin (planes 0-4 of project()).
void Shadow::rebuildFan()
{
    const Prepared &in = prepared_;
    PLANE wide[5];
    for (int32_t p = 0; p < 5; p++)
    {
        wide[p] = planes[p];
        wide[p].D += kFanMargin;
    }
    fan_.verts.clear();
    fan_.counts.clear();
    fan_.planes.clear();
    recordVerts = &fan_.verts;
    recordCounts = &fan_.counts;
    recordPlanes = &fan_.planes;
    col->Clip(core.GetEntityIds(SHADOW), wide, 5, in.cen, in.radius + kFanMargin, recordPoly, &entity, 1);
    fan_.valid = true;
    fan_.age = 0;
    fan_.ObjPos = in.ObjPos;
    fan_.dir = in.dir;
    fan_.cen = in.cen;
    fan_.perspective = in.perspective;
    fan_.sphereRadius = in.radius + kFanMargin;
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

// This shadow's tile of the raw atlas blurred into the same tile of the blurred atlas (nine shifted, additive
// copies), inside the blur pass of prepareAll.
void Shadow::Smooth()
{
    struct SMOOTHVRT
    {
        float sx, sy, sz, rhw;
        int32_t diffuse;
        float tu, tv;
    };

    int32_t x, y;
    tileOrigin(prepared_.tile, x, y);
    const float width = static_cast<float>(kAtlasWidth), height = static_cast<float>(atlasRows * kAtlasCell);
    // tile-local uv (0..1 over TEXTURE_SIZE texels) -> atlas uv
    const auto atlasU = [&](float u) { return (x + u * TEXTURE_SIZE) / width; };
    const auto atlasV = [&](float v) { return (y + v * TEXTURE_SIZE) / height; };
    SMOOTHVRT vrt[4];
    const float corner[4][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    for (int32_t k = 0; k < 4; k++)
    {
        vrt[k].sx = x + corner[k][0] * (TEXTURE_SIZE - 1.0f);
        vrt[k].sy = y + corner[k][1] * (TEXTURE_SIZE - 1.0f);
        vrt[k].sz = 0.5f;
        vrt[k].rhw = 0.5f;
    }
    vrt[0].diffuse = vrt[3].diffuse = HEAD_DENSITY;
    vrt[1].diffuse = vrt[2].diffuse = 0xFFFFFFFF;

    if (rs->TechniqueExecuteStart("shadow_smooth"))
        do
        {
            static const int32_t nIterations = 3;

            for (int32_t u = 0; u < nIterations; u++)
                for (int32_t v = 0; v < nIterations; v++)
                {
                    const float ud = 1.5f * (u / (nIterations - 1.0f) - 0.5f) / (TEXTURE_SIZE - 1.0f) * 2.0f;
                    const float vd = 1.5f * (v / (nIterations - 1.0f) - 0.5f) / (TEXTURE_SIZE - 1.0f) * 2.0f;
                    for (int32_t k = 0; k < 4; k++)
                    {
                        vrt[k].tu = atlasU(corner[k][0] + ud);
                        vrt[k].tv = atlasV(corner[k][1] + vd);
                    }
                    const int32_t col = static_cast<int32_t>(1.0f / (nIterations * nIterations) * 255.0f);
                    rs->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFF000000 | (col << 16) | (col << 8) | (col << 0));
                    rs->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,
                                        D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_DIFFUSE | D3DFVF_TEXTUREFORMAT2, 2, &vrt,
                                        sizeof(SMOOTHVRT));
                }
        } while (rs->TechniqueExecuteNext());
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
    if (--refcount == 0)
        releaseShared(rs);
}

void Shadow::RestoreRender()
{
    if (refcount++ == 0)
        vbuff = createShadowVertexBuffer(rs); // the atlases are created on the next frame that needs them
}
