//============================================================================================
//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    CameraFollow
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#include "camera_follow.h"
#include "character.h"
#include "location_camera.h"

//============================================================================================

//#define LFC_DEBUG

#define LCF_RADIUS 0.25f

// Radius search options
float CameraFollow::fndRadius;
float CameraFollow::fndMaxRadius;
glm::vec3 CameraFollow::fndCamDir;
glm::vec3 CameraFollow::fndCamDirXZ;
glm::vec3 CameraFollow::fndCamDirY;
glm::vec3 CameraFollow::fndCamPos;
float CameraFollow::fndcsAx;
float CameraFollow::fndkAx;
float CameraFollow::fndcsAy;
float CameraFollow::fndkAy;

// ============================================================================================
// Construction, destruction
// ============================================================================================

CameraFollow::CameraFollow()
    : camPos(), lookTo(), camradius(0), rotSpd(0), rotAcc(0)
{
    lc = nullptr;
    isBrn = true;
    camay = 0.0f;
    kRadInert = 1.0f;
    kRadInertCur = 1.0f;
}

CameraFollow::~CameraFollow()
{
}

void CameraFollow::SetLocationCamera(LocationCamera *locCamera)
{
    lc = locCamera;
    camradius = lc->radius;
}

//============================================================================================

// Make calculations
void CameraFollow::Update(float dltTime)
{
    auto oldPos(camPos);
    // If beyond the permitted area, reborn
    if (storm::math::lengthSquared(lc->pos - oldPos) >= lc->teleport_dist * lc->teleport_dist)
        isBrn = true;
    // Calculating a new position
    if (!isBrn)
    {
        MoveCamera(dltTime);
    }
    else
    {
        BornCamera();
        isBrn = false;
        oldPos = camPos;
    }
    // recalculate the angle
    /*
    double vz = lookTo.z - camPos.z;
    double vx = lookTo.x - camPos.x;
    double lv = vx*vx + vz*vz;
    if(lv > 0.0000001)
    {
      // Calculating angles
      vz = acos(vz/sqrt(lv));
      if(vx < 0) vz = -vz;
      camay = float(vz);
    }*/
    lc->camPos = camPos;
    lc->lookTo = lookTo;
    // Limit in height
    if (lc->camPos.y < lc->pos.y + lc->lheight * 0.5f)
        lc->camPos.y = lc->pos.y + lc->lheight * 0.5f;

    /*
    // Check if the camera is stuck
    float k1 = Trace(lookTo, camPos);
    if(k1 < 1.0f)
    {
      if(Trace(camPos, lookTo) <= 1.0f)
      {
        glm::vec3 dir = camPos - lookTo;
        float l = ~dir;
        if(l > 0.0f)
        {
          k1 -= LCF_RADIUS/sqrtf(l);
          if(k1 < 0.0f) k1 = 0.0f;
        }
        camPos = lookTo + (camPos - lookTo)*k1;
      }
    }
    //
    glm::vec3 dir = camPos - lookTo; dir.y = 0.0f;
    float r = sqrtf(~dir);
    lc->camPos = camPos;
    lc->lookTo = lookTo;
    if(r < lc->chradius*1.5f)
    {
      float k = 1.0f - r/(lc->chradius*1.5f);
      float clipH = lc->lookTo.y + k*(lc->height - lc->lheight + 0.2f)*1.2f;
      if(lc->camPos.y < clipH)
      {
        lc->lookTo.y += clipH - lc->camPos.y;
        lc->camPos.y = clipH;
      }
    }*/
}

