#include "core.h"
#include <storm/render/far_lod.hpp>

#include "entity.h"
#include "math_inlines.h"
#include "sea.h"
#include "shared/messages.h"

#include "shared/layers.h"

void SEA::EnvMap_GetSideMatrix(D3DCUBEMAP_FACES Face, glm::mat4 &mView)
{
    glm::vec3 vLookTo, vUp;

    switch (Face)
    {
    case D3DCUBEMAP_FACE_POSITIVE_X:
        vLookTo = glm::vec3(1.0f, 0.0f, 0.0f);
        vUp = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case D3DCUBEMAP_FACE_NEGATIVE_X:
        vLookTo = glm::vec3(-1.0f, 0.0f, 0.0f);
        vUp = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case D3DCUBEMAP_FACE_POSITIVE_Y:
        vLookTo = glm::vec3(0.0f, 1.0f, 0.0f);
        vUp = glm::vec3(0.0f, 0.0f, -1.0f);
        break;
    case D3DCUBEMAP_FACE_NEGATIVE_Y:
        vLookTo = glm::vec3(0.0f, -1.0f, 0.0f);
        vUp = glm::vec3(0.0f, 0.0f, 1.0f);
        break;
    case D3DCUBEMAP_FACE_POSITIVE_Z:
        vLookTo = glm::vec3(0.0f, 0.0f, 1.0f);
        vUp = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case D3DCUBEMAP_FACE_NEGATIVE_Z:
        vLookTo = glm::vec3(0.0f, 0.0f, -1.0f);
        vUp = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    }

    storm::math::viewMatrix(mView, glm::vec3(0.0f), vLookTo, vUp);
}

bool SEA::SunRoad_Render2()
{
    namespace far_lod = storm::render::far_lod;
    struct PassScope // the far LODs choose for this target (storm/render/far_lod.hpp)
    {
        PassScope() { far_lod::pass = far_lod::Reflection; }
        ~PassScope() { far_lod::pass = far_lod::Main; }
    } pass_scope;
    glm::mat4 mOldView = rs->GetView();
    glm::mat4 mOldProjection = rs->GetProjection();

    rs->EndScene();
    rs->PushRenderTarget();

    // rs->SetProjection( glm::mat4().BuildProjectionMatrix(PI / 2.0f, 256.0f, 256.0f, 1.0f, 4000.0f) );

    auto PlaneHeight = 0.5f;

    const glm::vec3 point(0, PlaneHeight, 0), normal(0, 1, 0);
    const auto mPlane = storm::math::planeFromPointNormal(normal, point);

    glm::mat4 Invertor;
    Invertor = storm::math::mirrorMatrix(mPlane.x, mPlane.y, mPlane.z, mPlane.w);

    glm::mat4 mView = rs->GetView();
    glm::mat4 mViewNew = mView;

    mViewNew = mViewNew * Invertor;

    rs->SetView(mViewNew);

    uint32_t Colors[6] = {0xd934c8, 0x2FFF1F, 0x0000FF, 0xFF00, 0xb28e11, 0x0};
    // for (uint32_t i=0; i<6; i++)
    {
        // if (i == D3DCUBEMAP_FACE_NEGATIVE_Y) continue;
        // if (i != D3DCUBEMAP_FACE_NEGATIVE_Z) continue;

        rs->BeginScene();
        bool bCool = rs->SetRenderTarget(reflectionSunroad, 0, 0, zStencil);
        bCool = D3D_OK != rs->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, 0x0, 1.0f, 0);

        // glm::mat4 mView;
        // EnvMap_GetSideMatrix((D3DCUBEMAP_FACES)i, mView);

        // mView.m[3][0] = -(mView.m[0][0] * vCamPos.x - mView.m[1][0] * vCamPos.y + mView.m[2][0] * vCamPos.z);
        // mView.m[3][1] = -(mView.m[0][1] * vCamPos.x - mView.m[1][1] * vCamPos.y + mView.m[2][1] * vCamPos.z);
        // mView.m[3][2] = -(mView.m[0][2] * vCamPos.x - mView.m[1][2] * vCamPos.y + mView.m[2][2] * vCamPos.z);

        // rs->SetView(mView);

        // Render scene here.
        // uint32_t dwSkyCode = MakeHashValue("sky");

        if (!core.IsLayerFrozen(SEA_REFLECTION2))
        {
            auto &&entities = core.GetEntityIds(SEA_SUNROAD);
            for (auto ent_id : entities)
            {
                const auto hash = core.GetClassCode(ent_id);
                if (hash != dwShipCode && hash != dwSailCode && hash != dwIslandCode)
                {
                    core.Send_Message(ent_id, "ll", MSG_SEA_SUNROAD_DRAW, static_cast<int32_t>(bSimpleSea));
                }
            }
        }

        rs->EndScene();
    }

    rs->PopRenderTarget();
    rs->SetView(mOldView);
    rs->SetProjection(mOldProjection);

    rs->BeginScene();
    mOldView = rs->GetView();

    return true;
}

