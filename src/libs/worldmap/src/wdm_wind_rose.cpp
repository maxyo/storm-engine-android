//============================================================================================
//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    WdmWindRose
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#include "wdm_wind_rose.h"

// ============================================================================================
// Construction, destruction
// ============================================================================================

WdmWindRose::WdmWindRose()
{
    shadowTexture = -1;
    glm::mat4 mscale, mrot = storm::math::eulerMatrix(0.0f, 3.1415926536f, 0.0f);
    mscale[0][0] = 0.028f;
    mscale[1][1] = 0.028f;
    mscale[2][2] = 0.028f;
    mtx = mscale * mrot;
    shadowTexture = wdmObjects->rs->TextureCreate("WorldMap\\Geometry\\compass_shadow.tga");
}

WdmWindRose::~WdmWindRose()
{
    if (shadowTexture >= 0)
        wdmObjects->rs->TextureRelease(shadowTexture);
}

// Calculations
void WdmWindRose::Update(float dltTime)
{
}

void WdmWindRose::LRender(VDX9RENDER *rs)
{
    static glm::mat4 view, prj, oldView, oldPrj;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(view));
    oldView = view;
    rs->GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(oldPrj));
    storm::math::invertRigid(view);
    const auto kDef = rs->GetHeightDeformator();
    storm::math::pos(mtx) = storm::math::transformPoint(view, glm::vec3(0.34f, -0.247f * kDef, 10.0f));

    float scrw, scrh;
    wdmObjects->GetVPSize(scrw, scrh);
    // prj.BuildProjectionMatrix(0.1f, scrw, scrh, 0.5f, 50.0f);
    prj = storm::math::projectionMatrix(0.1f, 4.0f, 3.0f, 0.5f, 50.0f);
    rs->SetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(prj));
    WdmRenderModel::LRender(rs);
    Vertex v[4];
    const auto size = 5.5f;
    const auto tsnap = 0.01f;
    v[0].x = -size;
    v[0].z = size;
    v[0].tu = 1.0f - tsnap;
    v[0].tv = 1.0f - tsnap;
    v[1].x = size;
    v[1].z = size;
    v[1].tu = tsnap;
    v[1].tv = 1.0f - tsnap;
    v[2].x = -size;
    v[2].z = -size;
    v[2].tu = 1.0f - tsnap;
    v[2].tv = tsnap;
    v[3].x = size;
    v[3].z = -size;
    v[3].tu = tsnap;
    v[3].tv = tsnap;
    v[0].y = v[1].y = v[2].y = v[3].y = -1.4f;
    v[0].c = v[1].c = v[2].c = v[3].c = 0x8f8f8f8f;

    rs->TextureSet(0, shadowTexture);
    rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v, sizeof(Vertex),
                        "WdmCompasShadow");

    rs->SetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(oldPrj));
    rs->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(oldView));
}