// Find current camera position
void CameraFollow::MoveCamera(float dltTime)
{
    if (dltTime <= 0.0f)
        return;
#ifdef __ANDROID__
    // Mobile camera mode (owner decision 2026-11-11). The native follow camera swings behind
    // the hero (likeCamay = chay below) and eases towards that heading with rotInertia; a
    // touch port added (c665430f, 2026-10-18) an orbit for a held stick that eased towards
    // camay + this frame's drag, so only dltTime * rotInertia of each frame's drag was applied
    // and the rest was thrown away: about 1/24 at 60 fps, the camera "almost not responding"
    // while running, while a fight (native turn) responded fully. On mobile, out of a fight,
    // the camera turns only from the player's drag, by all of it, and never re-centres behind
    // the hero; the radius still follows obstacles (FindRadius). The hero turns only with the
    // stick (Player::Rotate ignores the drag here), so the camera and the travel vector never
    // fight each other.
    if (lc->character->MobileCameraOrbit() && !lc->isSpecialMode)
    {
        camay += lc->dragAy;
        lookTo = lc->pos;
        lookTo.y += lc->lheight;
        ChangeRadius(dltTime, FindRadius(camay));
        CalcPosition(camay, camradius, 0.0f, camPos);
        return;
    }
#endif
    // Where are we looking
    auto likeCamay = lc->chay;
    if (lc->isSpecialMode)
    {
        likeCamay = camay + lc->dAy;
    }
    lookTo = lc->pos;
    lookTo.y += lc->lheight;
    // decide what to do
    const auto pi = 3.14159265359f;
    auto chay = likeCamay - static_cast<int32_t>(likeCamay / pi) * 2.0f * pi;
    const auto cay = camay - static_cast<int32_t>(camay / pi) * 2.0f * pi;
    if (cay - chay > pi)
        chay += 2 * pi;
    if (cay - chay < -pi)
        chay -= 2 * pi;
    const auto dAng = chay - cay;
    // Calculate the current radius
    kRadInert = fabsf(dAng) / 0.05f;
    if (kRadInert > 1.0f)
        kRadInert = 1.0f;
    kRadInert = powf(kRadInert, 5.0f);
    if (fabsf(dAng) > 0.001f)
    {
        // The camera rotates around the character
        auto k = dltTime * lc->rotInertia;
        if (k > 1.0f)
            k = 1.0f;
        camay = cay + dAng * k;
        lc->character->LockRotate(true);
    }
    ChangeRadius(dltTime, FindRadius(camay));
    CalcPosition(camay, camradius, 0.0f, camPos);
}

// Reinitialize camera position
void CameraFollow::BornCamera()
{
    // New camera options
    camay = lc->chay;
    camradius = lc->radius;
    // Where are we looking
    lookTo = lc->pos;
    lookTo.y += lc->lheight;
    // Where are we looking from
    CalcPosition(camay, camradius, 0.0f, camPos);
    // Checking the ray intersection
    constexpr auto kp = 2.0f;
    // ~!~
    lc->Trace(lookTo, camPos);
    if constexpr (kp < 1.0f)
        camPos = lookTo + (camPos - lookTo) * (kp * 0.9f);
}

// Calculate camera position for a given angle
void CameraFollow::CalcPosition(float ang, float radius, float dax, glm::vec3 &pos) const
{
    // Where are we looking from
    const auto ax = -lc->ax + dax;
    pos.x = lc->pos.x - radius * cosf(ax) * sinf(ang);
    pos.y = lc->pos.y + lc->lheight - radius * sinf(ax);
    pos.z = lc->pos.z - radius * cosf(ax) * cosf(ang);
}

#ifdef LFC_DEBUG

glm::vec3 fndv[4096 * 3];
int32_t numTrg = 0;
bool isFndTrg = false;
glm::vec3 fndvP1[4096];
int32_t numP1 = 0;
glm::vec3 fndvP2[4096];
int32_t numP2 = 0;
glm::vec3 fndvP3[4096 * 10];
int32_t numP3 = 0;

glm::vec3 fndvN[4096 * 10];
int32_t numN = 0;
glm::vec3 fndvE[4096 * 10];
int32_t numE = 0;

glm::vec3 fndvU[4096 * 10];
int32_t numU = 0;

int32_t numPoly1 = 0;

int camSteps = 0;

#endif

void CameraFollow::DrawDebug()
{
#ifdef LFC_DEBUG
    lc->location->GetRS()->SetTransform(D3DTS_WORLD, glm::mat4());
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xff00ff00);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, D3DFVF_XYZ, numTrg, fndv, sizeof(glm::vec3), "DebugTrs");
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xffffff00);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_POINTLIST, D3DFVF_XYZ, numP1, fndvP1, sizeof(glm::vec3), "DebugTrs");
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xffffffff);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_POINTLIST, D3DFVF_XYZ, numP2, fndvP2, sizeof(glm::vec3), "DebugTrs");
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xffff0000);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_LINELIST, D3DFVF_XYZ, numP3 / 2, fndvP3, sizeof(glm::vec3), "DebugTrs");
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xffff00ff);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_LINELIST, D3DFVF_XYZ, numN / 2, fndvN, sizeof(glm::vec3), "DebugTrs");
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xff0000ff);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_LINELIST, D3DFVF_XYZ, numE / 2, fndvE, sizeof(glm::vec3), "DebugTrs");
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xff00ffff);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_LINELIST, D3DFVF_XYZ, numU / 2, fndvU, sizeof(glm::vec3), "DebugTrs");
    lc->location->GetRS()->Print(10, 90, "NumEdges: %i, NumU %i", numE / 2, numU / 2);
