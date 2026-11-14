//#define SHOW_SPHERES 'Q'

#include "entity.h"
#include "core.h"
#include "modelr.h"
#include <storm/render/far_lod.hpp>
#include "string_compare.hpp"

#include <algorithm>

VGEOMETRY *NODER::gs = nullptr;
VRenderer *NODER::rs = nullptr;
int32_t NODER::depth = -1;
int32_t NODER::node;
extern int32_t clip_nps;
extern const PLANE *clip_p;
extern const glm::vec3 *clip_c;
extern float clip_r;
extern GEOS::ADD_POLYGON_FUNC clip_geosap;
extern ADD_POLYGON_FUNC clip_ap;
GEOS::PLANE clip_gp[256];
NODE *bestTraceNode = nullptr;
GEOS *clipGeo;

#ifdef SHOW_SPHERES
GEOS *sphere = 0;
#endif

//-------------------------------------------------------------------
//
//-------------------------------------------------------------------
glm::mat4 *backtrans;
glm::vec3 clip_v[256];

bool AddPolygon(const GEOS::VERTEX *vr, int32_t nv)
{
    //!!! must be transformed
    for (int32_t p = 0; p < nv; p++)
        clip_v[p] = storm::math::transformPoint(*backtrans, glm::vec3(vr[p].x, vr[p].y, vr[p].z));
    return clip_ap(clip_v, nv);
}

//-------------------------------------------------------------------
//
//-------------------------------------------------------------------
bool NODER::Clip()
{
    if (isReleased)
        return false;

    // check for bounding spheres intersection
    const auto sph_dist = storm::math::lengthSquared(*clip_c - storm::math::transformPoint(glob_mtx, center));
    if (sph_dist > (radius + clip_r) * (radius + clip_r))
        return false;

    auto retval = false;

    if (flags & CLIP_ENABLE && sph_dist <= (geo_radius + clip_r) * (geo_radius + clip_r))
    {
        backtrans = &glob_mtx;
        for (int32_t p = 0; p < clip_nps; p++)
        {
            const auto x = clip_p[p].D * clip_p[p].Nx - glob_mtx[3][0];
            const auto y = clip_p[p].D * clip_p[p].Ny - glob_mtx[3][1];
            const auto z = clip_p[p].D * clip_p[p].Nz - glob_mtx[3][2];
            const auto Nx =
                glob_mtx[0][0] * clip_p[p].Nx + glob_mtx[0][1] * clip_p[p].Ny + glob_mtx[0][2] * clip_p[p].Nz;
            const auto Ny =
                glob_mtx[1][0] * clip_p[p].Nx + glob_mtx[1][1] * clip_p[p].Ny + glob_mtx[1][2] * clip_p[p].Nz;
            const auto Nz =
                glob_mtx[2][0] * clip_p[p].Nx + glob_mtx[2][1] * clip_p[p].Ny + glob_mtx[2][2] * clip_p[p].Nz;
            const auto lx = glob_mtx[0][0] * x + glob_mtx[0][1] * y + glob_mtx[0][2] * z;
            const auto ly = glob_mtx[1][0] * x + glob_mtx[1][1] * y + glob_mtx[1][2] * z;
            const auto lz = glob_mtx[2][0] * x + glob_mtx[2][1] * y + glob_mtx[2][2] * z;
            clip_gp[p].nrm.x = Nx;
            clip_gp[p].nrm.y = Ny;
            clip_gp[p].nrm.z = Nz;
            clip_gp[p].d = (Nx * lx + Ny * ly + Nz * lz) * glob_mtx[3][3];
        }
        GEOS::VERTEX gc;
        glm::vec3 cnt;
        cnt = storm::math::inverseTransformPoint(glob_mtx, *clip_c);
        gc.x = cnt.x;
        gc.y = cnt.y;
        gc.z = cnt.z;

        bestTraceNode = this;
        retval = geo->Clip(clip_gp, clip_nps, gc, clip_r, clip_geosap);
    }

    if (flags & CLIP_ENABLE_TREE)
        for (const auto &child : next)
            if (static_cast<NODER *>(child.get())->Clip())
                retval = true;
    return retval;
}

