#ifdef STORM_ENABLE_BGFX
#include "fog_snapshot.hpp"
#endif
#include "sky.h"
#include "sun_glow.h"
#include "core.h"
#include "attributes.h"
#include "math_inlines.h"
#include "fog_sphere_mesh.hpp"
#include "legacy_colored_mesh.hpp"
#include "legacy_textured_mesh.hpp"
#include "sky_mesh.hpp"
#include <algorithm>
#include <cmath>
#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
#include <string_view>
#endif

SKY::SKY()
{
    fAngleY = 0.0f;
    pRS = nullptr;
    memset(TexturesID, -1, sizeof(TexturesID));
    memset(TexturesNextID, -1, sizeof(TexturesNextID));
    pAstronomy = nullptr;
    pSunGlow = nullptr;
}

SKY::~SKY()
{
    Release();
}

void SKY::Release()
{
    for (int32_t i = 0; i < SKY_NUM_TEXTURES; i++)
    {
        if (TexturesID[i] >= 0)
        {
            pRS->TextureRelease(TexturesID[i]);
            TexturesID[i] = -1;
        }
        if (TexturesNextID[i] >= 0)
        {
            pRS->TextureRelease(TexturesNextID[i]);
            TexturesNextID[i] = -1;
        }
    }

    skyMesh_.reset();
    fogMesh_.reset();
    loadedSkyDirs_ = {};
    loadedSkySetCount_ = 0;
    failedSkySetCount_ = 0;
}

bool SKY::Init()
{
    SetDevice();
    return true;
}

void SKY::SetDevice()
{
    pRS = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
    Assert(pRS);
}

void SKY::UpdateFogSphere(const bool initialize)
{
    if (initialize && !fogMesh_) fogMesh_ = storm::render::makeLegacyColoredMesh(*pRS);
    if (!fogMesh_) return;

    static_assert(kFogVertsNum == storm::weather::fogVertexCount);
    static_assert(kFogTrgsNum == storm::weather::fogIndexCount);
    const auto mesh = storm::weather::makeFogSphere([](float x, float y, float z) {
        auto *value = core.Event(WEATHER_CALC_FOG_COLOR, "fff", x, y, z);
        Assert(value);
        return static_cast<uint32_t>(value->GetInt());
    }, initialize);

#ifdef STORM_ENABLE_BGFX
    storm::render::stageFogMesh(mesh, initialize);
#endif
    fogMesh_->update(mesh.vertices, initialize ? std::span<const uint16_t>(mesh.indices) : std::span<const uint16_t>{});
}

void SKY::GenerateSky(const bool initialize)
{
    if (initialize)
    {
        Release();
        LoadTextures();
    }

    if (!skyMesh_) skyMesh_ = storm::render::makeLegacyTexturedMesh(*pRS);

    const auto mesh=storm::weather::makeSkyMesh(fSkySize);
    skyMesh_->update(mesh.vertices,mesh.indices);

    UpdateFogSphere(true);
}

void SKY::LoadTextures()
{
    failedSkySetCount_ = 0; // Explicit reload also retries a previously missing asset.
    SelectSkyTextures(requestedSkyTimeFactor_);
    fAngleY = 0.0f;
}

bool SKY::CreateState(ENTITY_STATE_GEN *state_gen)
{
    return true;
}

bool SKY::LoadState(ENTITY_STATE *state)
{
    SetDevice();
    return true;
}