#endif
}

//
float CameraFollow::FindRadius(float curAng) const
{
    static const auto pi = 3.14159265359f;
    static const auto day = pi * 0.25f; // Vertical deviation
    static const auto dax = pi * 0.16f; // Vertical deviation

#ifdef LFC_DEBUG
    struct Vrt
    {
        glm::vec3 p;
        float rhw;
    } vrt[64];

    for (int32_t ii = 0; ii < 32; ii++)
    {
        float x = day * ii / 31.0f;
        vrt[ii * 2 + 0].p.x = 300.0f;
        vrt[ii * 2 + 0].p.y = 400.0f;
        vrt[ii * 2 + 0].p.z = 0.5f;
        vrt[ii * 2 + 0].rhw = 2.0f;
        vrt[ii * 2 + 1].p.x = vrt[ii * 2 + 0].p.x + 200.0f * sinf(x);
        vrt[ii * 2 + 1].p.y = vrt[ii * 2 + 0].p.y - 200.0f * cosf(x);
        vrt[ii * 2 + 1].p.z = 0.5f;
        vrt[ii * 2 + 1].rhw = 2.0f;
    }
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xffff0000);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_LINELIST, D3DFVF_XYZRHW, 32, vrt, sizeof(Vrt), "DebugTrs");

    for (ii = 0; ii < 32; ii++)
    {
        float x = day * ii / 31.0f;
        float y = (cosf(x) - cosf(day)) / (1.0f - cosf(day));

        y = 200.0f + (100.0f - 200.0f) * (1.0f - powf(1.0f - y, 1.5f));

        vrt[ii * 2 + 0].p.x = 300.0f;
        vrt[ii * 2 + 0].p.y = 400.0f;
        vrt[ii * 2 + 0].p.z = 0.5f;
        vrt[ii * 2 + 0].rhw = 2.0f;
        vrt[ii * 2 + 1].p.x = vrt[ii * 2 + 0].p.x + y * sinf(x);
        vrt[ii * 2 + 1].p.y = vrt[ii * 2 + 0].p.y - y * cosf(x);
        vrt[ii * 2 + 1].p.z = 0.5f;
        vrt[ii * 2 + 1].rhw = 2.0f;
    }
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xff00ff00);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_LINELIST, D3DFVF_XYZRHW, 32, vrt, sizeof(Vrt), "DebugTrs");

    for (ii = 0; ii < 32; ii++)
    {
        float x = day * ii / 31.0f;
        vrt[ii * 2 + 0].p.x = 300.0f;
        vrt[ii * 2 + 0].p.y = 400.0f;
        vrt[ii * 2 + 0].p.z = 0.5f;
        vrt[ii * 2 + 0].rhw = 2.0f;
        vrt[ii * 2 + 1].p.x = vrt[ii * 2 + 0].p.x + 100.0f * sinf(x);
        vrt[ii * 2 + 1].p.y = vrt[ii * 2 + 0].p.y - 100.0f * cosf(x);
        vrt[ii * 2 + 1].p.z = 0.5f;
        vrt[ii * 2 + 1].rhw = 2.0f;
    }
    lc->location->GetRS()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xff0000ff);
    lc->location->GetRS()->DrawPrimitiveUP(D3DPT_LINELIST, D3DFVF_XYZRHW, 32, vrt, sizeof(Vrt), "DebugTrs");