//-------------------------------------------------------------------
//
//-------------------------------------------------------------------
float NODER::Update(glm::mat4 &mtx, glm::vec3 &cnt)
{
    glob_mtx = mtx * loc_mtx;

    center = geo_center;
    radius = geo_radius;

    for (const auto &child : next)
    {
        glm::vec3 cnt; // TODO: huh? ~!~
        const auto r = static_cast<NODER *>(child.get())->Update(glob_mtx, cnt);
        const auto rad = sqrtf(storm::math::lengthSquared(cnt - center)) + r;
        if (rad > radius)
            radius = rad;
    }

    cnt = storm::math::transformPoint(loc_mtx, center);
    return radius;
}

//----------------------------------------------------------
// NODE trace
//----------------------------------------------------------
float NODER::Trace(const glm::vec3 &src, const glm::vec3 &dst)
{
    if (isReleased)
        return 2.0f;
    // check for bounding spheres intersection
    const auto lmn = dst - src;
    const auto dist2ray2 = storm::math::lengthSquared(glm::cross(storm::math::transformPoint(glob_mtx, center) - src, lmn));
    const auto dlmn = storm::math::lengthSquared(lmn);
    // hierarchy test
    if (dist2ray2 > dlmn * radius * radius)
        return 2.0f;

    auto best_dist = 2.0f;

    if (flags & TRACE_ENABLE_TREE)
        for (const auto &child : next)
        {
            const auto d = static_cast<NODER *>(child.get())->Trace(src, dst);
            if (d < best_dist)
            {
                best_dist = d;
                bestTraceNode = child.get();
            }
        }

    if (flags & TRACE_ENABLE && dist2ray2 < dlmn * geo_radius * geo_radius)
    {
        glm::vec3 _src, _dst;
        _src = storm::math::inverseTransformPoint(glob_mtx, src);
        _dst = storm::math::inverseTransformPoint(glob_mtx, dst);
        const auto ds = geo->Trace((GEOS::VERTEX &)_src, (GEOS::VERTEX &)_dst);
        if (ds < best_dist)
        {
            best_dist = ds;
            bestTraceNode = this;
        }
    }
    return best_dist;
}

//----------------------------------------------------------
// NODE constructor
//----------------------------------------------------------
NODER::NODER()
{
#ifdef SHOW_SPHERES
    sphere = 0;
#endif
    geo = nullptr;
    parent = nullptr;

    max_view_dist = 0.f;
}