void SKY::Realize(uint32_t Delta_Time)
{
    fAngleY += static_cast<float>(Delta_Time) * 0.001f * fSkySpeedRotate;

    float fFov;
    CVECTOR vPos, vAng;
    CMatrix pMatWorld, pMatTranslate, pMatRotate;

    pRS->GetCamera(vPos, vAng, fFov);

    pMatTranslate.BuildPosition(vPos.x, vPos.y / 6.0f, vPos.z);
    pMatWorld = pMatWorld * pMatTranslate;
    pMatRotate.BuildRotateY(fAngleY + fSkyAngle);
    pMatWorld = pMatRotate * pMatWorld;
    pRS->SetTransform(D3DTS_WORLD, pMatWorld);

    const auto drawSky = [&](const std::string& material,uint32_t factor,bool transition) {
        if (!skyMesh_ || loadedSkySetCount_ == 0) return;
        std::array<storm::render::TextureDrawRange,10> ranges;
        size_t count=0;
        for (uint32_t i=0;i<SKY_NUM_TEXTURES;++i) {
            ranges[count++]={i*6,6,TexturesID[i],transition?TexturesNextID[i]:-1};
            if (Delta_Time==0) ranges[count++]={30+i*6,6,TexturesID[i],transition?TexturesNextID[i]:-1};
        }
        skyMesh_->submit({std::span<const float,16>(pMatWorld.matrix),{ranges.data(),count},material,factor});
    };

    if (aSkyDirArray.size() > 1)
        UpdateTimeFactor();
    else
        SelectSkyTextures(requestedSkyTimeFactor_);
    if (loadedSkySetCount_ == 2)
    {
        auto fBlendFactor = fTimeFactor - static_cast<int32_t>(fTimeFactor);
        if (fBlendFactor < 0.f)
            fBlendFactor = 0.f;
        if (fBlendFactor > 1.f)
            fBlendFactor = 1.f;
        uint32_t dwColor = (dwSkyColor & 0x00FFFFFF) | (static_cast<uint32_t>(255.f * fBlendFactor) << 24);
        pRS->SetRenderState(D3DRS_TEXTUREFACTOR, dwColor);

        drawSky(sTechSkyBlend, dwColor, true);

        if (Delta_Time != 0)
        {
            entid_t eid;

            if (!pAstronomy)
                if (eid = core.GetEntityId("Astronomy"))
                    pAstronomy = static_cast<Entity *>(core.GetEntityPointer(eid));

            if (!pSunGlow)
                if (eid = core.GetEntityId("SUNGLOW"))
                    pSunGlow = static_cast<Entity *>(core.GetEntityPointer(eid));

            if (pAstronomy || pSunGlow)
            {
                if (pAstronomy)
                    pAstronomy->ProcessStage(Stage::realize, Delta_Time);
                if (pSunGlow)
                    static_cast<SUNGLOW *>(pSunGlow)->DrawSunMoon();

                pRS->SetTransform(D3DTS_WORLD, pMatWorld);
                drawSky(sTechSkyBlendAlpha, dwColor, true);
            }
        }
    }
    else
    {
        pRS->SetRenderState(D3DRS_TEXTUREFACTOR, dwSkyColor);

        drawSky(sTechSky, dwSkyColor, false);
    }

    pMatWorld.SetIdentity();
    pMatTranslate.BuildPosition(vPos.x, vPos.y / 6.0f, vPos.z);
    pMatWorld = pMatWorld * pMatTranslate;
    // The colored-mesh backend consumes the transform explicitly.
#ifdef STORM_ENABLE_BGFX
    D3DMATRIX fogView{}, fogProjection{};
    pRS->GetTransform(D3DTS_VIEW, &fogView);
    pRS->GetTransform(D3DTS_PROJECTION, &fogProjection);
    std::string captureResult;
    if (storm::render::captureFogFrame(pMatWorld.matrix, &fogView.m[0][0], &fogProjection.m[0][0], captureResult))
        core.Trace("bgfx fog capture: %s", captureResult.empty() ? "saved" : captureResult.c_str());
#endif
    if (fogMesh_) fogMesh_->submit({std::span<const float,16>(pMatWorld.matrix), sTechSkyFog});
}