bool SEA::EnvMap_Render2()
{
    namespace far_lod = storm::render::far_lod;
    struct PassScope // the far LODs choose for this target (storm/render/far_lod.hpp)
    {
        PassScope() { far_lod::pass = far_lod::Reflection; }
        ~PassScope() { far_lod::pass = far_lod::Main; }
    } pass_scope;
    glm::mat4 mOldView = rs->GetView();
    glm::mat4 mOldProjection = rs->GetProjection();
    rs->EndScene();
    rs->PushRenderTarget();

    // rs->SetProjection( glm::mat4().BuildProjectionMatrix(PI / 2.0f, 256.0f, 256.0f, 1.0f, 4000.0f) );

    auto PlaneHeight = 0.5f;

    const glm::vec3 point(0, PlaneHeight, 0), normal(0, 1, 0);
    const auto mPlane = storm::math::planeFromPointNormal(normal, point);

    glm::mat4 Invertor;
    Invertor = storm::math::mirrorMatrix(mPlane.x, mPlane.y, mPlane.z, mPlane.w);

    glm::mat4 mView = rs->GetView();
    glm::mat4 mViewNew = mView;

    mViewNew = mViewNew * Invertor;

    rs->SetView(mViewNew);

    // rs->SetEffect("FlatSeaReverseCull");
    // Event("SeaReflection");

    uint32_t Colors[6] = {0xd934c8, 0x2FFF1F, 0x0000FF, 0xFF00, 0xb28e11, 0x0};
    // for (uint32_t i=0; i<6; i++)
    {
        // if (i == D3DCUBEMAP_FACE_NEGATIVE_Y) continue;
        // if (i != D3DCUBEMAP_FACE_NEGATIVE_Z) continue;

        rs->BeginScene();
        bool bCool = rs->SetRenderTarget(reflection, 0, 0, reflectionDepth);
        bCool = D3D_OK != rs->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, 0x0, 1.0f, 0);

        // glm::mat4 mView;
        // EnvMap_GetSideMatrix((D3DCUBEMAP_FACES)i, mView);

        // mView.m[3][0] = -(mView.m[0][0] * vCamPos.x - mView.m[1][0] * vCamPos.y + mView.m[2][0] * vCamPos.z);
        // mView.m[3][1] = -(mView.m[0][1] * vCamPos.x - mView.m[1][1] * vCamPos.y + mView.m[2][1] * vCamPos.z);
        // mView.m[3][2] = -(mView.m[0][2] * vCamPos.x - mView.m[1][2] * vCamPos.y + mView.m[2][2] * vCamPos.z);

        // core.Trace("sea: %.3f, %.3f, %.3f", mView.m[3][0], mView.m[3][1], mView.m[3][2]);
        // rs->SetView(mView);

        // Render scene here.
        {
            auto &&entities = core.GetEntityIds(SEA_REFLECTION);
            for (auto ent_id : entities)
            {
                core.Send_Message(ent_id, "ll", MSG_SEA_REFLECTION_DRAW, static_cast<int32_t>(bSimpleSea));
            }
        }
        {
            auto &&entities = core.GetEntityIds(SEA_REFLECTION2);
            for (auto ent_id : entities)
            {
                core.Send_Message(ent_id, "ll", MSG_SEA_REFLECTION_DRAW, static_cast<int32_t>(bSimpleSea));
            }
        }

        rs->EndScene();
    }

    rs->PopRenderTarget();
    rs->SetView(mOldView);
    rs->SetProjection(mOldProjection);

    rs->BeginScene();

    glm::mat4 mTex;
    mTex = storm::math::scaling(glm::vec3(0.5f, -0.5f, 0.5f));
    storm::math::pos(mTex) = glm::vec3(0.5f, 0.5f, 0.5f);

    glm::mat4 mProj = rs->GetProjection();
    mProj = mTex * mProj;

    glm::mat4 mCurWorld = rs->GetWorld();
    glm::mat4 mCurView = rs->GetView();

    mTexProjection = mProj * (mCurView * mCurWorld);

    return true;
}

uint32_t SEA::CubeFacesDue()
{
    if (cubeUnderSea_ != bUnderSea || storm::math::lengthSquared(vCamPos - cubeCamPos_) > kCubeJump * kCubeJump)
        cubeRefreshAll_ = true;
    cubeUnderSea_ = bUnderSea;
    cubeCamPos_ = vCamPos;
    uint32_t faces[6], count = 0;
    for (uint32_t i = 0; i < 6; i++)
        if (bUnderSea || i != D3DCUBEMAP_FACE_NEGATIVE_Y)
            faces[count++] = i;
    if (cubeRefreshAll_)
    {
        cubeRefreshAll_ = false;
        return (1u << 6) - 1;
    }
    uint32_t due = 0;
    for (uint32_t k = 0; k < kCubeFacesPerFrame; k++)
        due |= 1u << faces[cubeCursor_++ % count];
    return due;
}

