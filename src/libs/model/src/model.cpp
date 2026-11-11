#include "core.h"
#include "entity.h"
#include "modelr.h"
#include "shared/messages.h"
#include "legacy_dynamic_vertex_stream.hpp"
#include "skinning.hpp"

CREATE_CLASS(MODELR)


MODELR::MODELR()
{
    bSetupFog = false;
    LightPath[0] = 0;
    lmPath[0] = 0;
    ani = nullptr;
    memset(aniVerts, 0, sizeof(aniVerts));
    for (int32_t i = 0; i < ANI_MAX_ACTIONS; i++)
        aniPos[i] = -1.0f;
    root = nullptr;
    useBlend = false;
    idxBuff = nullptr;
}


MODELR::~MODELR()
{
    skinnedStreams_.clear();
    gpuSkinning_.clear();
    delete root;
    for (auto i = 0; i < MODEL_ANI_MAXBUFFERS; i++)
        delete aniVerts[i].v;
    delete ani;

    delete idxBuff;
}

bool MODELR::Init()
{
    rs = static_cast<VRenderer *>(core.GetService("dx9render"));
    if (!rs)
        throw std::runtime_error("No service: dx9render");
    gpuSkinning_.setRenderer(rs);

    GeometyService = static_cast<VGEOMETRY *>(core.GetService("geometry"));
    if (!GeometyService)
        throw std::runtime_error("No service: geometry");

    return true;
}

storm::render::VertexBufferId MODELR::VBTransform(void *context, const void *vb, const std::shared_ptr<storm::render::VertexSourceLifetime>& lifetime, int32_t startVrt, int32_t nVerts, int32_t totVerts, bool gpuSkinning)
{
    if(!context) throw std::invalid_argument("Missing skinning context");
    auto& model=*static_cast<MODELR*>(context);
    if(totVerts<=0 || startVrt<0 || nVerts<0 || startVrt>totVerts || nVerts>totVerts-startVrt)
        throw std::invalid_argument("Invalid animated draw range");
    if(gpuSkinning) {
        static_assert(sizeof(GEOS::AVERTEX0)==storm::model::kSkinnedSourceStride);
        const auto id=model.gpuSkinning_.vertexBuffer(vb,lifetime,uint32_t(totVerts));
        model.gpuSkinning_.bind(&model.ani->GetAnimationMatrix(0),uint32_t(model.ani->GetNumBones()));
        return {id};
    }
    static_assert(sizeof(GEOS::VERTEX0)==storm::render::skinnedVertexStride);
    static_assert(offsetof(GEOS::VERTEX0,color)==storm::render::skinnedColorOffset);
    auto* bones=&model.ani->GetAnimationMatrix(0);
    const auto* src=static_cast<const GEOS::AVERTEX0*>(vb);
    return model.skinnedStreams_.transform(vb,lifetime,uint32_t(totVerts),
        [&] {return storm::render::makeLegacyDynamicVertexStream(*model.rs);},
        [&](GEOS::VERTEX0* dst) {
    // NEON on arm64 (SSE2 on x86-64, scalar elsewhere); see skinning.hpp.
    static_assert(sizeof(GEOS::AVERTEX0) == sizeof(storm::skinning::Source) &&
                  offsetof(GEOS::AVERTEX0, weight) == offsetof(storm::skinning::Source, weight) &&
                  offsetof(GEOS::AVERTEX0, boneid) == offsetof(storm::skinning::Source, boneid) &&
                  offsetof(GEOS::AVERTEX0, nrm) == offsetof(storm::skinning::Source, nrm) &&
                  offsetof(GEOS::AVERTEX0, color) == offsetof(storm::skinning::Source, color) &&
                  offsetof(GEOS::AVERTEX0, tu0) == offsetof(storm::skinning::Source, tu0));
    static_assert(sizeof(GEOS::VERTEX0) == sizeof(storm::skinning::Target) &&
                  offsetof(GEOS::VERTEX0, nrm) == offsetof(storm::skinning::Target, nrm) &&
                  offsetof(GEOS::VERTEX0, color) == offsetof(storm::skinning::Target, color) &&
                  offsetof(GEOS::VERTEX0, tu) == offsetof(storm::skinning::Target, tu));
    static_assert(sizeof(glm::mat4) == 16 * sizeof(float));
    storm::skinning::skin(reinterpret_cast<const storm::skinning::Source *>(src),
                          reinterpret_cast<storm::skinning::Target *>(dst), bones, sizeof(glm::mat4),
                          static_cast<size_t>(totVerts));

        });
}