uint32_t SKY::AttributeChanged(ATTRIBUTES *pAttribute)
{
    // if (*pAttribute == "dir")            { sSkyDir = pAttribute->GetThisAttr(); return 0; }
    if (*pAttribute == "dir")
    {
        FillSkyDirArray(pAttribute);
        return 0;
    }
    if (*pAttribute == "color")
    {
        dwSkyColor = pAttribute->GetAttributeAsDword();
        return 0;
    }
    if (*pAttribute == "RotateSpeed")
    {
        fSkySpeedRotate = pAttribute->GetAttributeAsFloat();
        return 0;
    }
    if (*pAttribute == "Size")
    {
        fSkySize = pAttribute->GetAttributeAsFloat();
        return 0;
    }
    if (*pAttribute == "Angle")
    {
        fSkyAngle = pAttribute->GetAttributeAsFloat();
        return 0;
    }

    if (*pAttribute == "techSky")
    {
        sTechSky = to_string(pAttribute->GetThisAttr());
        return 0;
    }

    if (*pAttribute == "techSkyBlend")
    {
        sTechSkyBlend = to_string(pAttribute->GetThisAttr());
        return 0;
    }

    if (*pAttribute == "techSkyAlpha")
    {
        sTechSkyBlendAlpha = to_string(pAttribute->GetThisAttr());
        return 0;
    }

    if (*pAttribute == "techSkyFog")
    {
        sTechSkyFog = to_string(pAttribute->GetThisAttr());
        return 0;
    }

    if (*pAttribute == "isDone")
    {
        GenerateSky(true);
        return 0;
    }

    if (*pAttribute == "TimeUpdate")
    {
        UpdateFogSphere(false);
        return 0;
    }

    return 0;
}

uint64_t SKY::ProcessMessage(MESSAGE &message)
{
    if (message.Long() == MSG_SEA_REFLECTION_DRAW)
        Realize(0);
    return 0;
}

void SKY::RestoreRender()
{
    GenerateSky(false);
}

void SKY::FillSkyDirArray(ATTRIBUTES *pAttribute)
{
    aSkyDirArray.clear();
    failedSkySetCount_ = 0;
    const int32_t q = pAttribute->GetAttributesNum();
    if (q < 1)
        return;

    // aSkyDirArray.AddElements( q );
    aSkyDirArray.resize(aSkyDirArray.size() + q);
    if (q > 1)
    {
        for (int32_t n = 0; n < q; n++)
        {
            const auto *const attrName = pAttribute->GetAttributeName(n);
            if (attrName && attrName[0] == 'd' && attrName[1] >= '0' && attrName[1] <= '9')
            {
                const auto i = atol(&attrName[1]);
                if (i < q)
                    aSkyDirArray[i] = to_string(pAttribute->GetAttribute(n));
            }
        }
    }
    else
    {
        aSkyDirArray[0] = to_string(pAttribute->GetAttribute(0U));
    }
    // Attribute setup is batched until isDone; retain the requested phase even
    // if a subsequent load fails, independently of the still-visible old pair.
    requestedSkyTimeFactor_ = TimeToFactor(static_cast<float>(atof(pAttribute->GetThisAttr())));
}

float SKY::TimeToFactor(float hours) const
{
    if (aSkyDirArray.empty() || !std::isfinite(hours))
        return 0.f;
    double time = std::fmod(static_cast<double>(hours), 24.0);
    if (time < 0.0)
        time += 24.0;
    // Do not let float rounding turn the last interval into an out-of-range index.
    const float count = static_cast<float>(aSkyDirArray.size());
    return std::min(static_cast<float>(time / 24.0 * aSkyDirArray.size()), std::nextafter(count, 0.f));
}