#endif

    // The point we are looking at
    glm::vec3 pos(lc->pos.x, lc->pos.y + lc->lheight, lc->pos.z);
    const auto ax = -lc->ax;
    // Search initial values
    fndRadius = lc->radius + LCF_RADIUS;
    fndMaxRadius = lc->radius + LCF_RADIUS;
    fndCamDir = glm::vec3(-cosf(ax) * sinf(curAng), -sinf(ax), -cosf(ax) * cosf(curAng));
    fndCamDirXZ = fndCamDir;
    fndCamDirXZ.y = 0.0f;
    auto l = sqrtf(storm::math::lengthSquared(fndCamDirXZ));
    if (l > 0.0f)
        fndCamDirXZ *= 1.0f / l;
    fndCamDirY = fndCamDir;
    fndCamDirY.x = l;
    fndCamDirY.z = 0.0f;
    l = storm::math::lengthSquared(fndCamDirY);
    if (l > 0.0f)
        fndCamDirY *= 1.0f / sqrtf(l);
    fndCamPos = pos;
    fndcsAx = cosf(dax);
    fndkAx = 1.0f / (1.0f - fndcsAx);
    fndcsAy = cosf(day);
    fndkAy = 1.0f / (1.0f - fndcsAy);
    // Building a bounding volume
    static PLANE p[5];
    // Left plane
    p[0].Nx = -cosf(curAng + day);
    p[0].Ny = 0.0f;
    p[0].Nz = sinf(curAng + day);
    p[0].D = pos.x * p[0].Nx + pos.z * p[0].Nz;
    // Right plane
    p[1].Nx = cosf(curAng - day);
    p[1].Ny = 0.0f;
    p[1].Nz = -sinf(curAng - day);
    p[1].D = pos.x * p[1].Nx + pos.z * p[1].Nz;
    // Bottom plane
    p[2].Nx = sinf(ax + dax) * sinf(curAng);
    p[2].Ny = -cosf(ax + dax);
    p[2].Nz = sinf(ax + dax) * cosf(curAng);
    p[2].D = pos.x * p[2].Nx + pos.y * p[2].Ny + pos.z * p[2].Nz;
    // Top plane
    p[3].Nx = -sinf(ax - dax) * sinf(curAng);
    p[3].Ny = cosf(ax - dax);
    p[3].Nz = -sinf(ax - dax) * cosf(curAng);
    p[3].D = pos.x * p[3].Nx + pos.y * p[3].Ny + pos.z * p[3].Nz;
    // Back side
    p[4].Nx = fndCamDir.x;
    p[4].Ny = fndCamDir.y;
    p[4].Nz = fndCamDir.z;
    p[4].D = glm::dot(fndCamDir, pos + fndCamDir * lc->radius);

#ifdef LFC_DEBUG
    numPoly1 = 0;
    camSteps = 0;
    //!!!
    if (core.Controls->GetDebugAsyncKeyState('8') < 0)
    {
        isFndTrg = true;
        numTrg = 0;
        numP1 = 0;
        numP2 = 0;
        numP3 = 0;
        numN = 0;
        numE = 0;
        numU = 0;
    }
#endif

    // looking for a radius
    lc->Clip(p, 5, pos, lc->radius, ApplyPoly);

#ifdef LFC_DEBUG
    lc->location->GetRS()->Print(10, 10, "NumPoly: %i, steps: %i", numPoly1, camSteps);
    lc->location->GetRS()->Print(10, 30, "pos: %f, %f, %f", pos.x, pos.y, pos.z);
    lc->location->GetRS()->Print(10, 50, "curAng: %f, ax: %f rad = %f", curAng, ax, lc->radius);

#endif

    // isFndTrg = false;

#ifdef LFC_DEBUG
    /////////////////////
    static glm::vec3 ps(0.0f);
    static glm::vec3 vv[5];
    if (core.Controls->GetDebugAsyncKeyState('8') < 0)
    {
        ps = pos;
        glm::vec3 vvv;
        CalcPosition(curAng, 2.0f, 0.0f, vvv);

        vv[0] = ps + glm::vec3(p[0].Nx, p[0].Ny, p[0].Nz);
        vv[1] = ps + glm::vec3(p[1].Nx, p[1].Ny, p[1].Nz);
        vv[2] = ps + glm::vec3(p[2].Nx, p[2].Ny, p[2].Nz);
        vv[3] = ps + glm::vec3(p[3].Nx, p[3].Ny, p[3].Nz);
        vv[4] = ps + (vvv - pos);
    }

    lc->location->DrawLine(ps, 0xffff0000, vv[0], 0xffffff00);
    lc->location->DrawLine(ps, 0xff00ff00, vv[1], 0xffffff00);
    lc->location->DrawLine(ps, 0xff0000ff, vv[2], 0xffffff00);
    lc->location->DrawLine(ps, 0xffff00ff, vv[3], 0xffffff00);
    lc->location->DrawLine(ps, 0xffff00ff, vv[4], 0xffffff00);

    Assert(p[0].Ny == 0.0f);
    Assert(p[1].Ny == 0.0f);
    /////////////////////