void SetChildrenTechnique(NODE *_root, const char *_name)
{
    if (!_root || !_name)
        return;
    _root->SetTechnique(_name);
    for (int i = 0; i < _root->next.size(); i++)
        SetChildrenTechnique(_root->next[i], _name);
}

//-----------------------------------------------------------------------------------
// realize
//-----------------------------------------------------------------------------------
GEOS::PLANE ViewPlane[4];

void MODELR::Realize(uint32_t Delta_Time)
{
    // GUARD(MODELR::Realize)
    if (!root)
        return;

    uint32_t dwOldFogEnable;
    float fOldFogDensity;
    if (bSetupFog)
    {
        rs->GetRenderState(D3DRS_FOGENABLE, &dwOldFogEnable);
        rs->GetRenderState(D3DRS_FOGDENSITY, (uint32_t *)&fOldFogDensity);

        rs->SetRenderState(D3DRS_FOGENABLE, (bFogEnable) ? true : false);
        rs->SetRenderState(D3DRS_FOGDENSITY, F2DW(fFogDensity));
    }

    if (renderTuner)
        renderTuner->Set(this, rs);

    if (useBlend)
    {
        if (!passedTime)
            SetChildrenTechnique(root, blendTechnique.c_str());

        passedTime += Delta_Time;
        /*
        if (passedTime > blendTime)
          useBlend = false;
        else
        */
        {
            static uint32_t ambient;
            rs->GetRenderState(D3DRS_AMBIENT, &ambient);
            ambient &= 0x00FFFFFF;
            float timeK = static_cast<float>(passedTime) / blendTime;
            if (timeK > 1.0f)
                timeK = 1.0f;
            float alpha = alpha1 + (alpha2 - alpha1) * timeK;
            ambient |= static_cast<unsigned char>(255.0 * alpha) << 24;
            // ambient |= 0x05 << 24;
            rs->SetRenderState(D3DRS_TEXTUREFACTOR, ambient);
        }
    }

    glm::mat4 view, proj;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
    rs->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(proj));
    FindPlanes(view, proj);

    glm::vec3 tmp;
    root->Update(mtx, tmp);

    // if have animation - special render
    if (ani)
    {
        skinnedStreams_.beginPose(); // All bone-matrix inputs, including stationary actions.
        gpuSkinning_.beginPose();
        GeometyService->SetVBConvertFunc(VBTransform,this);
        struct ClearTransform {
            VGEOMETRY* geometry;
            storm::model::GpuSkinning& skinning;
            ~ClearTransform() {geometry->SetVBConvertFunc(nullptr); skinning.unbind();}
        } clearTransform{GeometyService,gpuSkinning_};
        root->Draw();
    }
    else
        root->Draw();

    if (renderTuner)
        renderTuner->Restore(this, rs);

    if (bSetupFog)
    {
        rs->SetRenderState(D3DRS_FOGENABLE, dwOldFogEnable);
        rs->SetRenderState(D3DRS_FOGDENSITY, F2DW(fOldFogDensity));
    }

    // UNGUARD
}

Animation *MODELR::GetAnimation()
{
    return ani;
}

void MODELR::AniRender()
{
}