bool SEA::SunRoad_Render()
{
    namespace far_lod = storm::render::far_lod;
    struct PassScope // the far LODs choose for this target (storm/render/far_lod.hpp)
    {
        PassScope() { far_lod::pass = far_lod::CubeFace; }
        ~PassScope() { far_lod::pass = far_lod::Main; }
    } pass_scope;
    glm::mat4 mOldView = rs->GetView();
    glm::mat4 mOldProjection = rs->GetProjection();

    rs->EndScene();
    rs->PushRenderTarget();

    rs->SetProjection(storm::math::projectionMatrix(PI / 2.0f, 256.0f, 256.0f, 1.0f, 4000.0f));

    uint32_t Colors[6] = {0xd934c8, 0x2FFF1F, 0x0000FF, 0xFF00, 0xb28e11, 0x0};
    for (uint32_t i = 0; i < 6; i++)
    {
        if ((!bUnderSea && i == D3DCUBEMAP_FACE_NEGATIVE_Y) || !(cubeFaces_ & (1u << i)))
            continue;

        rs->BeginScene();
        bool bCool = rs->SetRenderTarget(sunRoadMap, 0, i, zStencil);
        bCool = D3D_OK != rs->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, 0x0, 1.0f, 0);

        glm::mat4 mView;
        EnvMap_GetSideMatrix(static_cast<D3DCUBEMAP_FACES>(i), mView);

        mView[3][0] = -(mView[0][0] * vCamPos.x - mView[1][0] * vCamPos.y + mView[2][0] * vCamPos.z);
        mView[3][1] = -(mView[0][1] * vCamPos.x - mView[1][1] * vCamPos.y + mView[2][1] * vCamPos.z);
        mView[3][2] = -(mView[0][2] * vCamPos.x - mView[1][2] * vCamPos.y + mView[2][2] * vCamPos.z);

        rs->SetView(mView);

        // Render scene here.
        if (!core.IsLayerFrozen(SEA_REFLECTION2))
        {
            auto &&entities = core.GetEntityIds(SEA_SUNROAD);
            for (auto ent_id : entities)
            {
                const auto hash = core.GetClassCode(ent_id);
                if (hash != dwShipCode && hash != dwSailCode && hash != dwIslandCode)
                {
                    core.Send_Message(ent_id, "ll", MSG_SEA_SUNROAD_DRAW, static_cast<int32_t>(bSimpleSea));
                }
            }
        }

        rs->EndScene();
    }

    rs->PopRenderTarget();
    rs->SetView(mOldView);
    rs->SetProjection(mOldProjection);

    rs->BeginScene();
    mOldView = rs->GetView();

    return true;
}

bool SEA::EnvMap_Render()
{
    namespace far_lod = storm::render::far_lod;
    struct PassScope // the far LODs choose for this target (storm/render/far_lod.hpp)
    {
        PassScope() { far_lod::pass = far_lod::CubeFace; }
        ~PassScope() { far_lod::pass = far_lod::Main; }
    } pass_scope;
    glm::mat4 mOldView = rs->GetView();
    glm::mat4 mOldProjection = rs->GetProjection();
    rs->EndScene();
    rs->PushRenderTarget();

    rs->SetProjection(storm::math::projectionMatrix(PI / 2.0f, 256.0f, 256.0f, 1.0f, 4000.0f));

    uint32_t Colors[6] = {0xd934c8, 0x2FFF1F, 0x0000FF, 0xFF00, 0xb28e11, 0x0};
    for (uint32_t i = 0; i < 6; i++)
    {
        if ((!bUnderSea && i == D3DCUBEMAP_FACE_NEGATIVE_Y) || !(cubeFaces_ & (1u << i)))
            continue;

        rs->BeginScene();
        bool bCool = rs->SetRenderTarget(envMap, 0, i, zStencil);
        bCool = D3D_OK != rs->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, Colors[i] /*0x0*/, 1.0f, 0);

        glm::mat4 mView;
        EnvMap_GetSideMatrix(static_cast<D3DCUBEMAP_FACES>(i), mView);

        mView[3][0] = -(mView[0][0] * vCamPos.x - mView[1][0] * vCamPos.y + mView[2][0] * vCamPos.z);
        mView[3][1] = -(mView[0][1] * vCamPos.x - mView[1][1] * vCamPos.y + mView[2][1] * vCamPos.z);
        mView[3][2] = -(mView[0][2] * vCamPos.x - mView[1][2] * vCamPos.y + mView[2][2] * vCamPos.z);

        // core.Trace("sea: %.3f, %.3f, %.3f", mView.m[3][0], mView.m[3][1], mView.m[3][2]);
        rs->SetView(mView);

        // Render scene here.
        {
            auto &&entities = core.GetEntityIds(SEA_REFLECTION);
            for (auto ent_id : entities)
            {
                core.Send_Message(ent_id, "ll", MSG_SEA_REFLECTION_DRAW, static_cast<int32_t>(bSimpleSea));
            }
        }
        {
            auto &&entities = core.GetEntityIds(SEA_REFLECTION2);
            for (auto ent_id : entities)
            {
                core.Send_Message(ent_id, "ll", MSG_SEA_REFLECTION_DRAW, static_cast<int32_t>(bSimpleSea));
            }
        }

        rs->EndScene();
    }

    rs->PopRenderTarget();
    rs->SetView(mOldView);
    rs->SetProjection(mOldProjection);

    rs->BeginScene();

    return true;
}