#endif

    fndRadius -= LCF_RADIUS;
    if (fndRadius < 0.1f)
        fndRadius = 0.1f;
    return fndRadius;
}

// Consider polygon in radius search
bool CameraFollow::ApplyPoly(const glm::vec3 *v, int32_t n)
{
#ifdef LFC_DEBUG
    numPoly1++;
#endif
    if (n < 3)
        return true;
    // Plane normal
    auto norm = glm::cross(v[0] - v[1], v[0] - v[2]);
    auto dist = storm::math::lengthSquared(norm);
    if (dist <= 1e-10f)
        return true;
    norm *= 1.0f / sqrtf(dist);
    // Distance
    dist = glm::dot(v[0], norm);
    // Distance to the plane of the polygon
    auto d = glm::dot(fndCamPos, norm) - dist;
    // If the plane is farther than the current radius, skip it
    if (fabs(d) >= fndRadius)
        return true;
    // Filling in the parameters of the planes passing through the edges
    static EdgeInfo edge[32];
    if (n > 32)
        n = 32;
    for (int32_t i = 0; i < n; i++)
    {
        // Edge
        auto j = i + 1;
        if (j >= n)
            j = 0;
        // Normal of a plane passing through an edge
        edge[i].n = glm::cross(v[i] - v[j], norm);
        const auto d = storm::math::lengthSquared(edge[i].n);
        if (d == 0.0f)
            continue;
        edge[i].n *= 1.0f / sqrtf(d);
        // Distance
        edge[i].d = glm::dot(v[i], edge[i].n);
        edge[i].s = v[i];
        edge[i].e = v[j];
    }
    // looking for the closest point to the position where the camera is looking
    const auto pos1 = fndCamPos - norm * d;
    // looking for the closest point to the point of intersection of the ray direction to the camera
    const auto cs = glm::dot(norm, fndCamDir);
    if (cs != 0.0f)
    {
        d /= -cs;
        if (d >= 0.0f)
        {
            const auto pos2 = fndCamPos + fndCamDir * d;
            const auto dlt = pos2 - pos1;
            auto step = sqrtf(storm::math::lengthSquared(dlt)) * 3.0f;
            if (step > 0.0f)
            {
                if (step > 10.0f)
                    step = 10.0f;
                step = static_cast<float>(static_cast<int>(step));
                if (step < 1.0f)
                    step = 1.0f;
#ifdef LFC_DEBUG
                if (camSteps < int(step))
                    camSteps = int(step);
#endif
                step = 1.0f / step;
                for (auto k = 0.0f; k <= 1.0f; k += step)
                {
                    ApplyPolyFindNearest(edge, n, pos1 + dlt * k);
                }
            }
            else
                ApplyPolyFindNearest(edge, n, pos1);
        }
        else
            ApplyPolyFindNearest(edge, n, pos1);
    }
    else
        ApplyPolyFindNearest(edge, n, pos1);
    return true;
}

void CameraFollow::ApplyPolyFindNearest(EdgeInfo *e, int32_t ne, const glm::vec3 &pos)
{
    // Testing with edges, determining the closest distance
    glm::vec3 pnt;
    auto minDist = -1.0f;
    for (int32_t i = 0; i < ne; i++)
    {
        // Distance to planes from points
        auto d = glm::dot(pos, e[i].n) - e[i].d;
        if (d >= 0.0f)
            continue;
        // Point on a straight line
        auto p = pos - e[i].n * d;
        // Point on the edge
        if (glm::dot(e[i].s - e[i].e, e[i].s - p) < 0.0f)
            p = e[i].s;
        else if (glm::dot(e[i].s - e[i].e, p - e[i].e) < 0.0f)
            p = e[i].e;
        // Distance
        d = storm::math::lengthSquared(p - pos);
        if (minDist >= 0.0f)
        {
            if (minDist > d)
            {
                minDist = d;
                pnt = p;
            }
        }
        else
        {
            minDist = d;
            pnt = p;
        }
    }
    // Direction vector to the nearest point
    if (minDist < 0.0f)
        pnt = pos - fndCamPos;
    else
        pnt = pnt - fndCamPos;
    // Length
    auto dist = storm::math::lengthSquared(pnt);
    if (dist >= fndRadius * fndRadius)
        return;
    // Attenuation coefficient
    // XZ
    auto d = sqrtf(pnt.x * pnt.x + pnt.z * pnt.z);
    auto kxz = 1.0f;
    if (d > 0.0f)
    {
        kxz = glm::dot(pnt, fndCamDirXZ) / d;
        kxz = (kxz - fndcsAy) * fndkAy;
        if (kxz <= 0.0f)
            return;
    }
    // Y
    auto norm = pnt;
    pnt.x = d;
    pnt.z = 0.0f;
    d = sqrtf(pnt.x * pnt.x + pnt.y * pnt.y);
    auto ky = 1.0f;
    if (d > 0.0f)
    {
        ky = glm::dot(pnt, fndCamDirY) / d;
        ky = (ky - fndcsAx) * fndkAx;
        if (ky <= 0.0f)
            return;
    }
    // Distance taking into account correction coefficients
    auto oldDist = dist;
    dist = fndMaxRadius + (sqrtf(dist) - fndMaxRadius) * static_cast<float>(1.0 - powf(fabsf(1.0f - kxz * ky), 1.5f));
    Assert(dist >= 0.0f);
    if (dist < fndRadius)
        fndRadius = dist;
}