uint64_t MODELR::ProcessMessage(MESSAGE &message)
{
    std::string str;
    const int32_t code = message.Long();
    glm::vec3 tmp;
    switch (code)
    {
    case MSG_SEA_REFLECTION_DRAW:
        Realize(0);
        break;
    case MSG_MODEL_SET_PARENT: {
        /*entid_t ParentID = message.EntityID();
        if (core.ValidateEntity(&ParentID))
        {
          parent = (MODEL*)ParentID.pointer;
        }*/
    }
    break;
    case MSG_MODEL_SET_POSITION: {
        // Pos,vx,vy,vz
        glm::vec3 &vx = storm::math::vx(mtx);
        glm::vec3 &vy = storm::math::vy(mtx);
        glm::vec3 &vz = storm::math::vz(mtx);
        glm::vec3 &vpos = storm::math::pos(mtx);

        vpos.x = message.Float();
        vpos.y = message.Float();
        vpos.z = message.Float();

        vx.x = message.Float();
        vx.y = message.Float();
        vx.z = message.Float();

        vy.x = message.Float();
        vy.y = message.Float();
        vy.z = message.Float();

        vz.x = message.Float();
        vz.y = message.Float();
        vz.z = message.Float();
    }
    break;
    case MSG_MODEL_BLEND:
        // blendTechnique, time, a1, a2
        useBlend = true;
        passedTime = 0;
        blendTechnique = message.String();
        blendTime = message.Long();
        alpha1 = message.Float();
        alpha2 = message.Float();
        break;
    case MSG_MODEL_LOAD_GEO: // set geometry
        // GUARD(MSG_MODEL_LOAD_GEO)
        str = message.String();
        NODER::gs = GeometyService;
        NODER::rs = rs;
        root = new NODER();
        if (!root->Init(LightPath.c_str(), str.c_str(), "", storm::math::eulerMatrix(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f), mtx, nullptr,
                        lmPath.c_str()))
        {
            delete root;
            root = nullptr;
            core.EraseEntity(GetId());
            return 0;
        }
        // glm::vec3 tmp;
        root->Update(mtx, tmp);
        return 1;
        // UNGUARD
        break;
    case MSG_MODEL_LOAD_ANI: // set animation
    {
        str = message.String();
        auto asr = static_cast<AnimationService *>(core.GetService("AnimationServiceImp"));
        skinnedStreams_.clear();
        gpuSkinning_.clear();
        delete ani;
        ani = nullptr;
        ani = asr->CreateAnimation(str.c_str());
        if (ani)
            return 1;
        return 0;
    }
    break;
    case MSG_MODEL_SET_FOG:
        bSetupFog = message.Long() != 0;
        bFogEnable = message.Long() != 0;
        fFogDensity = message.Float();
        break;
    case MSG_MODEL_SET_LIGHT_PATH:
        // GUARD(MSG_MODEL_SET_LIGHT_PATH)
        LightPath = message.String();
        // UNGUARD
        break;
    case MSG_MODEL_SET_LIGHT_LMPATH:
        // GUARD(MSG_MODEL_SET_LIGHT_LMPATH)
        lmPath = message.String();
        // UNGUARD
        break;
    case MSG_MODEL_RELEASE:
        // GUARD(MSG_MODEL_RELEASE)
        if (root)
            root->ReleaseGeometry();
        // UNGUARD
        break;
    case MSG_MODEL_RESTORE:
        // GUARD(MSG_MODEL_RESTORE)
        if (root)
            root->RestoreGeometry();
        // UNGUARD
        break;
    case MSG_MODEL_SET_DIRPATH: {
        str = message.String();
        GeometyService->SetTexturePath(str.c_str());
    }
    break;
    case MSG_MODEL_SET_TECHNIQUE: {
        str = message.String();
        if (root)
            SetChildrenTechnique(root, str.c_str());
    }
    break;
    case MSG_MODEL_SET_MAX_VIEW_DIST:
        if (root)
            root->SetMaxViewDist(message.Float());
        break;
    case MSG_MODEL_SUBSTITUTE_GEOMETRY_NODE: {
        auto &&geometry_node = message.String();
        auto &&new_model_name = message.String();

        if (auto *node = FindNode(geometry_node.c_str()))
        {
            node->SubstituteGeometry(new_model_name);
        }
        else
        {
            spdlog::trace("MODELR: Cannot substitute geometry node {}", geometry_node);
        }
        break;
    }

    case MSG_MODEL_PLAY_ACTION: {
        if (!ani)
        {
            return 0;
        }

        auto &player = ani->Player(message.Long());
        player.SetAction(message.String().c_str());
        player.Play();
        break;
    }
    }
    return 1;
}

NODE *MODELR::GetNode(int32_t n)
{
    return root->GetNode(n);
}

NODE *MODELR::FindNode(const char *cNodeName)
{
    return root->FindNode(cNodeName);
}

void MODELR::Update()
{
    glm::vec3 tmp;
    static_cast<NODER *>(root)->Update(mtx, tmp);
}