bool SKY::SelectSkyTextures(float factor)
{
    if (aSkyDirArray.empty() || !std::isfinite(factor) || factor < 0.f || factor >= aSkyDirArray.size())
        return false;
    const auto current = static_cast<size_t>(factor);
    const size_t setCount = aSkyDirArray.size() > 1 ? 2 : 1;
    const std::string empty;
    const auto &currentDir = aSkyDirArray[current];
    const auto &nextDir = aSkyDirArray.size() > 1 ? aSkyDirArray[(current + 1) % aSkyDirArray.size()] : empty;
    if (failedSkySetCount_ == setCount && currentDir == failedSkyDirs_[0] && nextDir == failedSkyDirs_[1])
        return false;
    failedSkySetCount_ = 0;
    if (loadedSkySetCount_ == setCount && currentDir == loadedSkyDirs_[0] && nextDir == loadedSkyDirs_[1])
    {
        fTimeFactor = factor;
        return true;
    }
    // Acquire a whole pair before releasing anything. Shared filenames still own
    // one renderer reference per slot, including a reused previous next set.
    std::array<std::string, 2> dirs{currentDir, nextDir};
    std::array<int32_t, SKY_NUM_TEXTURES * 2> replacement;
    replacement.fill(-1);
    const auto discard = [&] {
        for (const auto texture : replacement)
            if (texture >= 0) pRS->TextureRelease(texture);
    };
    const auto fail = [&] {
        discard();
        failedSkyDirs_.swap(dirs);
        failedSkySetCount_ = setCount; // No missing-file I/O every frame; retry on a new pair/reload.
        return false;
    };
    constexpr const char *names[SKY_NUM_TEXTURES] = {"sky_fr.tga", "sky_lf.tga", "sky_bk.tga", "sky_rt.tga", "sky_up.tga"};
    try
    {
        for (size_t set = 0; set < setCount; ++set)
        {
            const int32_t *reuse = loadedSkySetCount_ > 0 && dirs[set] == loadedSkyDirs_[0] ? TexturesID :
                                  loadedSkySetCount_ > 1 && dirs[set] == loadedSkyDirs_[1] ? TexturesNextID : nullptr;
            for (size_t face = 0; face < SKY_NUM_TEXTURES; ++face)
            {
                auto &texture = replacement[set * SKY_NUM_TEXTURES + face];
                if (reuse && reuse[face] >= 0)
                {
                    if (pRS->TextureIncReference(reuse[face])) texture = reuse[face];
                }
                else
                    texture = pRS->TextureCreate((dirs[set] + names[face]).c_str());
                if (texture < 0) return fail();
            }
        }
    }
    catch (...)
    {
        discard();
        throw;
    }
    for (size_t face = 0; face < SKY_NUM_TEXTURES; ++face)
    {
        if (TexturesID[face] >= 0) pRS->TextureRelease(TexturesID[face]);
        if (TexturesNextID[face] >= 0) pRS->TextureRelease(TexturesNextID[face]);
        TexturesID[face] = replacement[face];
        TexturesNextID[face] = replacement[SKY_NUM_TEXTURES + face];
    }
    loadedSkyDirs_.swap(dirs);
    loadedSkySetCount_ = setCount;
    fTimeFactor = factor;
#ifdef __ANDROID__
    // Opt-in transition evidence only; no per-frame logging or unlimited output.
    static unsigned logsRemaining = 64;
    char logLevel[PROP_VALUE_MAX] = {};
    if (logsRemaining && __system_property_get("log.tag.StormSky", logLevel) > 0 &&
        std::string_view(logLevel) == "DEBUG")
    {
        --logsRemaining;
        __android_log_print(ANDROID_LOG_DEBUG, "StormSky",
            "pair entity=%llu phase=%.6f sets=%zu current=%s next=%s handles=%d,%d",
            static_cast<unsigned long long>(GetId()), static_cast<double>(fTimeFactor), loadedSkySetCount_,
            loadedSkyDirs_[0].c_str(), loadedSkyDirs_[1].c_str(), TexturesID[0], TexturesNextID[0]);
    }
#endif
    return true;
}

void SKY::UpdateTimeFactor()
{
    entid_t eid;
    if (!(eid = core.GetEntityId("weather")))
        return;
    const float hours = static_cast<WEATHER_BASE *>(core.GetEntityPointer(eid))->GetFloat(whf_time_counter);
    if (std::isfinite(hours))
    {
        requestedSkyTimeFactor_ = TimeToFactor(hours);
        SelectSkyTextures(requestedSkyTimeFactor_);
    }
}