// Change radius
void CameraFollow::ChangeRadius(float dltTime, float radius)
{
    if (radius < 0.0f)
        radius = 0.0f;
    if (radius > lc->radius)
        radius = lc->radius;
    auto k = dltTime * 10.0f;
    if (k > 0.99f)
        k = 0.99f;
    // kRadInertCur = kRadInert + (kRadInertCur - kRadInert)*k;
    // k = dltTime*lc->radInertia*(0.1f + kRadInertCur*10.0f);
    if (radius > camradius)
        k *= 0.7f;
    if (k < 0.0f)
        k = 0.0f;
    if (k > 1.0f)
        k = 1.0f;
    camradius += (radius - camradius) * k;
    //    lc->location->GetRS()->Print(10, 70, "fndRadius = %f, camradius = %f", fndRadius, camradius);
}

// Find the acceleration of the camera stopping at an angular collision with the patch, dir +-1
void CameraFollow::FindRotAccelCam()
{
    rotAcc = 0.0f;
    if (rotSpd > 0.0f)
        rotAcc = -3.0f;
    if (rotSpd < 0.0f)
        rotAcc = 3.0f;
}

// Stretch the ray taking into account cull and camera size
float CameraFollow::Trace(const glm::vec3 &src, const glm::vec3 &dst)
{
    // Direction to camera
    auto dir = dst - src;
    auto dist = storm::math::lengthSquared(dir);
    if (dist <= 0.0f)
        return 2.0f;
    dist = sqrtf(dist);
    dir *= (dist + LCF_RADIUS) / dist;
    // Center ray check
    float k[5];
    k[0] = SubTrace(src, src + dir);
    // Building a basis
    auto left = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));
    const auto l = storm::math::lengthSquared(left);
    if (l <= 0.0f)
        return k[0];
    left *= 1.0f / sqrtf(l);
    const auto up = glm::cross(dir, left);
    // Looking for the nearest distance
    glm::vec3 s;
    s = src + left * LCF_RADIUS;
    k[1] = SubTrace(s, s + dir);
    s = src - left * LCF_RADIUS;
    k[2] = SubTrace(s, s + dir);
    s = src + up * LCF_RADIUS;
    k[3] = SubTrace(s, s + dir);
    s = src - up * LCF_RADIUS;
    k[4] = SubTrace(s, s + dir);
    auto kRes = 2.0f;
    for (int32_t i = 0; i < 5; i++)
        if (kRes > k[i])
            kRes = k[i];
    return kRes;
}

// Stretch a ray with cull
inline float CameraFollow::SubTrace(const glm::vec3 &src, const glm::vec3 &dst) const
{
    TRIANGLE trg;
    const auto k = lc->Trace(src, dst);
    if (k > 1.0f || !lc->GetCollideTriangle(trg))
        return 2.0f;
    // Determine the normal
    auto n = glm::cross(trg.vrt[0] - trg.vrt[1], trg.vrt[1] - trg.vrt[2]);
    const auto l = storm::math::lengthSquared(n);
    if (l > 0.0f)
    {
        // Determine which side we pass through the triangle
        n *= 1.0f / sqrtf(l);
        if (glm::dot(n, src) > glm::dot(n, trg.vrt[0]))
            return k;
    }
    return 2.0f;
}