//-------------------------------------------------------------------
// clip functions
//-------------------------------------------------------------------
const PLANE *clip_p;
const glm::vec3 *clip_c;
float clip_r;
ADD_POLYGON_FUNC clip_ap;
GEOS::ADD_POLYGON_FUNC clip_geosap;
int32_t clip_nps;

bool AddPolygon(const GEOS::VERTEX *vr, int32_t nv);
//-------------------------------------------------------------------
bool MODELR::Clip(const PLANE *planes, int32_t nplanes, const glm::vec3 &center, float radius, ADD_POLYGON_FUNC addpoly)
{
    clip_p = planes;
    clip_nps = nplanes;
    clip_c = &center;
    clip_r = radius;
    clip_ap = addpoly;
    if (clip_ap == nullptr)
        clip_geosap = nullptr;
    else
        clip_geosap = AddPolygon;
    return root->Clip();
}

extern NODE *bestTraceNode;
//-------------------------------------------------------------------
const char *MODELR::GetCollideMaterialName()
{
    GEOS::TRACE_INFO ti;
    if (colideNode->geo->GetCollisionDetails(ti) == false)
        return "";
    GEOS::OBJECT go;
    colideNode->geo->GetObj(ti.obj, go);
    static GEOS::MATERIAL gm;
    colideNode->geo->GetMaterial(go.material, gm);
    return gm.name;
}

//-------------------------------------------------------------------
bool MODELR::GetCollideTriangle(TRIANGLE &triangle)
{
    GEOS::TRACE_INFO ti;
    if (colideNode->geo->GetCollisionDetails(ti) == false)
        return false;
    triangle.vrt[0] = storm::math::transformPoint(colideNode->glob_mtx, glm::vec3(ti.vrt[0].x, ti.vrt[0].y, ti.vrt[0].z));
    triangle.vrt[1] = storm::math::transformPoint(colideNode->glob_mtx, glm::vec3(ti.vrt[1].x, ti.vrt[1].y, ti.vrt[1].z));
    triangle.vrt[2] = storm::math::transformPoint(colideNode->glob_mtx, glm::vec3(ti.vrt[2].x, ti.vrt[2].y, ti.vrt[2].z));
    return true;
}

//-------------------------------------------------------------------
glm::vec3 cold[1024 * 1024];