float SKY::CalculateAlphaForSun(const CVECTOR &vSunPos, float fSunSize)
{
    // get Sky
    float fFov;
    CVECTOR vPos, vAng;
    pRS->GetCamera(vPos, vAng, fFov);

    CMatrix mtxWorld;
    mtxWorld.BuildRotateY(fAngleY + fSkyAngle);
    mtxWorld.Move(vPos.x, vPos.y / 6.0f, vPos.z);

    CVECTOR vLocalSunPos;
    mtxWorld.MulToInv(vSunPos, vLocalSunPos);

    if (vLocalSunPos.y < 0.f)
        vLocalSunPos.y = 0.f;

    // calculate where we end up
    if (fSkySize > 0.f)
    {
        int32_t nTexNum = -1;
        float fu, fv, fk;

        // check the hit to the top side
        if (vLocalSunPos.y > 0.f)
        {
            fk = fSkySize / vLocalSunPos.y;
            fu = -vLocalSunPos.x * fk;
            fv = vLocalSunPos.z * fk;

            if (fu <= fSkySize && fu >= -fSkySize && fv <= fSkySize && fv >= -fSkySize)
            {
                nTexNum = 4;
                fu = (fu + fSkySize) * 0.5f / fSkySize;
                fv = (fv + fSkySize) * 0.5f / fSkySize;
            }
        }

        // check for hitting the front side
        if (nTexNum == -1)
        {
            if (vLocalSunPos.z < 0.f)
            {
                fk = -fSkySize / vLocalSunPos.z;
                fu = vLocalSunPos.x * fk;
                fv = vLocalSunPos.y * fk;

                if (fu <= fSkySize && fu >= -fSkySize && fv <= fSkySize && fv >= 0.f)
                {
                    nTexNum = 0;
                    fu = (fu + fSkySize) * 0.5f / fSkySize;
                    fv = (fSkySize - fv) / fSkySize;
                }
            }
        }

        // check the hit to the back side
        if (nTexNum == -1)
        {
            if (vLocalSunPos.z > 0.f)
            {
                fk = fSkySize / vLocalSunPos.z;
                fu = -vLocalSunPos.x * fk;
                fv = vLocalSunPos.y * fk;

                if (fu <= fSkySize && fu >= -fSkySize && fv <= fSkySize && fv >= 0.f)
                {
                    nTexNum = 2;
                    fu = (fu + fSkySize) * 0.5f / fSkySize;
                    fv = (fSkySize - fv) / fSkySize;
                }
            }
        }

        // check the hit to the left side
        if (nTexNum == -1)
        {
            if (vLocalSunPos.x > 0.f)
            {
                fk = fSkySize / vLocalSunPos.x;
                fu = vLocalSunPos.z * fk;
                fv = vLocalSunPos.y * fk;

                if (fu <= fSkySize && fu >= -fSkySize && fv <= fSkySize && fv >= 0.f)
                {
                    nTexNum = 1;
                    fu = (fSkySize + fu) * 0.5f / fSkySize;
                    fv = (fSkySize - fv) / fSkySize;
                }
            }
        }

        // check the hit to the right side
        if (nTexNum == -1)
        {
            if (vLocalSunPos.x < 0.f)
            {
                fk = -fSkySize / vLocalSunPos.x;
                fu = -vLocalSunPos.z * fk;
                fv = vLocalSunPos.y * fk;

                if (fu <= fSkySize && fu >= -fSkySize && fv <= fSkySize && fv >= 0.f)
                {
                    nTexNum = 3;
                    fu = (fSkySize + fu) * 0.5f / fSkySize;
                    fv = (fSkySize - fv) / fSkySize;
                }
            }
        }

        // looking for alpha in texture
        if (nTexNum != -1)
        {
            const auto dwCol1 =
                GetPixelColor(TexturesID[nTexNum], fu, fv);
            const auto dwCol2 =
                GetPixelColor(TexturesNextID[nTexNum], fu, fv);

            const auto fK = fTimeFactor - static_cast<int32_t>(fTimeFactor);
            const auto fAlpha = (1.f - fK) * (dwCol1 >> 24) / 255.f + fK * (dwCol2 >> 24) / 255.f;
            return fAlpha;
        }
    }

    return 1.f;
}

uint32_t SKY::GetPixelColor(int32_t texture, float fu, float fv) const
{
    uint32_t color=0xffffffffu;
    pRS->sampleTextureArgb(texture,fu,fv,color);
    return color;
}