bool NODER::Init(const char *lightPath, const char *pname, const char *oname, const glm::mat4 &m, const glm::mat4 &globm,
                 NODER *par, const char *lmPath)
{
    isReleased = false;
    name[0] = 0;
    technique[0] = 0;
    geoMaterialFunc = nullptr;
    flags = VISIBLE | VISIBLE_TREE | CLIP_ENABLE | CLIP_ENABLE_TREE | TRACE_ENABLE | TRACE_ENABLE_TREE;

    loc_mtx = m;
    glob_mtx = globm * loc_mtx;

    if (pname == nullptr)
    {
        throw std::runtime_error(fmt::format("NODER::Init: got nullptr model name"));
    }
    sys_modelName_base = pname;

    if (oname && oname[0])
        sys_modelName_full = fmt::format("{}_{}", sys_modelName_base, oname);
    else
        sys_modelName_full = sys_modelName_base;
    
    if (lightPath)
        sys_LightPath = lightPath;

    if (lmPath)
        sys_lmPath = lmPath;

    sys_TexPath = gs->GetTexturePath();

    geo = gs->CreateGeometry(sys_modelName_full.c_str(), sys_LightPath.c_str(), 0, lmPath);
    if (!geo)
    {
        return false;
    }

    // load geometry radius and center
    GEOS::INFO gi;
    geo->GetInfo(gi);
    geo_radius = gi.radius;
    geo_center = glm::vec3(gi.boxcenter.x, gi.boxcenter.y, gi.boxcenter.z);

    parent = par;
    if (parent == nullptr)
        strcpy_s(name, pname);
    // calculate number of labels
    const auto idGeo = geo->FindName("geometry");
    int32_t node_count = 0;
    int32_t sti = -1;
    while ((sti = geo->FindLabelG(sti + 1, idGeo)) > -1)
        node_count++;

    if (node_count > 0)
    {
        next = std::vector<std::unique_ptr<NODE>>(node_count);
        int32_t sti = -1;
        int32_t l = 0;
        while ((sti = geo->FindLabelG(sti + 1, idGeo)) > -1)
        {
            GEOS::LABEL lb;
            geo->GetLabel(sti, lb);
            glm::mat4 mt;
            // memcpy(mt.m, lb.m, sizeof(lb.m));
            storm::math::vx(mt) = glm::vec3(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            storm::math::vy(mt) = glm::vec3(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            storm::math::vz(mt) = glm::vec3(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            storm::math::pos(mt) = glm::vec3(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            auto *child = new NODER();
            next[l].reset(child);

            if (!child->Init(lightPath, pname, lb.name, mt, glob_mtx, this, lmPath))
                return false;

            strcpy_s(child->name, lb.name);
            l++;
        }
    }

    idGeoGroup = -1;
    if (parent)
        idGeoGroup = parent->geo->FindName("geometry");

    return true;
}

//----------------------------------------------------------
// NODE destructor
//----------------------------------------------------------

NODER::~NODER()
{
#ifdef SHOW_SPHERES
    if (sphere != 0)
    {
        delete sphere;
        sphere = 0;
    }
#endif

    delete geo;
    // the children (std::unique_ptr) go with this node
}

void NODER::ReleaseGeometry()
{
    if (isReleased)
        return;
    delete geo;
    geo = nullptr;
    isReleased = true;
    for (const auto &child : next)
        static_cast<NODER *>(child.get())->ReleaseGeometry();
}

void NODER::RestoreGeometry()
{
    if (!isReleased)
        return;

    const auto *const tPath = gs->GetTexturePath();
    const auto len = strlen(tPath) + 1;
    auto *const ttPath = new char[len];
    memcpy(ttPath, tPath, len);
    gs->SetTexturePath(sys_TexPath.c_str());
    geo = gs->CreateGeometry(sys_modelName_full.c_str(), sys_LightPath.c_str(), 0, sys_lmPath.c_str());
    gs->SetTexturePath(ttPath);
    delete[] ttPath;
    if (!geo)
        throw std::runtime_error(fmt::format("Cannot restore geometry {}", sys_modelName_full));

    isReleased = false;
    for (const auto &child : next)
        static_cast<NODER *>(child.get())->RestoreGeometry();
}

//----------------------------------------------------------
// NODE draw
//----------------------------------------------------------
extern GEOS::PLANE ViewPlane[4];
GEOS::PLANE TViewPlane[4];

void NODER::Draw()
{
    if (isReleased)
        return;

    const auto cnt = storm::math::transformPoint(glob_mtx, center);

    // visibility check
    int32_t p;
    for (p = 0; p < 4; p++)
    {
        const auto dist =
            cnt.x * ViewPlane[p].nrm.x + cnt.y * ViewPlane[p].nrm.y + cnt.z * ViewPlane[p].nrm.z - ViewPlane[p].d;
        if (dist > radius)
            break;
    }
    if (p < 4)
        return;
    if (max_view_dist > 0.f)
    {
        glm::vec3 cpos, cang;
        float cpersp;
        rs->GetCamera(cpos, cang, cpersp);
        const float fdist = storm::math::lengthSquared(cpos - cnt);
        const float fmindist = (max_view_dist + radius) * (max_view_dist + radius);
        const float fmaxdist = (max_view_dist * 1.3f + radius) * (max_view_dist * 1.3f + radius);
        if (fdist > fmaxdist)
            distance_blend = 1.f;
        else if (fdist < fmindist)
            distance_blend = 0.f;
        else
            distance_blend = (fdist - fmindist) / (fmaxdist - fmindist);
        if (distance_blend >= 1.f)
            return;
        if (distance_blend < 0.f)
            distance_blend = 0.f;
    }

#ifdef SHOW_SPHERES
    if (sphere == 0)
    {
        sphere = gs->CreateGeometry("sphere", 0, 0);
    }
    if (core.Controls->GetDebugAsyncKeyState(SHOW_SPHERES) < 0)
    {
        glm::mat4 sm(0.0f, 0.0f, 0.0f, cnt.x, cnt.y, cnt.z);
        glm::mat4 sc;
        sc.m[0][0] = sc.m[1][1] = sc.m[2][2] = radius;
        glm::mat4 rm = sc * sm;
        rs->SetTransform(D3DTS_WORLD, (D3DMATRIX *)&rm);
        sphere->Draw(0, 0, 0);
    }
#endif

    // draw this node
    if (flags & VISIBLE)
    {
        // visibility check for geometry
        int32_t p;
        for (p = 0; p < 4; p++)
        {
            const float dist =
                cnt.x * ViewPlane[p].nrm.x + cnt.y * ViewPlane[p].nrm.y + cnt.z * ViewPlane[p].nrm.z - ViewPlane[p].d;
            if (dist > geo_radius)
                break;
        }
        if (p == 4)
        {
            rs->SetTransform(D3DTS_WORLD, (D3DMATRIX *)&glob_mtx);
            gs->SetTechnique(&technique[0]);
            if (max_view_dist > 0.f && distance_blend > 0.f)
            {
                gs->SetTechnique("geomdistanceblend");
                uint32_t dwTFColor;
                dwTFColor = (static_cast<uint32_t>(255.f - 255.f * distance_blend) << 24) | 0xFFFFFF;
                rs->SetRenderState(D3DRS_TEXTUREFACTOR, dwTFColor);
            }
            // transform viewplanes
            for (p = 0; p < 4; p++)
            {
                const float x = ViewPlane[p].d * ViewPlane[p].nrm.x - glob_mtx[3][0];
                const float y = ViewPlane[p].d * ViewPlane[p].nrm.y - glob_mtx[3][1];
                const float z = ViewPlane[p].d * ViewPlane[p].nrm.z - glob_mtx[3][2];
                const float Nx = glob_mtx[0][0] * ViewPlane[p].nrm.x + glob_mtx[0][1] * ViewPlane[p].nrm.y +
                                 glob_mtx[0][2] * ViewPlane[p].nrm.z;
                const float Ny = glob_mtx[1][0] * ViewPlane[p].nrm.x + glob_mtx[1][1] * ViewPlane[p].nrm.y +
                                 glob_mtx[1][2] * ViewPlane[p].nrm.z;
                const float Nz = glob_mtx[2][0] * ViewPlane[p].nrm.x + glob_mtx[2][1] * ViewPlane[p].nrm.y +
                                 glob_mtx[2][2] * ViewPlane[p].nrm.z;
                const float lx = glob_mtx[0][0] * x + glob_mtx[0][1] * y + glob_mtx[0][2] * z;
                const float ly = glob_mtx[1][0] * x + glob_mtx[1][1] * y + glob_mtx[1][2] * z;
                const float lz = glob_mtx[2][0] * x + glob_mtx[2][1] * y + glob_mtx[2][2] * z;
                TViewPlane[p].nrm.x = Nx;
                TViewPlane[p].nrm.y = Ny;
                TViewPlane[p].nrm.z = Nz;
                TViewPlane[p].d = (Nx * lx + Ny * ly + Nz * lz) * glob_mtx[3][3];
            }

            // draw geos
            if (geo->HasFarLods())
                SetFarLodView();
            geo->Draw(&TViewPlane[0], 4, geoMaterialFunc);
            storm::render::far_lod::view.valid = false;
        }
    }

    // draw all children
    if (flags & VISIBLE_TREE)
        for (const auto &child : next)
            static_cast<NODER *>(child.get())->Draw();
}

// What the far LODs of this node's geometry see (storm/render/far_lod.hpp): the camera and its view direction in the
// node's space, the scale, and the pixels of the target being drawn.
void NODER::SetFarLodView() const
{
    namespace far_lod = storm::render::far_lod;
    far_lod::View &view = far_lod::view;
    view.valid = false;
    if (far_lod::pixels <= 0.f && far_lod::cull_m <= 0.f)
        return;
    D3DVIEWPORT9 viewport;
    if (rs->GetViewport(&viewport) != D3D_OK || viewport.Height == 0)
        return;
    // The eye and the view direction of the view being drawn (the sea's reflection and cube faces set their own
    // views, not the camera): view space looks along +z.
    const glm::mat4 from_view = glm::inverse(rs->GetView());
    const glm::vec3 camera = glm::vec3(from_view * glm::vec4(0.f, 0.f, 0.f, 1.f));
    const glm::mat4 to_node = glm::inverse(glob_mtx);
    const glm::vec3 local = glm::vec3(to_node * glm::vec4(camera, 1.f));
    const glm::vec3 forward = glm::normalize(glm::vec3(from_view * glm::vec4(0.f, 0.f, 1.f, 0.f)));
    const glm::vec3 local_forward = glm::vec3(to_node * glm::vec4(forward, 0.f));
    view.scale = glm::length(glm::vec3(glob_mtx[0]));
    if (view.scale <= 0.f)
        return;
    view.camera[0] = local.x, view.camera[1] = local.y, view.camera[2] = local.z;
    // forward in node units per world metre: a depth in node units times `scale` is metres.
    const glm::vec3 unit_forward = local_forward * view.scale;
    view.forward[0] = unit_forward.x, view.forward[1] = unit_forward.y, view.forward[2] = unit_forward.z;
    const glm::mat4 &projection = rs->GetProjection();
    view.ortho = projection[3][3] == 1.f;
    const float half_height = 0.5f * static_cast<float>(viewport.Height);
    if (view.ortho)
        view.pixels_per_unit = half_height * projection[1][1];
    else
    {
        // From the screen's centre to its corner the same distance is up to this much nearer in depth.
        const float tan_x = 1.f / projection[0][0], tan_y = 1.f / projection[1][1];
        view.pixels_per_unit = half_height * projection[1][1] * std::sqrt(1.f + tan_x * tan_x + tan_y * tan_y);
    }
    rs->GetNearFarPlane(view.near_plane, view.far_plane);
    view.valid = true;
}

//----------------------------------------------------------
// NODE get node by number
//----------------------------------------------------------
NODER *NODER::FindNode(const char *cNodeName)
{
    if (storm::iEquals(cNodeName, name))
        return this;
    for (const auto &child : next)
        if (NODER *nd = static_cast<NODER *>(child.get())->FindNode(cNodeName))
            return nd;
    return nullptr;
}

//----------------------------------------------------------
// NODE get node by number
//----------------------------------------------------------
NODER *NODER::GetNode(int32_t n)
{
    depth++;
    if (depth == 0)
        node = 0;
    if (node == n)
    {
        depth--;
        return this;
    }
    node++;
    for (const auto &child : next)
    {
        NODER *g = static_cast<NODER *>(child.get())->GetNode(n);
        if (g != nullptr)
        {
            depth--;
            return g;
        }
    }
    depth--;
    return nullptr;
}

//-------------------------------------------------------------------
//
//-------------------------------------------------------------------
const char *NODER::GetName()
{
    return &name[0];
}

//-------------------------------------------------------------------
// unlink node to model
//-------------------------------------------------------------------
entid_t NODER::Unlink2Model()
{
    const entid_t id = core.CreateEntity("modelr");
    auto *mdl = static_cast<MODELR *>(core.GetEntityPointer(id));

    // for non-root: the parent hands its ownership of this node to the new model
    if (parent != nullptr)
    {
        // get parent matrix
        mdl->mtx = parent->glob_mtx;

        const auto slot = std::ranges::find_if(parent->next, [this](const auto &child) { return child.get() == this; });
        if (slot == parent->next.end())
            throw std::runtime_error(fmt::format("NODER::Unlink2Model: node {} is not a child of its parent {}", name,
                                                 parent->GetName()));
        slot->release();
        parent->next.erase(slot);
        // the old model may go before this one (a fallen mast outlives its ship)
        parent = nullptr;
    }
    //!!! else get model's mtx

    // link node to as root
    mdl->root = this;

    return id;
}

//-------------------------------------------------------------------
//
//-------------------------------------------------------------------
void NODER::SetTechnique(const char *name)
{
    strcpy_s(technique, name);
}

//-------------------------------------------------------------------
//
//-------------------------------------------------------------------
const char *NODER::GetTechnique()
{
    return &technique[0];
}

void NODER::SetMaxViewDist(float fDist)
{
    max_view_dist = fDist;
    for (const auto &child : next)
        static_cast<NODER *>(child.get())->SetMaxViewDist(fDist);
}

void NODER::SubstituteGeometry(const std::string &new_model)
{
    sys_modelName_full = fmt::format("{}_{}", sys_modelName_base, new_model);
    ReleaseGeometry();
    RestoreGeometry();
}