float MODELR::Trace(const glm::vec3 &src, const glm::vec3 &dst)
{
    // collision with skinned geometry
    if (ani)
    {
        // check for bounding spheres intersection
        glm::vec3 lmn = dst - src;
        float dist2ray2 = storm::math::lengthSquared(glm::cross(storm::math::transformPoint(root->glob_mtx, root->center) - src, lmn));
        float dlmn = storm::math::lengthSquared(lmn);
        // hierarchy test
        if (dist2ray2 > dlmn * root->radius * root->radius)
            return 2.0f;
        // if(core.Controls->GetAsyncKeyState(0xC0)>=0)    return 2.0f;
        // if(core.Controls->GetAsyncKeyState(VK_SHIFT)<0 && dist2ray2 > dlmn*root->radius*root->radius)    return 2.0f;

        // get bones
        auto* bones = &ani->GetAnimationMatrix(0);

        glm::vec3 _src, _dst;
        _src = storm::math::inverseTransformPoint(root->glob_mtx, src);
        _dst = storm::math::inverseTransformPoint(root->glob_mtx, dst);
        glm::vec3 dirvec = _dst - _src;

        float bd = 2.0f;
        GEOS::INFO gi;

        // load indices
        if (idxBuff == nullptr)
        {
            auto *idx = static_cast<unsigned short *>(rs->LockIndexBuffer(root->geo->GetIndexBuffer()));
            if (idx == nullptr)
                return 0.;

            int nt = 0;
            root->geo->GetInfo(gi);
            for (int32_t vb = 0; vb < gi.nvrtbuffs; vb++)
            {
                int32_t avb = root->geo->GetVertexBuffer(vb);
                VGEOMETRY::ANIMATION_VB gavb = GeometyService->GetAnimationVBDesc(avb);
                auto *gsrc = static_cast<GEOS::AVERTEX0 *>(gavb.buff);

                // for all objects that refers to this vertexBuffer
                for (int32_t o = 0; o < gi.nobjects; o++)
                {
                    GEOS::OBJECT go;
                    root->geo->GetObj(o, go);
                    if (go.vertex_buff != static_cast<uint32_t>(avb))
                        continue;

                    nt += go.ntriangles;
                }
            }

            idxBuff = new unsigned short[nt * 3];
            for (int32_t vb = 0; vb < gi.nvrtbuffs; vb++)
            {
                int32_t avb = root->geo->GetVertexBuffer(vb);
                VGEOMETRY::ANIMATION_VB gavb = GeometyService->GetAnimationVBDesc(avb);
                auto *gsrc = static_cast<GEOS::AVERTEX0 *>(gavb.buff);

                // for all objects that refers to this vertexBuffer
                for (int32_t o = 0; o < gi.nobjects; o++)
                {
                    GEOS::OBJECT go;
                    root->geo->GetObj(o, go);
                    if (go.vertex_buff != static_cast<uint32_t>(avb))
                        continue;

                    // for all triangles in object
                    for (int32_t gt = 0; gt < go.ntriangles; gt++)
                    {
                        int32_t t = gt * 3 + go.striangle;
                        // Tomas Moller and Ben Trumbore algorithm
                        idxBuff[t + 0] = idx[t + 0];
                        idxBuff[t + 1] = idx[t + 1];
                        idxBuff[t + 2] = idx[t + 2];
                    }
                }
            }
            rs->UnLockIndexBuffer(root->geo->GetIndexBuffer());
        }

        root->geo->GetInfo(gi);
        for (int32_t vb = 0; vb < gi.nvrtbuffs; vb++)
        {
            int32_t avb = root->geo->GetVertexBuffer(vb);
            VGEOMETRY::ANIMATION_VB gavb = GeometyService->GetAnimationVBDesc(avb);
            auto *gsrc = static_cast<GEOS::AVERTEX0 *>(gavb.buff);

            // transform vertices and trace
            for (int32_t v = 0; v < gavb.nvertices; v++)
            {
                cold[v] = *(glm::vec3 *)(&gsrc->pos);
                glm::mat4 &m1 = bones[gsrc->boneid & 0xff];
                glm::mat4 &m2 = bones[(gsrc->boneid >> 8) & 0xff];

                glm::vec3 pos(*(glm::vec3 *)(&gsrc->pos));
                glm::vec3 v1 = storm::math::transformPoint(m1, pos);
                glm::vec3 v2 = storm::math::transformPoint(m2, pos);
                cold[v].x = -(gsrc->weight * v1.x + (1.0f - gsrc->weight) * v2.x);
                cold[v].y = gsrc->weight * v1.y + (1.0f - gsrc->weight) * v2.y;
                cold[v].z = gsrc->weight * v1.z + (1.0f - gsrc->weight) * v2.z;
                gsrc++;
            }

            // for all objects that refers to this vertexBuffer
            for (int32_t o = 0; o < gi.nobjects; o++)
            {
                GEOS::OBJECT go;
                root->geo->GetObj(o, go);
                if (go.vertex_buff != static_cast<uint32_t>(avb))
                    continue;

                // for all triangles in object
                for (int32_t gt = 0; gt < go.ntriangles; gt++)
                {
                    int32_t t = gt * 3 + go.striangle;
                    // Tomas Moller and Ben Trumbore algorithm
                    glm::vec3 a = cold[idxBuff[t + 1] + go.start_vertex] - cold[idxBuff[t + 0] + go.start_vertex];
                    glm::vec3 b = cold[idxBuff[t + 2] + go.start_vertex] - cold[idxBuff[t + 0] + go.start_vertex];
                    glm::vec3 pvec = glm::cross(dirvec, b);
                    float det = glm::dot(a, pvec);
                    float invdet = 1.0f / det;

                    glm::vec3 c = _src - cold[idxBuff[t + 0] + go.start_vertex];
                    float U = glm::dot(c, pvec) * invdet;
                    if (U < 0.0f || U > 1.0)
                        continue;

                    glm::vec3 qvec = glm::cross(c, a);
                    float V = glm::dot(dirvec, qvec) * invdet;
                    if (V < 0.0f || U + V > 1.0)
                        continue;

                    float d = glm::dot(b, qvec) * invdet;
                    if (d >= 0.0f && d <= 1.0 && d < bd)
                    {
                        bd = d;
                    }
                }
            }
        }

        return bd;
    }
    float d = root->Trace(src, dst);
    colideNode = bestTraceNode;
    return d;
}

//-------------------------------------------------------------------
NODE *MODELR::GetCollideNode()
{
    return colideNode;
}

void MODELR::FindPlanes(const glm::mat4 &view, const glm::mat4 &proj)
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

    ViewPlane[0].nrm.x = v[0].x * view[0][0] + v[0].y * view[0][1] + v[0].z * view[0][2];
    ViewPlane[0].nrm.y = v[0].x * view[1][0] + v[0].y * view[1][1] + v[0].z * view[1][2];
    ViewPlane[0].nrm.z = v[0].x * view[2][0] + v[0].y * view[2][1] + v[0].z * view[2][2];

    ViewPlane[1].nrm.x = v[1].x * view[0][0] + v[1].y * view[0][1] + v[1].z * view[0][2];
    ViewPlane[1].nrm.y = v[1].x * view[1][0] + v[1].y * view[1][1] + v[1].z * view[1][2];
    ViewPlane[1].nrm.z = v[1].x * view[2][0] + v[1].y * view[2][1] + v[1].z * view[2][2];

    ViewPlane[2].nrm.x = v[2].x * view[0][0] + v[2].y * view[0][1] + v[2].z * view[0][2];
    ViewPlane[2].nrm.y = v[2].x * view[1][0] + v[2].y * view[1][1] + v[2].z * view[1][2];
    ViewPlane[2].nrm.z = v[2].x * view[2][0] + v[2].y * view[2][1] + v[2].z * view[2][2];

    ViewPlane[3].nrm.x = v[3].x * view[0][0] + v[3].y * view[0][1] + v[3].z * view[0][2];
    ViewPlane[3].nrm.y = v[3].x * view[1][0] + v[3].y * view[1][1] + v[3].z * view[1][2];
    ViewPlane[3].nrm.z = v[3].x * view[2][0] + v[3].y * view[2][1] + v[3].z * view[2][2];

    ViewPlane[0].nrm.x = -ViewPlane[0].nrm.x;
    ViewPlane[0].nrm.y = -ViewPlane[0].nrm.y;
    ViewPlane[0].nrm.z = -ViewPlane[0].nrm.z;
    ViewPlane[1].nrm.x = -ViewPlane[1].nrm.x;
    ViewPlane[1].nrm.y = -ViewPlane[1].nrm.y;
    ViewPlane[1].nrm.z = -ViewPlane[1].nrm.z;
    ViewPlane[2].nrm.x = -ViewPlane[2].nrm.x;
    ViewPlane[2].nrm.y = -ViewPlane[2].nrm.y;
    ViewPlane[2].nrm.z = -ViewPlane[2].nrm.z;
    ViewPlane[3].nrm.x = -ViewPlane[3].nrm.x;
    ViewPlane[3].nrm.y = -ViewPlane[3].nrm.y;
    ViewPlane[3].nrm.z = -ViewPlane[3].nrm.z;

    ViewPlane[0].d = (pos.x * ViewPlane[0].nrm.x + pos.y * ViewPlane[0].nrm.y + pos.z * ViewPlane[0].nrm.z);
    ViewPlane[1].d = (pos.x * ViewPlane[1].nrm.x + pos.y * ViewPlane[1].nrm.y + pos.z * ViewPlane[1].nrm.z);
    ViewPlane[2].d = (pos.x * ViewPlane[2].nrm.x + pos.y * ViewPlane[2].nrm.y + pos.z * ViewPlane[2].nrm.z);
    ViewPlane[3].d = (pos.x * ViewPlane[3].nrm.x + pos.y * ViewPlane[3].nrm.y + pos.z * ViewPlane[3].nrm.z);
}

void MODELR::LostRender()
{
    skinnedStreams_.clear();
    gpuSkinning_.clear();
    root->ReleaseGeometry();
}

void MODELR::RestoreRender()
{
    root->RestoreGeometry();
    skinnedStreams_.clear(); // First draw uploads each restored source buffer.
    gpuSkinning_.clear();
}
