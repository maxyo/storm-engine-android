#include "renderer_impl.h"
#include <platform/android_ui.hpp>
#include <platform/android_interface_presentation.hpp>
#include <platform/android_hud_presenter.hpp>
#ifdef __ANDROID__
#include "android_render_resolution.hpp"
#include "auto_preset.hpp"
#include "../../xinterface/src/string_service.h"
#endif

#include "app_events.hpp"
#include "core.h"
#include "scene_sweep.hpp"

#include "entity.h"
#include "math_inlines.h"
#include "s_import_func.h"
#include "texture.h"
#include "v_s_stack.h"
#include "fs.h"
#include "debug-trap.h"
#include "string_compare.hpp"
#include "logging.hpp"
#include "storm/render/d3d_interop.hpp"
#include "storm/render/frame_owner.hpp"
#include "scene_hold.hpp"
#include "storm/render/effects.hpp"
#include "storm/render/frame_rate.hpp"
#include "storm/render/log.hpp"
#include "storm/render/perf_stats.hpp"
#include "storm/render/program_cache.hpp"
#include "storm/render/texture_cache.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <SDL_rwops.h>
#include <SDL_timer.h>


#include <unistd.h>

CREATE_SERVICE(Renderer)

CREATE_SCRIPTLIBRIARY(DX9RENDER_SCRIPT_LIBRIARY)

#define S_RELEASE(a, b)                                                                                                \
    if (a)                                                                                                             \
    {                                                                                                                  \
        ULONG refc = a->Release();                                                                                     \
        a = NULL;                                                                                                      \
    }

namespace
{


void InvokeEntitiesLostRender()
{
    core.ForEachEntity([](entptr_t entity_ptr) { entity_ptr->ProcessStage(Entity::Stage::lost_render); });
}
void InvokeEntitiesRestoreRender()
{
    core.ForEachEntity([](entptr_t entity_ptr) { entity_ptr->ProcessStage(Entity::Stage::restore_render); });
}

} // namespace

Renderer *Renderer::pRS = nullptr;

class LostDeviceSentinel : public SERVICE
{
    void RunStart() override
    {
        if (!Renderer::pRS)
            return;
        if (auto *d3d9 = Renderer::pRS->GetD3DDevice())
        {
            switch (d3d9->TestCooperativeLevel())
            {
            case D3DERR_DEVICENOTRESET:
                if (!Renderer::pRS->ResetDevice())
                {
                    core.stopFrameProcessing();
                }
                break;

            case D3DERR_DEVICELOST:
                core.stopFrameProcessing();
                break;
            }
        }

    }
};
CREATE_SERVICE(LostDeviceSentinel)

uint32_t ScriptSetTexturePath(VS_STACK *pS)
{
    auto *pString = (VDATA *)pS->Pop();
    auto *pNumber = (VDATA *)pS->Pop();

    const uintptr_t iNumber = pNumber->GetInt();
    auto *const pStr = pString->GetString();

    if (!Renderer::pRS)
    {
        core.GetService("renderer");
        Assert(Renderer::pRS);
    }

    auto *pVR = (VDATA *)pS->Push();
    if (!pVR || iNumber < 0 || iNumber >= 4)
    {
        pVR->Set(0);
        return IFUNCRESULT_OK;
    }

    Renderer::pRS->TextureCreate((const char *)-1);
    Renderer::pRS->TextureCreate((const char *)iNumber);
    Renderer::pRS->TextureCreate(static_cast<const char *>(pStr));

    pVR->Set(1);

    return IFUNCRESULT_OK;
}

uint32_t RPrint(VS_STACK *pS)
{
    auto *pString = (VDATA *)pS->Pop();
    const int32_t y = ((VDATA *)pS->Pop())->GetInt();
    const int32_t x = ((VDATA *)pS->Pop())->GetInt();

    if (pString->GetString())
        Renderer::pRS->Print(x, y, pString->GetString());
    auto *pVR = (VDATA *)pS->Push();
    pVR->Set(0);
    return IFUNCRESULT_OK;
}

// The game's options screen and seadogs.c still call SetGlowParams for the D3D9 glow, which never ran (its render
// targets were never created) and is gone: the arguments are taken and ignored. Glow is fx bloom now.
uint32_t SetGlowParams(VS_STACK *pS)
{
    for (int i = 0; i < 3; ++i)
        pS->Pop();

    auto *pVR = (VDATA *)pS->Push();
    pVR->Set(0);
    return IFUNCRESULT_OK;
}

uint32_t slGetTexture(VS_STACK *pS)
{
    auto filename = ((VDATA *)pS->Pop())->GetString();
    int32_t texNum = Renderer::pRS->TextureCreate(filename);

    auto *pVR = (VDATA *)pS->Push();
    pVR->Set(texNum);
    return IFUNCRESULT_OK;
}

uint32_t slReleaseTexture(VS_STACK *pS)
{
    int32_t texNum = ((VDATA *)pS->Pop())->GetInt();

    Renderer::pRS->TextureRelease(texNum);
    return IFUNCRESULT_OK;
}

bool DX9RENDER_SCRIPT_LIBRIARY::Init()
{
    IFUNCINFO sIFuncInfo;

    sIFuncInfo.nArguments = 2;
    sIFuncInfo.pFuncName = "SetTexturePath";
    sIFuncInfo.pReturnValueName = "int";
    sIFuncInfo.pFuncAddress = ScriptSetTexturePath;
    core.SetScriptFunction(&sIFuncInfo);

    sIFuncInfo.nArguments = 3;
    sIFuncInfo.pFuncName = "RPrint";
    sIFuncInfo.pReturnValueName = "int";
    sIFuncInfo.pFuncAddress = RPrint;
    core.SetScriptFunction(&sIFuncInfo);

    sIFuncInfo.nArguments = 3;
    sIFuncInfo.pFuncName = "SetGlowParams";
    sIFuncInfo.pReturnValueName = "int";
    sIFuncInfo.pFuncAddress = SetGlowParams;
    core.SetScriptFunction(&sIFuncInfo);

    sIFuncInfo.nArguments = 1;
    sIFuncInfo.pFuncName = "GetTexture";
    sIFuncInfo.pReturnValueName = "int";
    sIFuncInfo.pFuncAddress = slGetTexture;
    core.SetScriptFunction(&sIFuncInfo);

    sIFuncInfo.nArguments = 1;
    sIFuncInfo.pFuncName = "ReleaseTexture";
    sIFuncInfo.pReturnValueName = "void";
    sIFuncInfo.pFuncAddress = slReleaseTexture;
    core.SetScriptFunction(&sIFuncInfo);

    return true;
}

uint64_t _rdtsc;
uint32_t dwTotalSize = 0;
uint32_t dwSplashTime = 0;
bool bSplash = false;
#pragma warning(push)
#pragma warning(disable : 4305 4309)
char sSplashText[] = {'\xbb', '\x9a', '\x89', '\x9a', '\x93', '\x90', '\x8f', '\x9a', '\x8d', '\x8c', '\xdf',
                      '\xbc', '\x90', '\x8f', '\x86', '\xd1', '\xdf', '\xb1', '\x90', '\x8b', '\xdf', '\x99',
                      '\x90', '\x8d', '\xdf', '\xac', '\x9e', '\x93', '\x9a', '\xd1', '\0'};
#pragma warning(pop)
char splashbuffer[256];

#define VIDEODIR "Resource\\Videos\\%s"

struct SphereVertex
{
    glm::vec3 v;
    uint32_t c;
};

uint32_t sphereNumTrgs;
SphereVertex *sphereVertex = nullptr;

void CreateSphere()
{
#define CalcKColor(ind)                                                                                                \
    {                                                                                                                  \
        kColor = glm::dot(light, storm::math::normalizeOrZero(glm::vec3(sphereVertex[t * 3 + ind].v.x,              \
                    sphereVertex[t * 3 + ind].v.y, sphereVertex[t * 3 + ind].v.z)));                            \
        if (kColor < 0.0f)                                                                                             \
            kColor = 0.0f;                                                                                             \
    }
#define CLerp(c, min) (uint32_t(c * (kColor * (1.0f - min) + min)))
#define Color1                                                                                                         \
    ((CLerp(255.0f, 0.5f) << 24) | (CLerp(255.0f, 0.7f) << 16) | (CLerp(255.0f, 0.7f) << 8) |                          \
     (CLerp(255.0f, 0.7f) << 0));

    if (sphereVertex)
        return;

    const float myPI = 3.1415926535897932f;
    const int32_t a1 = 32;
    const int32_t a2 = (a1 / 2);

    sphereNumTrgs = a1 * a2 * 2;
    sphereVertex = new SphereVertex[sphereNumTrgs * 6];

    const glm::vec3 light = storm::math::normalizeOrZero(glm::vec3(0.0f, 0.0f, 1.0f));
    float kColor;
    // fill in the vertices
    for (int32_t i = 0, t = 0; i < a2; i++)
    {
        const float r1 = sinf(myPI * i / static_cast<float>(a2));
        const float y1 = cosf(myPI * i / static_cast<float>(a2));
        const float r2 = sinf(myPI * (i + 1) / static_cast<float>(a2));
        const float y2 = cosf(myPI * (i + 1) / static_cast<float>(a2));
        for (int32_t j = 0; j < a1; j++)
        {
            const float x1 = sinf(2.0f * myPI * j / static_cast<float>(a1));
            const float z1 = cosf(2.0f * myPI * j / static_cast<float>(a1));
            const float x2 = sinf(2.0f * myPI * (j + 1) / static_cast<float>(a1));
            const float z2 = cosf(2.0f * myPI * (j + 1) / static_cast<float>(a1));
            // 0
            sphereVertex[t * 3 + 0].v.x = r1 * x1;
            sphereVertex[t * 3 + 0].v.y = y1;
            sphereVertex[t * 3 + 0].v.z = r1 * z1;
            CalcKColor(0);
            sphereVertex[t * 3 + 0].c = Color1;
            // 1
            sphereVertex[t * 3 + 1].v.x = r2 * x1;
            sphereVertex[t * 3 + 1].v.y = y2;
            sphereVertex[t * 3 + 1].v.z = r2 * z1;
            CalcKColor(1);
            sphereVertex[t * 3 + 1].c = Color1;
            // 2
            sphereVertex[t * 3 + 2].v.x = r1 * x2;
            sphereVertex[t * 3 + 2].v.y = y1;
            sphereVertex[t * 3 + 2].v.z = r1 * z2;
            CalcKColor(2);
            sphereVertex[t * 3 + 2].c = Color1;
            // 3 = 2
            sphereVertex[t * 3 + 3] = sphereVertex[t * 3 + 2];
            // 4 = 1
            sphereVertex[t * 3 + 4] = sphereVertex[t * 3 + 1];
            // 5
            sphereVertex[t * 3 + 5].v.x = r2 * x2;
            sphereVertex[t * 3 + 5].v.y = y2;
            sphereVertex[t * 3 + 5].v.z = r2 * z2;
            CalcKColor(5);
            sphereVertex[t * 3 + 5].c = Color1;
            // added 2 triangles
            t += 2;
        }
    }
}

struct SD_TEXTURE_FORMAT
{
    TX_FORMAT txFormat;
    D3DFORMAT d3dFormat;
    bool isSwizzled;
    const char *format;
};

SD_TEXTURE_FORMAT textureFormats[] = {
    {TXF_DXT1, D3DFMT_DXT1, true, "D3DFMT_DXT1"},
    {TXF_DXT3, D3DFMT_DXT3, true, "D3DFMT_DXT3"},
    {TXF_DXT5, D3DFMT_DXT5, true, "D3DFMT_DXT5"},
    {TXF_A8R8G8B8, D3DFMT_A8R8G8B8, false, "D3DFMT_A8R8G8B8"},
    {TXF_X8R8G8B8, D3DFMT_X8R8G8B8, false, "D3DFMT_X8R8G8B8"},
    {TXF_R5G6B5, D3DFMT_R5G6B5, false, "D3DFMT_R5G6B5"},
    {TXF_A4R4G4B4, D3DFMT_A4R4G4B4, false, "D3DFMT_A4R4G4B4"},
    {TXF_A1R5G5B5, D3DFMT_A1R5G5B5, false, "D3DFMT_A1R5G5B5"},
};

uint32_t dwSoundBuffersCount = 0;
uint32_t dwSoundBytes = 0;
uint32_t dwSoundBytesCached = 0;

//################################################################################
//~!~
/*#define STRIPPATH(s)\
    (sizeof(s) > 11 && (s)[sizeof(s)-12] == '/' ? (s) + sizeof(s) - 11 : \
    sizeof(s) > 10 && (s)[sizeof(s)-11] == '/' ? (s) + sizeof(s) - 10 : \
    sizeof(s) > 9 && (s)[sizeof(s)-10] == '/' ? (s) + sizeof(s) - 9 : \
    sizeof(s) > 8 && (s)[sizeof(s)-9] == '/' ? (s) + sizeof(s) - 8 : \
    sizeof(s) > 7 && (s)[sizeof(s)-8] == '/' ? (s) + sizeof(s) - 7 : \
    sizeof(s) > 6 && (s)[sizeof(s)-7] == '/' ? (s) + sizeof(s) - 6 : \
    sizeof(s) > 5 && (s)[sizeof(s)-6] == '/' ? (s) + sizeof(s) - 5 : \
    sizeof(s) > 4 && (s)[sizeof(s)-5] == '/' ? (s) + sizeof(s) - 4 : \
    sizeof(s) > 3 && (s)[sizeof(s)-4] == '/' ? (s) + sizeof(s) - 3 : \
    sizeof(s) > 2 && (s)[sizeof(s)-3] == '/' ? (s) + sizeof(s) - 2 : (s))*/

#define CHECKD3DERR(expr) ErrorHandler(expr, __FILE__, __LINE__, __func__, #expr)

inline bool ErrorHandler(HRESULT hr, const char *file, unsigned line, const char *func, const char *expr)
{
    if (hr != D3D_OK)
    {
        core.Trace("[%s:%s:%d] (%s)", file, func, line, expr);
        return true;
    }

    return false;
}

const uint32_t Renderer::rectsVBuffer_SizeInRects = 512;

//################################################################################
Renderer::Renderer()
{
    rectsVBuffer = nullptr;

    pOriginalScreenSurface = nullptr;
    pOriginalDepthSurface = nullptr;

    pRS = this;
    d3d = nullptr;
    d3d9 = nullptr;
    aniVBuffer = nullptr;
    numAniVerteces = 0;
    pVTL = nullptr;
    nFontQuantity = 0;
    idFontCurrent = 0;
    fontIniFileName = nullptr;

    bLoadTextureEnabled = true;

    dwBackColor = 0x0;

    bTrace = true;
    iSetupPath = 0;

    bDropVideoConveyor = false;
    pDropConveyorVBuffer = nullptr;

    aspectRatio = -1.0f;

    bShowFps = false;
    bShowExInfo = false;
    bInsideScene = false;

    m_fHeightDeformator = 1.f;

    progressImage = nullptr;
    progressImageSize = 0;
    progressTexture = -1;
    progressBackImage = nullptr;
    progressBackImageSize = 0;
    progressTipsImage = nullptr;
    progressTipsImageSize = 0;
    progressTipsTexture = -1;
    loadFrame = 0;
    backTexture = -1;
    back0Texture = -1;
    progressSafeCounter = 0;
    isInPViewProcess = false;
    progressUpdateTime = 0;
    progressFramesPosX = 0.85f;
    progressFramesPosY = 0.8f;
    progressFramesWidth = 64;
    progressFramesHeight = 64;
    progressFramesCountX = 8;
    progressFramesCountY = 8;

    vViewRelationPos = glm::vec3(0.f, 0.f, 0.f);
    vWordRelationPos = -vViewRelationPos;
    storm::render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
    bUseLargeBackBuffer = false;
}

namespace
{
// Graphics effects (renderer_bgfx/fx): «fx_preset» 0 Низкая / 1 Средняя / 2 Высокая from engine.ini and, on
// Android, the player's choice in the app, which wins; the individual fx_* options (-1 = the preset's value)
// are developer keys of engine.ini. STORM_FX_OPTIONS="fx_preset=2,fx_bloom=0" overrides both (measurements);
// it is logged.
int effectsPreset = 0;
std::vector<storm::render::EffectsOption> effectsOptions;
// The scene's internal height of the rung below «Низкая» that the automatic preset measured (auto_preset.hpp; the
// app reports it as fx_auto_render_height only while that automatic choice stands); 0: the preset's own.
uint16_t effectsAutoHeight = 0;

// Applies "name=value,..." over `preset` / `options`.
void ApplyEffectsOverrides(std::string_view list, int &preset, std::vector<storm::render::EffectsOption> &options)
{
    while (!list.empty())
    {
        const auto comma = list.find(',');
        const std::string_view item = list.substr(0, comma);
        list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
        const auto equals = item.find('=');
        if (equals == std::string_view::npos)
        {
            spdlog::error("graphics effects: option '{}' is not name=value", item);
            continue;
        }
        const std::string name(item.substr(0, equals));
        const int value = std::atoi(std::string(item.substr(equals + 1)).c_str());
        if (name == "fx_preset")
            preset = value;
        else
        {
            std::erase_if(options, [&](const auto &option) { return option.name == name; });
            options.push_back({name, value});
        }
    }
}

// `auto_height` (the measured rung below «Низкая») applies to «Низкая» only; a developer fx_render_height wins.
void ApplyEffects(int preset, uint16_t auto_height, const std::vector<storm::render::EffectsOption> &options)
{
    std::vector<storm::render::EffectsOption> applied = options;
    if (auto_height > 0 && preset == 0 &&
        std::none_of(options.begin(), options.end(), [](const auto &option) { return option.name == "fx_render_height"; }))
        applied.push_back({"fx_render_height", auto_height});
    const auto report = storm::render::ConfigureEffects(preset, applied);
    for (const auto &error : report.errors)
        spdlog::error("graphics effects: {}", error);
}

// engine.ini's values of the effect options: the fallbacks when the app's preferences are read
// again (live re-application, ReconfigureGraphicsEffects).
std::map<std::string, int> effectsIniValues;

void ConfigureGraphicsEffects(const std::function<int(const char *, int)> &iniValue)
{
    // The preset: engine.ini, and on Android the player's choice in the app, which wins.
    effectsPreset = iniValue("fx_preset", 0);
#ifdef __ANDROID__
    effectsPreset = storm::android::graphicsSetting("fx_preset", effectsPreset);
    effectsAutoHeight = static_cast<uint16_t>(std::clamp(storm::android::graphicsSetting("fx_auto_render_height", 0), 0, 4320));
    if (effectsAutoHeight > 0 && effectsPreset != 0)
    {
        spdlog::error("graphics effects: the app reports the measured {}-line scene with preset {}; it belongs to «Низкая» "
                      "only and is not applied", effectsAutoHeight, effectsPreset);
        effectsAutoHeight = 0;
    }
    else if (effectsAutoHeight > 0)
        spdlog::info("graphics effects: the automatic preset chose «Низкая» at a {}-line scene (the rung below it)",
                     effectsAutoHeight);
#endif
    // The single effects (and fx_anisotropy, which the preset decides too) are developer keys of
    // engine.ini; a debuggable Android build also offers some of them on the settings screen
    // (GraphicsSettings.DEBUG_KEYS of the app), which win over engine.ini.
    effectsOptions.clear();
    std::string overridden;
    for (const auto &info : storm::render::EffectsOptionNames())
        if (std::string_view(info.name) != "fx_preset")
        {
            int value = iniValue(info.name, -1);
#ifdef __ANDROID__
            value = storm::android::graphicsSetting(info.name, value);
#endif
            if (value >= 0)
            {
                effectsOptions.push_back({info.name, value});
                overridden += fmt::format(" {}={}", info.name, value);
            }
        }
    spdlog::info("graphics effects: preset {}, options:{}", effectsPreset, overridden.empty() ? " none" : overridden);
    if (const char *overrides = std::getenv("STORM_FX_OPTIONS"); overrides && *overrides)
    {
        spdlog::warn("graphics effects: STORM_FX_OPTIONS overrides the settings: {}", overrides);
        ApplyEffectsOverrides(overrides, effectsPreset, effectsOptions);
    }
    ApplyEffects(effectsPreset, effectsAutoHeight, effectsOptions);
#ifdef STORM_SCENE_SWEEP
    // A sweep variant is applied over the configured options.
    storm::scene_sweep::setVariantHook([](const char *variant) {
        int preset = effectsPreset;
        auto options = effectsOptions;
        ApplyEffectsOverrides(variant, preset, options);
        spdlog::info("graphics effects: sweep variant '{}'", variant);
        ApplyEffects(preset, effectsAutoHeight, options);
    }, [] { return !storm::render::CurrentEffects().pending; });
    storm::scene_sweep::setIncompleteFrameHook([] { return storm::render::LastIncompleteFrame(); });
#endif
}
#ifdef __ANDROID__
// The player's «Частота кадров» (the app offers the rates the display can show, 60 by default): the frame pacing
// follows it at once (Swappy's interval, the ADPF target; logged by the shim).
void ConfigureFrameRate()
{
    storm::render::SetFrameRate(static_cast<uint32_t>(std::max(storm::android::graphicsSetting("frame_rate", 60), 0)));
}

// The settings screen stored a new quality preset: the effects follow it without a restart
// (MSAA and anisotropy rebuild the scene stores at the next Present). Options engine.ini
// gave keep its values as fallbacks.
// The automatic graphics preset (auto_preset.hpp): armed at the start unless the player chose «Графика» or the
// stored automatic choice was measured by this version; it measures on the title screen, applies its choice and
// reports it to the app, which stores it (GraphicsSettings.java).
std::optional<storm::render::AutoPresetBenchmark> autoPreset;
storm::render::PerfCounters autoPresetPerf;

void ArmAutoPreset()
{
    // The host answers -1 for a preset the player chose, else the version of the stored measurement (0: none).
    constexpr int kNotReported = -2;
    const int stored = storm::android::graphicsSetting("fx_preset_auto", kNotReported);
    if (stored == kNotReported)
    {
        spdlog::error("graphics auto preset: the app reports no fx_preset_auto; preset {} stays", effectsPreset);
        return;
    }
    if (stored < 0)
    {
        spdlog::info("graphics auto preset: off, the player chose preset {}", effectsPreset);
        return;
    }
    if (const char *overrides = std::getenv("STORM_FX_OPTIONS"); overrides && *overrides)
    {
        spdlog::warn("graphics auto preset: off while STORM_FX_OPTIONS overrides the settings");
        return;
    }
    if (stored >= storm::render::kAutoPresetVersion)
    {
        spdlog::info("graphics auto preset: preset {} was measured (version {}, current {})", effectsPreset, stored,
                     storm::render::kAutoPresetVersion);
        return;
    }
    spdlog::info("graphics auto preset: will measure on the title screen (stored measurement version {}, current {}; "
                 "target {:.0f} fps with {:.2f}x headroom: GPU ms x {:.2f} <= {:.2f} and p95 present interval <= {:.2f} ms, "
                 "without GPU timers p95 <= {:.2f} ms; paced at {} fps while measuring; relief generation and ray bakes "
                 "held on the title until it ends; preset {} until then{})",
                 stored, storm::render::kAutoPresetVersion, storm::render::kAutoPresetTargetFps,
                 storm::render::kAutoPresetHeadroom, storm::render::kAutoPresetHeadroom, storm::render::AutoPresetBudgetMs(),
                 storm::render::AutoPresetLimitMs(), storm::render::AutoPresetHeadroomLimitMs(),
                 storm::render::kAutoPresetPacingFps, effectsPreset,
                 effectsOptions.empty() ? "" : "; developer fx_* overrides stay on during the measurement");
    autoPreset.emplace();
}

// The measurement ended (chosen or aborted): the present intervals are no longer kept, the player's frame rate and
// the background work come back.
void StopAutoPresetMeasurement()
{
    storm::render::SamplePresentIntervals(false);
    storm::render::HoldBackgroundWork(nullptr);
    ConfigureFrameRate();
}

void ApplyAutoPresetRung(int rung)
{
    const storm::render::AutoPresetRung &r = storm::render::kAutoPresetRungs[static_cast<size_t>(rung)];
    ApplyEffects(r.preset, r.render_height, effectsOptions);
}

std::string AutoPresetRungName(int rung)
{
    const storm::render::AutoPresetRung &r = storm::render::kAutoPresetRungs[static_cast<size_t>(rung)];
    return r.render_height ? fmt::format("preset {} at a {}-line scene", r.preset, r.render_height)
                           : fmt::format("preset {}", r.preset);
}

void EndAutoPreset(const std::string &reason)
{
    if (!autoPreset)
        return;
    const bool running = autoPreset->running();
    if (!autoPreset->abort(reason).aborted)
        return;
    if (!running)
    {
        storm::render::HoldBackgroundWork(nullptr); // it may have been waiting on the title
        spdlog::info("graphics auto preset: not measured this time: {}", reason);
        return;
    }
    StopAutoPresetMeasurement();
    spdlog::warn("graphics auto preset: measurement aborted: {}; nothing stored, preset {} again, the next start "
                 "measures again; so far {}", reason, effectsPreset, autoPreset->report());
}

void AutoPresetFrame()
{
    if (!autoPreset || autoPreset->phase() == storm::render::AutoPresetBenchmark::Phase::Done ||
        autoPreset->phase() == storm::render::AutoPresetBenchmark::Phase::Aborted)
        return;
    const bool running = autoPreset->running();
    storm::render::AutoPresetBenchmark::Frame frame;
    frame.now_s = static_cast<double>(SDL_GetTicks64()) / 1000.0;
    frame.title = storm::android::mobileScreen == "mainmenu";
    frame.effects_ready = frame.title && !storm::render::CurrentEffects().pending;
    // Held while the measurement waits on the title or runs; a relief map already in progress finishes first.
    storm::render::HoldBackgroundWork(autoPreset->holds_background(frame.title)
                                          ? "the automatic graphics preset measures the GPU on the title screen"
                                          : nullptr);
    frame.background_busy = !running && frame.title && storm::render::BackgroundWorkBusy();
    std::vector<float> intervals;
    if (running)
    {
        intervals = storm::render::TakePresentIntervals();
        const storm::render::PerfCounters &totals = storm::render::PerfTotals();
        const storm::render::PerfCounters since = totals.Since(autoPresetPerf);
        autoPresetPerf = totals;
        frame.gpu_ms = since.gpu_frames ? since.gpu_ms : -1.0;
        frame.gpu_frames = since.gpu_frames;
    }
    frame.intervals_ms = intervals;
    const size_t measured = autoPreset->results().size();
    const auto step = autoPreset->frame(frame);
    if (autoPreset->results().size() > measured)
    {
        const storm::render::AutoPresetResult &r = autoPreset->results().back();
        spdlog::info("graphics auto preset: {}: {} frames in {:.2f} s ({:.1f} fps), present ms p50 {:.2f} p95 {:.2f} "
                     "max {:.2f}, gpu {}: {} ({})",
                     AutoPresetRungName(r.rung), r.frames, r.seconds, r.seconds > 0.0 ? r.frames / r.seconds : 0.0,
                     r.p50_ms, r.p95_ms, r.max_ms,
                     r.gpu_ms >= 0.0 ? fmt::format("{:.2f} ms", r.gpu_ms) : std::string("n/a (no timer queries)"),
                     r.passes ? "holds the target" : "misses the target", r.why);
    }
    if (step.aborted)
    {
        StopAutoPresetMeasurement();
        spdlog::warn("graphics auto preset: measurement aborted: {}; nothing stored, preset {} again, the next start "
                     "measures again; so far {}", autoPreset->reason(), effectsPreset, autoPreset->report());
        ApplyEffects(effectsPreset, effectsAutoHeight, effectsOptions);
        return;
    }
    if (step.apply_rung >= 0 && !step.finished)
    {
        if (!running)
        {
            storm::render::SetFrameRate(storm::render::kAutoPresetPacingFps);
            storm::render::SamplePresentIntervals(true);
            autoPresetPerf = storm::render::PerfTotals();
        }
        spdlog::info("graphics auto preset: measuring {}", AutoPresetRungName(step.apply_rung));
        ApplyAutoPresetRung(step.apply_rung);
    }
    if (step.finished)
    {
        StopAutoPresetMeasurement();
        const auto &results = autoPreset->results();
        const bool held = std::any_of(results.begin(), results.end(), [&](const auto &r) { return r.passes && r.rung == step.apply_rung; });
        const storm::render::AutoPresetRung &chosen = storm::render::kAutoPresetRungs[static_cast<size_t>(step.apply_rung)];
        spdlog::log(held ? spdlog::level::info : spdlog::level::warn, "graphics auto preset: chose {}{}: {}",
                    AutoPresetRungName(step.apply_rung), held ? "" : " (it misses the target too: nothing is lower)",
                    autoPreset->report());
        effectsPreset = chosen.preset;
        effectsAutoHeight = chosen.render_height;
        ApplyEffects(effectsPreset, effectsAutoHeight, effectsOptions);
        auto *strings = static_cast<VSTRSERVICE *>(core.GetService("STRSERVICE"));
        const char *language = strings ? strings->GetLanguage() : nullptr;
        if (!language)
            spdlog::error("graphics auto preset: the string service has no game language; the app's notice follows the system's");
        storm::android::reportAutoPreset(effectsPreset, effectsAutoHeight, storm::render::kAutoPresetVersion,
                                         autoPreset->report(), language ? language : "");
    }
}

void ReconfigureGraphicsEffects()
{
    if (!storm::android::graphicsSettingsChanged.exchange(false))
        return;
    if (autoPreset && autoPreset->running())
        EndAutoPreset("the player applied settings");
    else if (autoPreset && autoPreset->phase() == storm::render::AutoPresetBenchmark::Phase::WaitingForTitle &&
             storm::android::graphicsSetting("fx_preset_auto", -1) < 0)
        EndAutoPreset("the player chose a preset");
    spdlog::info("graphics effects: preferences changed, applying them");
    ConfigureGraphicsEffects([](const char *name, int fallback) {
        const auto found = effectsIniValues.find(name);
        return found == effectsIniValues.end() ? fallback : found->second;
    });
    ConfigureFrameRate();
}
#endif
} // namespace


static bool texLog = false;

bool Renderer::Init()
{
    // The bgfx renderer's lines (effects, textures, relief) into the engine's logs: system.log (and logcat), their
    // errors into error.log too, from whichever thread logs them (the sinks lock).
    storm::render::SetLogSink([](bool error, const char *line) {
        static const storm::logging::logger_ptr logger = storm::logging::getOrCreateLogger("system");
        logger->log(error ? spdlog::level::err : spdlog::level::info, "[storm-bgfx] {}", line);
    });
    if (auto *sentinelService = core.GetService("LostDeviceSentinel"); !sentinelService)
    {
        throw std::runtime_error("Cannot create LostDeviceSentinel! Abort");
    }

    char str[256];
    for (int32_t i = 0; i < MAX_STEXTURES; i++)
        Textures[i].ref = NULL;

    d3d = nullptr;
    d3d9 = nullptr;

    auto ini = fio->OpenIniFile(core.EngineIniFileName());
    if (!ini)
        throw std::runtime_error("Cannot initialize renderer: engine.ini is unavailable");
    if (ini)
    {
#ifdef __ANDROID__
        // The game's engine.ini may switch the engine's FPS and debug text on; on a phone those are
        // drawn over the touch HUD like a game icon. They show only with an explicit debug
        // setting (adb shell setprop debug.storm.show_fps 1), never from the game's data.
        char fpsSetting[PROP_VALUE_MAX] = {};
        __system_property_get("debug.storm.show_fps", fpsSetting);
        bShowFps = std::string_view(fpsSetting) == "1";
        bShowExInfo = false;
        if (ini->GetInt(nullptr, "show_fps", 0) == 1 || ini->GetInt(nullptr, "show_exinfo", 0) == 1)
            __android_log_print(ANDROID_LOG_INFO, "StormRender", "engine.ini show_fps/show_exinfo ignored (debug.storm.show_fps=%s)", fpsSetting);
#else
        bShowFps = ini->GetInt(nullptr, "show_fps", 0) == 1;
        bShowExInfo = ini->GetInt(nullptr, "show_exinfo", 0) == 1;
#endif
        renderMetricsEnabled = ini->GetInt(nullptr, "render_metrics", 0) == 1;
#ifdef STORM_SCENE_SWEEP
        storm::scene_sweep::setRendererWindow([start = storm::render::PerfTotals()]() mutable {
            const storm::render::PerfCounters &totals = storm::render::PerfTotals();
            std::string summary = totals.Since(start).Summary();
            start = totals;
            return summary;
        });
#endif
        bDropVideoConveyor = ini->GetInt(nullptr, "DropVideoConveyor", 0) != 0;
        texLog = ini->GetInt(nullptr, "texture_log", 0) == 1;
        bUseLargeBackBuffer = ini->GetInt(nullptr, "UseLargeBackBuffer", 0) != 0;

        bWindow = ini->GetInt(nullptr, "full_screen", 1) == 0;

        nTextureDegradation = ini->GetInt(nullptr, "texture_degradation", 0);
#ifdef __ANDROID__
        nTextureDegradation = std::clamp(storm::android::graphicsSetting("texture_degradation", nTextureDegradation), 0, 2);
#endif
        ConfigureGraphicsEffects([&ini](const char *name, int fallback) {
            return effectsIniValues[name] = ini->GetInt(nullptr, name, fallback);
        });
        // Developer key render_thread = 0: bgfx renders on the game thread (measurements against the render
        // thread, which is the default). The Android graphics setting of the same name wins. Logged by the shim.
        int renderThread = ini->GetInt(nullptr, "render_thread", 1);
#ifdef __ANDROID__
        renderThread = storm::android::graphicsSetting("render_thread", renderThread);
#endif
        storm::render::SetRenderThread(renderThread != 0);
        // Key render_backend (frame_owner.hpp SetRenderBackend): auto (the default: Vulkan on the devices it is
        // chosen for), gles or vulkan. The Android graphics setting of the same name wins: 0 auto, 1 gles, 2 vulkan.
        // A Vulkan start leaves its marker under the cache root until frames were presented. Logged by the shim.
        ini->ReadString(nullptr, "render_backend", str, sizeof(str), "auto");
        int renderBackend = std::string_view(str) == "gles" ? 1 : std::string_view(str) == "vulkan" ? 2 : 0;
        if (renderBackend == 0 && std::string_view(str) != "auto")
            spdlog::error("engine.ini render_backend = '{}' is not auto, gles or vulkan; using auto", str);
#ifdef __ANDROID__
        renderBackend = std::clamp(storm::android::graphicsSetting("render_backend", renderBackend), 0, 2);
#endif
        storm::render::SetRenderBackend(static_cast<storm::render::Backend>(renderBackend), fs::GetCachePath() / "RenderBackend");
        // Developer key vulkan_async_pipelines = 0 (frame_owner.hpp SetVulkanAsyncPipelines): measurements.
        storm::render::SetVulkanAsyncPipelines(ini->GetInt(nullptr, "vulkan_async_pipelines", 1) != 0);
        // Developer key render_overlap (frame_owner.hpp SetFrameOverlap): -1 chosen per frame, 0 never, 1 always.
        int renderOverlap = ini->GetInt(nullptr, "render_overlap", -1);
#ifdef __ANDROID__
        renderOverlap = storm::android::graphicsSetting("render_overlap", renderOverlap);
#endif
        storm::render::SetFrameOverlap(renderOverlap);
#ifdef __ANDROID__
        ConfigureFrameRate();
        ArmAutoPreset();
#endif

        storm::render::TextureCacheConfig textureCache;
        ini->ReadString(nullptr, "texture_compression", str, sizeof(str), "auto");
        if (const auto mode = storm::render::ParseTextureCompression(str))
            textureCache.mode = *mode;
        else
            spdlog::error("engine.ini texture_compression = '{}' is not auto, bc, etc2 or rgba; using auto", str);
#ifdef __ANDROID__
        textureCache.mode = static_cast<storm::render::TextureCompression>(
            std::clamp(storm::android::graphicsSetting("texture_compression", static_cast<int>(textureCache.mode)), 0, 3));
#endif
        textureCache.max_bytes = static_cast<uint64_t>(std::max(ini->GetInt(nullptr, "texture_cache_mb", 3072), 64)) << 20;
        textureCache.cache_directory = fs::GetCachePath() / "TextureCache";
        storm::render::ConfigureTextureCache(textureCache);

        // Linked GPU programs between runs (storm/render/program_cache.hpp).
        storm::render::ProgramCacheConfig programCache;
        programCache.directory = fs::GetCachePath() / "ProgramCache";
        storm::render::ConfigureProgramCache(programCache);

        // Relief maps (fx/relief_store.hpp): made by PBRify's models in the game process from the game's own files, never
        // shipped; derived data, so in the engine's cache root (fs::GetCachePath: on Android the app's cache) + "relief":
        // «Очистить кэш» or the system's eviction removes them and the game makes them again.
        // engine.ini relief_dir overrides (host checks with prepared maps). The models: APK assets on Android, the files
        // in engine.ini relief_models (the folder the build makes them in: src/libs/relief/model/README.md) on the host (none: no maps are made, logged).
        storm::render::ReliefConfig relief;
        ini->ReadString(nullptr, "relief_dir", str, sizeof(str), "");
        relief.directory = std::filesystem::u8path(str);
        relief.textures = std::filesystem::u8path(fio->ConvertPathResource("resource\\textures"));
        if (relief.directory.empty())
            relief.directory = fs::GetCachePath() / "relief";
#ifdef __ANDROID__
        relief.model = [](const char *file) {
            std::vector<uint8_t> model;
            if (SDL_RWops *asset = SDL_RWFromFile(file, "rb"))
            {
                const Sint64 size = SDL_RWsize(asset);
                model.resize(size > 0 ? static_cast<size_t>(size) : 0);
                if (model.empty() || SDL_RWread(asset, model.data(), model.size(), 1) != 1)
                    model.clear();
                SDL_RWclose(asset);
            }
            return model;
        };
#else
        ini->ReadString(nullptr, "relief_models", str, sizeof(str), "");
        if (*str)
            relief.model = [directory = std::filesystem::u8path(str)](const char *file) {
                std::ifstream in(directory / file, std::ios::binary);
                return std::vector<uint8_t>{std::istreambuf_iterator<char>(in), {}};
            };
#endif
        storm::render::ConfigureRelief(relief);
#ifdef __ANDROID__
        {
            // The relief maps of earlier builds in the files directories (kept across «Очистить кэш»). The old
            // locations in the app's caches (getCacheDir()/relief, /NormalMaps) are the app's to remove (GameCache).
            const char *external = SDL_AndroidGetExternalStoragePath();
            storm::render::RemoveOldReliefStores(
                {std::filesystem::path(SDL_AndroidGetInternalStoragePath()) / "relief",
                 std::filesystem::path(external && *external ? external : SDL_AndroidGetInternalStoragePath()) / "relief"},
                relief.directory);
        }
#else
        storm::render::RemoveOldReliefStores({fs::GetStashPath() / "NormalMaps"}, relief.directory);
#endif

        FovMultiplier = ini->GetFloat(nullptr, "fov_multiplier", 1.0f);

        screen_size.x = ini->GetInt(nullptr, "screen_x", 1024);
        screen_size.y = ini->GetInt(nullptr, "screen_y", 768);
        fNearClipPlane = ini->GetFloat(nullptr, "NearClipPlane", 0.1f);
        fFarClipPlane = ini->GetFloat(nullptr, "FarClipPlane", 4000.0f);
        bBackBufferCanLock = ini->GetInt(nullptr, "lockable_back_buffer", 0) != 0;
        ini->ReadString(nullptr, "screen_bpp", str, sizeof(str), "D3DFMT_R5G6B5");
        screen_bpp = D3DFMT_R5G6B5;
        stencil_format = D3DFMT_D16;
        if (storm::iEquals(str, "D3DFMT_A8R8G8B8"))
        {
            screen_bpp = D3DFMT_A8R8G8B8;
            stencil_format = D3DFMT_D24S8;
        }
        if (storm::iEquals(str, "D3DFMT_X8R8G8B8"))
        {
            screen_bpp = D3DFMT_X8R8G8B8;
            stencil_format = D3DFMT_D24S8;
        }
        if (storm::iEquals(str, "D3DFMT_R5G6B5"))
        {
            screen_bpp = D3DFMT_R5G6B5;
            stencil_format = D3DFMT_D16;
        }

        // new renderer settings
#ifdef __ANDROID__
        // A phone always presents in step with the display (no tearing, steady pacing, less heat);
        // there is no player setting for it.
        vSyncEnabled = true;
#else
        vSyncEnabled = ini->GetInt(nullptr, "vsync", 0);
#endif

        msaa = ini->GetInt(nullptr, "msaa", D3DMULTISAMPLE_NONE);
        if (msaa != D3DMULTISAMPLE_NONE)
        {
            if (msaa < D3DMULTISAMPLE_2_SAMPLES || msaa > D3DMULTISAMPLE_16_SAMPLES)
            {
                msaa = D3DMULTISAMPLE_16_SAMPLES;
            }
        }


        // stencil_format = D3DFMT_D24S8;
#ifdef __ANDROID__
        // SDL's view already exists and has been shown by main. Query it here,
        // immediately before device creation; imported PC dimensions are only
        // a fallback. Presentation continues to handle later view resizes.
        const storm::android::RenderExtent legacySize{screen_size.x, screen_size.y};
        const auto viewSize = core.GetWindow()->GetWindowSize();
        const auto startupSize = storm::android::chooseStartupRenderResolution(
            {viewSize.width, viewSize.height}, legacySize);
        // The view size is the default; an explicit graphics preference (lower render resolution,
        // scaled up by the composite pass) still wins.
        screen_size.x = std::clamp(storm::android::graphicsSetting("screen_x", startupSize.extent.width), 320, 8192);
        screen_size.y = std::clamp(storm::android::graphicsSetting("screen_y", startupSize.extent.height), 320, 8192);
        core.Trace("AndroidRenderResolution: legacy=%dx%d view=%dx%d chosen=%dx%d reason=%s",
                   legacySize.width, legacySize.height, viewSize.width, viewSize.height,
                   screen_size.x, screen_size.y, startupSize.reason);
#endif
        if (!InitDevice(bWindow, static_cast<HWND>(core.GetWindow()->OSHandle()), screen_size.x, screen_size.y))
#ifdef __ANDROID__
            throw std::runtime_error("Cannot initialize renderer: OpenGL ES 3.0 could not create a graphics device. "
                                     "The game cannot start with this graphics driver.");
#else
            throw std::runtime_error("Cannot initialize renderer: could not create an OpenGL ES 3.0 device");
#endif
#ifdef __ANDROID__
        // The render scale a later view size keeps (ApplyViewResize).
        renderScale_[0] = viewSize.width > 0 ? static_cast<float>(screen_size.x) / viewSize.width : 1.0f;
        renderScale_[1] = viewSize.height > 0 ? static_cast<float>(screen_size.y) / viewSize.height : 1.0f;
        core.Trace("AndroidRenderResolution: legacy=%dx%d view=%dx%d chosen=%dx%d final=%dx%d reason=%s deviceAdjusted=%d",
                   legacySize.width, legacySize.height, viewSize.width, viewSize.height,
                   startupSize.extent.width, startupSize.extent.height, screen_size.x, screen_size.y,
                   startupSize.reason,
                   screen_size.x != startupSize.extent.width || screen_size.y != startupSize.extent.height);
#endif

        gamePictures_ = std::make_unique<storm::GamePictures>(*this);
        storm::render::SetGamePictureProvider(gamePictures_.get());
        // The Android surface is destroyed in background: stop presenting before control returns
        // to SDL (early subscriber) and re-attach to the new native window on return.
        appEventsSubscription_ = storm::AppEvents::instance().subscribe(
            [this](const storm::AppEvent &event) {
                if (event.type == storm::AppEventType::WillEnterBackground)
                    storm::render::NotifySurfaceLost();
                else if (event.type == storm::AppEventType::DidEnterForeground)
                    storm::render::NotifySurfaceRestored();
#ifdef __ANDROID__
                else if (event.type == storm::AppEventType::WindowResized)
                    pendingViewResize_ = {event.width, event.height}; // applied between frames (RunStart)
#endif
            },
            true);
        pTechnique = std::make_unique<CTechnique>(this);
        pTechnique->DecodeFiles();

        // get start ini file for fonts
        if (!ini->ReadString(nullptr, "startFontIniFile", str, sizeof(str) - 1, ""))
        {
            core.Trace("Not finded 'startFontIniFile' parameter into ENGINE.INI file");
            sprintf_s(str, "resource\\ini\\fonts.ini");
        }
        const auto len = strlen(str) + 1;
        if ((fontIniFileName = new char[len]) == nullptr)
            throw std::runtime_error("allocate memory error");
        strcpy_s(fontIniFileName, len, str);
        // get start font quantity
        if (!ini->ReadString(nullptr, "font", str, sizeof(str) - 1, ""))
        {
            core.Trace("Start font not defined");
            sprintf_s(str, "normal");
        }
        if (LoadFont(str) == -1L)
            core.Trace("can not init start font: %s", str);
        idFontCurrent = 0L;

        // Progress image parameters
        progressFramesPosX = ini->GetFloat("ProgressImage", "RelativePosX", 0.85f);
        progressFramesPosY = ini->GetFloat("ProgressImage", "RelativePosY", 0.8f);
        progressFramesWidth = ini->GetFloat("ProgressImage", "RelativeWidth", 0.0625f);
        if (progressFramesWidth < 0.0f)
            progressFramesWidth = 0.0f;
        if (progressFramesWidth > 10.0f)
            progressFramesWidth = 10.0f;
        progressFramesHeight = ini->GetFloat("ProgressImage", "RelativeHeight", 0.0625f);
        if (progressFramesHeight < 0.0f)
            progressFramesHeight = 0.0f;
        if (progressFramesHeight > 10.0f)
            progressFramesHeight = 10.0f;
        progressFramesCountX = static_cast<int32_t>(ini->GetFloat("ProgressImage", "HorisontalFramesCount", 8));
        if (progressFramesCountX < 1)
            progressFramesCountX = 1;
        if (progressFramesCountX > 64)
            progressFramesCountX = 64;
        progressFramesCountY = static_cast<int32_t>(ini->GetFloat("ProgressImage", "VerticalFramesCount", 8));
        if (progressFramesCountY < 1)
            progressFramesCountY = 1;
        if (progressFramesCountY > 64)
            progressFramesCountY = 64;

        CreateSphere();
        auto *pScriptRender = static_cast<VDATA *>(core.GetScriptVariable("Render"));
        ATTRIBUTES *pARender = pScriptRender->GetAClass();

        pARender->SetAttributeUseDword("full_screen", !bWindow);
        pARender->SetAttributeUseDword("screen_x", screen_size.x);
        pARender->SetAttributeUseDword("screen_y", screen_size.y);
    }

    pDropConveyorVBuffer = nullptr;
    rectsVBuffer = nullptr;
    d3d9->CreateVertexBuffer(2 * sizeof(glm::vec3), D3DUSAGE_WRITEONLY, D3DFVF_XYZ, D3DPOOL_MANAGED,
                             &pDropConveyorVBuffer, nullptr);
    d3d9->CreateVertexBuffer(rectsVBuffer_SizeInRects * 6 * sizeof(RECT_VERTEX), D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,
                             RS_RECT_VERTEX_FORMAT, D3DPOOL_DEFAULT, &rectsVBuffer, nullptr); // D3DPOOL_MANAGED
    if (!pDropConveyorVBuffer || !rectsVBuffer)
    {
        throw std::runtime_error("Cannot initialize renderer: could not allocate vertex buffers");
    }

    // UNGUARD
    return true;
}

//################################################################################
Renderer::~Renderer()
{
    storm::render::ShutdownRelief();
    storm::render::ShutdownTextureCache();
    // d3d9->SetRenderTarget(NULL, NULL); //~!~

    S_RELEASE(pOriginalScreenSurface, 3);
    S_RELEASE(pOriginalDepthSurface, 4);

    S_RELEASE(rectsVBuffer, 8);
    S_RELEASE(pDropConveyorVBuffer, 9);

    delete[] progressImage;
    progressImage = nullptr;
    delete[] progressBackImage;
    progressBackImage = nullptr;
    delete[] progressTipsImage;
    progressTipsImage = nullptr;

    for (int i = 0; i < nFontQuantity; i++)
    {
        delete FontList[i].font;

        delete[] FontList[i].name;
    }
    nFontQuantity = 0;
    delete[] fontIniFileName;

    delete[] sphereVertex;
    sphereVertex = nullptr;
    ReleaseDevice();
    pRS = nullptr;
}

bool Renderer::InitDevice(bool windowed, HWND _hwnd, int32_t width, int32_t height)
{
    // GUARD(Renderer::InitDevice)

    aniVBuffer = nullptr;
    numAniVerteces = 0;

    screen_size.x = width;
    screen_size.y = height;
    bWindow = windowed;

    hwnd = _hwnd;
    core.Trace("Initializing DirectX 9");
    d3d = storm::shim::CreateDirect3D();
    if (d3d == nullptr)
    {
        // MessageBox(hwnd, "Direct3DCreate9 error", "InitDevice::Direct3DCreate9", MB_OK);
        core.Trace("Direct3DCreate9 error : InitDevice::Direct3DCreate9");
        return false;
    }

    d3dpp = {};
    d3dpp.BackBufferWidth = width;
    d3dpp.BackBufferHeight = height;
    d3dpp.BackBufferFormat = screen_bpp;
    d3dpp.BackBufferCount = 1;
    d3dpp.hDeviceWindow = hwnd;
    d3dpp.Windowed = windowed;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = stencil_format;

    if (windowed)
    {
        D3DDISPLAYMODE d3ddm;
        if (FAILED(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm)))
            throw std::runtime_error("failed to GetAdapterDisplayMode");
        d3dpp.BackBufferFormat = d3ddm.Format;
        if (d3dpp.BackBufferFormat == D3DFMT_R5G6B5)
            d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
        else
            d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
        stencil_format = d3dpp.AutoDepthStencilFormat;
        if (!bUseLargeBackBuffer)
            if (d3ddm.Width < static_cast<uint32_t>(width) || d3ddm.Height < static_cast<uint32_t>(height))
            {
                d3dpp.BackBufferWidth = d3ddm.Width;
                d3dpp.BackBufferHeight = d3ddm.Height;
                width = d3ddm.Width;
                height = d3ddm.Height;
            }
    }

    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    for (auto samples = msaa; samples >= D3DMULTISAMPLE_2_SAMPLES; samples--)
    {
        if (SUCCEEDED(d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.BackBufferFormat, false,
                                                      static_cast<D3DMULTISAMPLE_TYPE>(samples), nullptr)))
        {
            d3dpp.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(samples);
            break;
        }
    }
    if (msaa >= D3DMULTISAMPLE_2_SAMPLES && d3dpp.MultiSampleType == D3DMULTISAMPLE_NONE)
        spdlog::warn("engine.ini msaa = {}: no multisample back buffer is available on this device; the back buffer is "
                     "not multisampled (scene MSAA is the fx_msaa option)",
                     static_cast<int>(msaa));

    if (bBackBufferCanLock)
        d3dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    else
        d3dpp.Flags = 0;
    // d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    // d3dpp.SwapEffect = D3DSWAPEFFECT_COPY;
    // if(windowed) d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;//FLIP;
    // else d3dpp.SwapEffect = D3DSWAPEFFECT_FLIP;

    if (vSyncEnabled)
    {
        d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    }
    else
    {
        d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }

    // One device on bgfx; the behaviour flags of D3D9 mean nothing to the shim.
    if (CHECKD3DERR(d3d->CreateDevice(0, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &d3d9)))
        return false;

    // The back buffer and its depth: the target of every frame (SetScreenAsRenderTarget).
    d3d9->GetRenderTarget(0, &pOriginalScreenSurface);
    d3d9->GetDepthStencilSurface(&pOriginalDepthSurface);
    ClearSurface(pOriginalScreenSurface);

    OriginalViewPort.X = 0;
    OriginalViewPort.Y = 0;
    OriginalViewPort.Width = width;
    OriginalViewPort.Height = height;
    OriginalViewPort.MinZ = 0.0f;
    OriginalViewPort.MaxZ = 1.0f;

    for (int32_t b = 0; b < MAX_BUFFERS; b++)
    {
        VertexBuffers[b].buff = nullptr;
        IndexBuffers[b].buff = nullptr;
    }

    int32_t num_stages;
    num_stages = 8;

    for (int32_t s = 0; s < num_stages; s++)
    {
        // texture operation
        SetTextureStageState(s, D3DTSS_COLORARG1, D3DTA_CURRENT);
        SetTextureStageState(s, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        SetTextureStageState(s, D3DTSS_COLOROP, D3DTOP_DISABLE);

        // texture coord
        SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, s);

        // texture filtering
        SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    }

    // set base texture and diffuse+specular lighting
    SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

    // texture op for lightmaps
    // SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

    SetCamera(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), 1.0f);

    D3DLIGHT9 l{};
    l.Type = D3DLIGHT_POINT;
    l.Range = 100.0f;
    l.Attenuation0 = 1.0f;

    for (int i = 0; i < 8; i++)
    {
        d3d9->SetLight(i, &l);
        d3d9->LightEnable(i, false);
    }

    SetCommonStates();

    screen_size.x = width;
    screen_size.y = height;

    m_fHeightDeformator = (float)(height * 4.0f) / (float)(width * 3.0f);

    // UNGUARD
    return true;
}

//################################################################################
bool Renderer::ReleaseDevice()
{
    if (appEventsSubscription_ >= 0)
        storm::AppEvents::instance().unsubscribe(appEventsSubscription_);
    appEventsSubscription_ = -1;
    storm::render::SetGamePictureProvider(nullptr);
    gamePictures_.reset();
    if (aniVBuffer)
        aniVBuffer->Release();
    aniVBuffer = nullptr;
    numAniVerteces = 0;
    for (auto &declaration : vertexDeclarations_)
        declaration.declaration->Release();
    vertexDeclarations_.clear();
    for (auto *surface : depthSurfaces_)
        if (surface)
            surface->Release();
    depthSurfaces_.clear();
    for (int32_t b = 0; b < MAX_BUFFERS; b++)
    {
        if (VertexBuffers[b].buff)
            VertexBuffers[b].buff->Release();
        VertexBuffers[b].buff = nullptr;
        if (IndexBuffers[b].buff)
            IndexBuffers[b].buff->Release();
        IndexBuffers[b].buff = nullptr;
    }

    // COM Release returns the remaining reference count, not an HRESULT.
    for (int32_t t = 0; t < MAX_STEXTURES; t++)
        if (Textures[t].ref && Textures[t].loaded && Textures[t].d3dtex)
        {
            Textures[t].d3dtex->Release();
            Textures[t].ref = NULL;
            delete[] Textures[t].name;
        }

    if (d3d9 != nullptr)
        d3d9->Release();
    d3d9 = nullptr;
    if (d3d != nullptr)
        d3d->Release();
    d3d = nullptr;
    return true;
}

//################################################################################
bool Renderer::ClearFrame(int32_t type)
{
    if (CHECKD3DERR(d3d9->Clear(0L, NULL, type, dwBackColor, 1.0f, 0L)) == true)
        return false;
    // if(CHECKD3DERR(d3d9->Clear(0L, NULL, type, 0x0, 1.0f, 0L))==true)    return false;
    return true;
}

void Renderer::ClearSurface(storm::shim::Surface *pSurf)
{
    HRESULT hr = SetRenderTarget(pSurf, nullptr);
    hr = BeginScene();
    hr = d3d9->Clear(0, nullptr, D3DCLEAR_TARGET, 0x0, 0.0f, 0x0);
    hr = EndScene();
}

void Renderer::SetScreenAsRenderTarget()
{
    SetRenderTarget(pOriginalScreenSurface, pOriginalDepthSurface);
    SetViewport(&OriginalViewPort);
}

void Renderer::MakePostProcess()
{
    // Every interface calls this before it draws: the 3D scene of this frame ends here (graphics effects).
    storm::render::EffectsSceneComplete();
}

//################################################################################
bool Renderer::DrawFrameStats()
{
    if (bShowFps)
    {
        Print(screen_size.x - 100, screen_size.y - 50, "FPS %d", core.EngineFps());
    }

    if (bShowExInfo)
    {
        uint32_t dwTotalTexSize = 0;
        uint32_t dwTotalTexNum = 0, dwTotalVB = 0, dwTotalIB = 0, dwTotalVBSize = 0, dwTotalIBSize = 0;

        int32_t t;
        for (t = 0; t < MAX_STEXTURES; t++)
            if (Textures[t].ref > 0)
            {
                dwTotalTexSize += Textures[t].dwSize;
                dwTotalTexNum++;
            }

        for (t = 0; t < MAX_BUFFERS; t++)
        {
            if (VertexBuffers[t].buff)
            {
                dwTotalVBSize += VertexBuffers[t].size;
                dwTotalVB++;
            }
            if (IndexBuffers[t].buff)
            {
                dwTotalIBSize += IndexBuffers[t].size;
                dwTotalIB++;
            }
        }

        glm::mat4 mView;
        GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(mView));
        storm::math::invertRigid(mView);
        Print(80, 50, "Cam: %.3f, %.3f, %.3f", storm::math::pos(mView).x, storm::math::pos(mView).y, storm::math::pos(mView).z);
        Print(80, 70, "t : %d, %.3f Mb", dwTotalTexNum, float(dwTotalTexSize) / (1024.0f * 1024.0f));
        Print(80, 90, "v : %d, %.3f Mb", dwTotalVB, float(dwTotalVBSize) / (1024.0f * 1024.0f));
        Print(80, 110, "i : %d, %.3f Mb", dwTotalIB, float(dwTotalIBSize) / (1024.0f * 1024.0f));
        Print(80, 130, "d : %d, lv: %d, li: %d", dwNumDrawPrimitive, dwNumLV, dwNumLI);
        Print(80, 150, "s : %d, %.3f, %.3f", dwSoundBuffersCount, dwSoundBytes / 1024.f, dwSoundBytesCached / 1024.f);
    }

    // Try to drop video conveyor
    if (bDropVideoConveyor && pDropConveyorVBuffer)
    {
        glm::vec3 *pV;
        pDropConveyorVBuffer->Lock(0, 0, (void **)&pV, 0);
        for (int32_t i = 0; i < 2; i++)
            pV[i] = glm::vec3(1e6f, 1e6f, 1e6f);
        pDropConveyorVBuffer->Unlock();
        d3d9->SetStreamSource(0, pDropConveyorVBuffer, 0, sizeof(glm::vec3));
        SetFVF(D3DFVF_XYZ);
        DrawPrimitive(D3DPT_LINELIST, 0, 1);
    }

    if (CHECKD3DERR(EndScene()))
    {
#ifdef __ANDROID__
        storm::android::worldMapHud.presented(false);
#endif
        return false;
    }

    const auto presentStart = renderMetricsEnabled ? SDL_GetPerformanceCounter() : 0;
    const HRESULT hRes = d3d9->Present();
#ifdef __ANDROID__
    storm::android::worldMapHud.presented(hRes==D3D_OK);
#endif
#ifdef STORM_SCENE_SWEEP
    storm::scene_sweep::framePresented(dwNumDrawPrimitive);
#endif
    if (renderMetricsEnabled)
    {
        const auto presentEnd = SDL_GetPerformanceCounter();
        const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
        const double now = static_cast<double>(presentEnd) / frequency;
        if (hRes == D3D_OK && metricsPreviousPresent != 0.0)
        {
            const double interval = now - metricsPreviousPresent;
            metricsFrameTimes.push_back(interval * 1000.0);
            metricsElapsed += interval;
            metricsPresentTime += static_cast<double>(presentEnd - presentStart) / frequency;
            metricsDrawCalls += dwNumDrawPrimitive;
            // Also bound memory if a driver presents without throttling.
            if (metricsElapsed >= 5.0 || metricsFrameTimes.size() >= 4096)
            {
                std::sort(metricsFrameTimes.begin(), metricsFrameTimes.end());
                const auto count = metricsFrameTimes.size();
                const auto percentile = [&](size_t percent) {
                    return metricsFrameTimes[(count * percent + 99) / 100 - 1];
                };
                const storm::render::PerfCounters &perfTotals = storm::render::PerfTotals();
                core.Trace("RenderMetrics: frames=%u seconds=%.3f fps=%.2f frame_ms_p50=%.2f "
                           "frame_ms_p95=%.2f frame_ms_p99=%.2f frame_ms_max=%.2f "
                           "present_ms_mean=%.2f draws_mean=%.1f resolution=%dx%d %s",
                           static_cast<unsigned>(count), metricsElapsed, count / metricsElapsed,
                           percentile(50), percentile(95), percentile(99), metricsFrameTimes.back(),
                           1000.0 * metricsPresentTime / count, static_cast<double>(metricsDrawCalls) / count,
                           screen_size.x, screen_size.y, perfTotals.Since(metricsPerfStart).Summary().c_str());
                metricsPerfStart = perfTotals;
                metricsFrameTimes.clear();
                metricsElapsed = metricsPresentTime = 0.0;
                metricsDrawCalls = 0;
            }
        }
        if (hRes != D3D_OK || metricsPreviousPresent == 0.0)
            metricsPerfStart = storm::render::PerfTotals();
        if (hRes != D3D_OK)
        {
            metricsFrameTimes.clear();
            metricsElapsed = metricsPresentTime = 0.0;
            metricsDrawCalls = 0;
        }
        metricsPreviousPresent = hRes == D3D_OK ? now : 0.0;
    }

    if (hRes == D3DERR_DEVICELOST)
    {
        LostRender();
    }


    return true;
}

//################################################################################
static int totSize = 0;

int32_t Renderer::TextureCreate(const char *fname)
{
    // start add texture path
    if ((uintptr_t)fname == -1)
    {
        iSetupPath = 1;
        return -1;
    }

    // continue add texture path process
    if (iSetupPath)
    {
        if (iSetupPath == 1)
        {
            dwSetupNumber = (uintptr_t)(fname);
            iSetupPath = 2;
            return -1;
        }

        TexPaths[dwSetupNumber].str[0] = 0;
        if (fname && fname[0])
            strcpy_s(TexPaths[dwSetupNumber].str, fname);
        iSetupPath = 0;
        return -1;
    }

    if (fname == nullptr)
    {
        core.Trace("Can't create texture with null name");
        return -1L;
    }

    if (!bLoadTextureEnabled)
        return -1;

    for (int32_t i = 3; i >= -1; i--)
    {
        char _fname[256];

        if (i >= 0 && TexPaths[i].str[0] == 0)
            continue;
        if (i >= 0)
        {
            const uint32_t dwLen = strlen(fname);

            int32_t j;
            for (j = dwLen - 1; j >= 0; j--)
                if (fname[j] == '\\')
                    break;

            _fname[0] = 0;
            strncpy_s(_fname, fname, j + 1);
            _fname[j + 1] = 0;
            strcat_s(_fname, TexPaths[i].str);
            strcat_s(_fname, &fname[j + 1]);
            bTrace = false;
        }
        else
        {
            bTrace = true;
            strcpy_s(_fname, fname);
        }

        std::ranges::for_each(_fname, [](char &c) { c = std::toupper(c); });

        const uint32_t hf = MakeHashValue(_fname);

        int32_t t;
        for (t = 0; t < MAX_STEXTURES; t++)
            if (Textures[t].ref != 0)
                if (Textures[t].name)
                    if (Textures[t].hash == hf && storm::iEquals(Textures[t].name, _fname))
                    {
                        Textures[t].ref++;
                        return t;
                    }

        for (t = 0; t < MAX_STEXTURES; t++)
            if (Textures[t].ref == 0)
                break;

        Textures[t].hash = hf;

        const auto len = strlen(_fname) + 1;
        if ((Textures[t].name = new char[len]) == nullptr)
            throw std::runtime_error("allocate memory error");
        strcpy_s(Textures[t].name, len, _fname);
        Textures[t].isCubeMap = false;
        Textures[t].loaded = false;
        Textures[t].ref++;
        if (TextureLoad(t))
            return t;
        Textures[t].ref--;
        delete[] Textures[t].name;
        Textures[t].name = nullptr;
    }
    return -1;
}

int32_t Renderer::AddCodeTexture(storm::shim::BaseTexture *texture, uint32_t size, bool cubeMap)
{
    const auto slot = std::find_if(std::begin(Textures), std::end(Textures),
                                   [](const STEXTURE &entry) { return entry.ref == 0; });
    if (slot == std::end(Textures))
    {
        spdlog::error("Renderer: all {} texture slots are in use", MAX_STEXTURES);
        texture->Release();
        return -1;
    }
    *slot = {};
    slot->d3dtex = texture;
    slot->ref = 1;
    slot->dwSize = size;
    slot->isCubeMap = cubeMap;
    slot->loaded = true;
    return static_cast<int32_t>(slot - std::begin(Textures));
}

storm::shim::BaseTexture *Renderer::CodeTexture(int32_t texid, const char *operation)
{
    if (texid < 0 || texid >= MAX_STEXTURES || Textures[texid].ref <= 0 || !Textures[texid].d3dtex)
    {
        spdlog::error("Renderer: {} with invalid texture id {}", operation, texid);
        return nullptr;
    }
    return Textures[texid].d3dtex;
}

int32_t Renderer::TextureCreate(UINT width, UINT height, UINT levels, uint32_t usage, D3DFORMAT format, D3DPOOL pool)
{
    storm::shim::Texture2D *texture = nullptr;
    if (CHECKD3DERR(CreateTexture(width, height, levels, usage, format, pool, &texture)))
    {
        spdlog::error("Renderer: cannot create a {}x{} texture (format {}, usage {:#x})", width, height,
                      static_cast<int>(format), usage);
        return -1;
    }
    return AddCodeTexture(texture, width * height * 4, false);
}

int32_t Renderer::CubeTextureCreate(UINT edgeLength, UINT levels, uint32_t usage, D3DFORMAT format, D3DPOOL pool)
{
    storm::shim::CubeTexture *texture = nullptr;
    if (CHECKD3DERR(CreateCubeTexture(edgeLength, levels, usage, format, pool, &texture)))
    {
        spdlog::error("Renderer: cannot create a {} cube texture (format {}, usage {:#x})", edgeLength,
                      static_cast<int>(format), usage);
        return -1;
    }
    return AddCodeTexture(texture, edgeLength * edgeLength * 4 * 6, true);
}

int32_t Renderer::VolumeTextureCreate(UINT width, UINT height, UINT depth, UINT levels, uint32_t usage,
                                       D3DFORMAT format, D3DPOOL pool)
{
    storm::shim::VolumeTexture *texture = nullptr;
    if (CHECKD3DERR(d3d9->CreateVolumeTexture(width, height, depth, levels, usage, format, pool, &texture, nullptr)))
    {
        spdlog::error("Renderer: cannot create a {}x{}x{} volume texture (format {})", width, height, depth,
                      static_cast<int>(format));
        return -1;
    }
    return AddCodeTexture(texture, width * height * depth * 4, false);
}

bool Renderer::TextureLockBox(int32_t texid, uint32_t level, D3DLOCKED_BOX &locked, uint32_t flags)
{
    auto *texture = CodeTexture(texid, "TextureLockBox");
    if (!texture)
        return false;
    if (texture->GetType() != D3DRTYPE_VOLUMETEXTURE ||
        CHECKD3DERR(static_cast<storm::shim::VolumeTexture *>(texture)->LockBox(level, &locked, nullptr, flags)) ||
        !locked.pBits)
    {
        spdlog::error("Renderer: cannot lock level {} of volume texture {}", level, texid);
        return false;
    }
    return true;
}

void Renderer::TextureUnlockBox(int32_t texid, uint32_t level)
{
    auto *texture = CodeTexture(texid, "TextureUnlockBox");
    if (texture && texture->GetType() == D3DRTYPE_VOLUMETEXTURE)
        CHECKD3DERR(static_cast<storm::shim::VolumeTexture *>(texture)->UnlockBox(level));
}

bool Renderer::TextureLock(int32_t texid, uint32_t level, D3DLOCKED_RECT &locked, uint32_t flags, uint32_t cubeFace)
{
    auto *texture = CodeTexture(texid, "TextureLock");
    if (texture && texture->GetType() == D3DRTYPE_VOLUMETEXTURE)
    {
        spdlog::error("Renderer: TextureLock on volume texture {}", texid);
        return false;
    }
    if (!texture)
        return false;
    const auto result =
        Textures[texid].isCubeMap
            ? static_cast<storm::shim::CubeTexture *>(texture)->LockRect(static_cast<D3DCUBEMAP_FACES>(cubeFace), level,
                                                                       &locked, nullptr, flags)
            : static_cast<storm::shim::Texture2D *>(texture)->LockRect(level, &locked, nullptr, flags);
    if (CHECKD3DERR(result) || !locked.pBits)
    {
        spdlog::error("Renderer: cannot lock level {} of texture {}", level, texid);
        return false;
    }
    return true;
}

void Renderer::TextureUnlock(int32_t texid, uint32_t level, uint32_t cubeFace)
{
    auto *texture = CodeTexture(texid, "TextureUnlock");
    if (texture && texture->GetType() == D3DRTYPE_VOLUMETEXTURE)
    {
        spdlog::error("Renderer: TextureUnlock on volume texture {}", texid);
        return;
    }
    if (!texture)
        return;
    if (Textures[texid].isCubeMap)
        CHECKD3DERR(static_cast<storm::shim::CubeTexture *>(texture)->UnlockRect(static_cast<D3DCUBEMAP_FACES>(cubeFace),
                                                                             level));
    else
        CHECKD3DERR(static_cast<storm::shim::Texture2D *>(texture)->UnlockRect(level));
}

bool Renderer::TextureLevelDesc(int32_t texid, uint32_t level, D3DSURFACE_DESC &desc)
{
    auto *texture = CodeTexture(texid, "TextureLevelDesc");
    if (texture && texture->GetType() == D3DRTYPE_VOLUMETEXTURE)
    {
        spdlog::error("Renderer: TextureLevelDesc on volume texture {}", texid);
        return false;
    }
    if (!texture)
        return false;
    const auto result = Textures[texid].isCubeMap
                            ? static_cast<storm::shim::CubeTexture *>(texture)->GetLevelDesc(level, &desc)
                            : static_cast<storm::shim::Texture2D *>(texture)->GetLevelDesc(level, &desc);
    return !CHECKD3DERR(result);
}

storm::render::DepthSurfaceId Renderer::DepthSurfaceCreate(UINT width, UINT height, D3DFORMAT format)
{
    storm::shim::Surface *surface = nullptr;
    if (CHECKD3DERR(CreateDepthStencilSurface(width, height, format, D3DMULTISAMPLE_NONE, &surface)))
    {
        spdlog::error("Renderer: cannot create a {}x{} depth surface (format {})", width, height,
                      static_cast<int>(format));
        return {};
    }
    const auto free = std::ranges::find(depthSurfaces_, nullptr);
    if (free != depthSurfaces_.end())
    {
        *free = surface;
        return {static_cast<int32_t>(free - depthSurfaces_.begin())};
    }
    depthSurfaces_.push_back(surface);
    return {static_cast<int32_t>(depthSurfaces_.size() - 1)};
}

void Renderer::DepthSurfaceRelease(storm::render::DepthSurfaceId surface)
{
    if (!surface)
        return;
    if (static_cast<size_t>(surface.value) >= depthSurfaces_.size() || !depthSurfaces_[surface.value])
    {
        spdlog::error("Renderer: DepthSurfaceRelease with invalid id {}", surface.value);
        return;
    }
    depthSurfaces_[surface.value]->Release();
    depthSurfaces_[surface.value] = nullptr;
}

bool Renderer::SetRenderTarget(int32_t texid, uint32_t level, uint32_t cubeFace, storm::render::DepthSurfaceId depth)
{
    auto *texture = CodeTexture(texid, "SetRenderTarget");
    if (texture && texture->GetType() == D3DRTYPE_VOLUMETEXTURE)
    {
        spdlog::error("Renderer: SetRenderTarget on volume texture {}", texid);
        return false;
    }
    if (!texture)
        return false;
    storm::shim::Surface *depthSurface = nullptr;
    if (depth)
    {
        if (static_cast<size_t>(depth.value) >= depthSurfaces_.size() || !depthSurfaces_[depth.value])
        {
            spdlog::error("Renderer: SetRenderTarget with invalid depth surface id {}", depth.value);
            return false;
        }
        depthSurface = depthSurfaces_[depth.value];
    }
    storm::shim::Surface *surface = nullptr;
    const auto result =
        Textures[texid].isCubeMap
            ? static_cast<storm::shim::CubeTexture *>(texture)->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(cubeFace),
                                                                               level, &surface)
            : static_cast<storm::shim::Texture2D *>(texture)->GetSurfaceLevel(level, &surface);
    if (CHECKD3DERR(result))
        return false;
    const bool ok = !CHECKD3DERR(SetRenderTarget(surface, depthSurface));
    surface->Release();
    return ok;
}

bool Renderer::TextureIncReference(int32_t texid)
{
    ++Textures[texid].ref;
    return true;
}

bool Renderer::TextureLoad(int32_t t)
{
    using namespace std::literals;

    ProgressView();
    // Form the path to the texture
    char fn[_MAX_FNAME];
    Textures[t].dwSize = 0;
    if (Textures[t].name == nullptr)
    {
        return false;
    }

    auto lTexture = std::string(Textures[t].name);
    std::transform(lTexture.begin(), lTexture.end(), lTexture.begin(), [](unsigned char c) { return std::tolower(c); });
    auto has_resource_prefix = starts_with(lTexture, "resource\\textures\\");
    auto has_tx_postfix = ends_with(lTexture, ".tx");

    sprintf_s(fn, "%s%s%s", has_resource_prefix ? "" : "resource\\textures\\", Textures[t].name, has_tx_postfix ? "" : ".tx");

    for (int32_t s = 0, d = 0; fn[d]; s++)
    {
        if (d > 0 && (fn[d - 1] == PATH_SEP || fn[d - 1] == WRONG_PATH_SEP) &&
            (fn[s] == PATH_SEP || fn[s] == WRONG_PATH_SEP))
        {
            continue;
        }
        fn[d++] = fn[s];
    }
    // Opening the file
    auto fileS = fio->_CreateFile(fn, std::ios::binary | std::ios::in);
    if (!fileS.is_open())
    {
        if (bTrace)
        {
            core.Trace("Can't load texture %s", fn);
        }
        delete[] Textures[t].name;
        Textures[t].name = nullptr;
        return false;
    }
    // Reading the header
    TX_FILE_HEADER head;
    if (!fio->_ReadFile(fileS, &head, sizeof(head)))
    {
        if (bTrace)
        {
            core.Trace("Can't load texture %s", fn);
        }
        delete[] Textures[t].name;
        Textures[t].name = nullptr;
        fio->_CloseFile(fileS);
        return false;
    }
    // Analyzing the format
    D3DFORMAT d3dFormat = D3DFMT_UNKNOWN;
    int32_t textureFI;
    for (textureFI = 0; textureFI < sizeof(textureFormats) / sizeof(SD_TEXTURE_FORMAT); textureFI++)
    {
        if (textureFormats[textureFI].txFormat == head.format)
        {
            break;
        }
    }
    if (textureFI == sizeof(textureFormats) / sizeof(SD_TEXTURE_FORMAT) || head.flags & TX_FLAGS_PALLETTE)
    {
        if (bTrace)
        {
            core.Trace("Invalidate texture format %s, not loading it.", fn);
        }
        delete[] Textures[t].name;
        Textures[t].name = nullptr;
        fio->_CloseFile(fileS);
        return false;
    }
    d3dFormat = textureFormats[textureFI].d3dFormat;
    bool isSwizzled = textureFormats[textureFI].isSwizzled;
    const char *formatTxt = textureFormats[textureFI].format;
    // Skipping mips
    uint32_t seekposition = 0;
    for (int32_t nTD = nTextureDegradation; nTD > 0; nTD--)
    {
        if (head.nmips <= 1 || head.width <= 32 || head.height <= 32)
        {
            break; // degradation limit
        }
        seekposition += head.mip_size;
        head.nmips--;
        head.width /= 2;
        head.height /= 2;
        head.mip_size /= 4;
    }
    // Loading the texture
    if (!(head.flags & TX_FLAGS_CUBEMAP))
    {
        // Loading a regular texture
        // Position in file
        if (seekposition)
        {
            fio->_SetFilePointer(fileS, seekposition, std::ios::cur);
        }
        // create the texture
        storm::shim::Texture2D *tex = nullptr;
        if (CHECKD3DERR(d3d9->CreateTexture(head.width, head.height, head.nmips, 0, d3dFormat, D3DPOOL_MANAGED, &tex,
                                            NULL)) == true ||
            !tex)
        {
            if (bTrace)
            {
                core.Trace(
                    "Texture %s is not created (width: %i, height: %i, num mips: %i, format: %s), not loading it.", fn,
                    head.width, head.height, head.nmips, formatTxt);
            }
            delete[] Textures[t].name;
            Textures[t].name = nullptr;
            fio->_CloseFile(fileS);
            return false;
        }
        // Filling the levels
        for (int32_t m = 0; m < head.nmips; m++)
        {
            // take into account the size of the mip
            Textures[t].dwSize += head.mip_size;
            // Getting the mip surface
            bool isError = false;
            storm::shim::Surface *surface = nullptr;
            if (CHECKD3DERR(tex->GetSurfaceLevel(m, &surface)) == true || !surface)
            {
                isError = true;
            }
            else
            {
                // read the mip
                isError = !LoadTextureSurface(fileS, surface, head.mip_size, head.width, head.height, isSwizzled, fn);
            }
            // Freeing the surface
            if (surface)
            {
                surface->Release();
            }
            // If there was an error, then interrupt the download
            if (isError)
            {
                if (bTrace)
                {
                    core.Trace("Can't loading mip %i, texture %s is not created (width: %i, height: %i, num mips: %i, "
                               "format: %s), not loading it.",
                               m, fn, head.width, head.height, head.nmips, formatTxt);
                }
                delete[] Textures[t].name;
                Textures[t].name = nullptr;
                fio->_CloseFile(fileS);
                tex->Release();
                return false;
            }
            // recalculate the dimensions for the next mip
            head.width /= 2;
            head.height /= 2;
            head.mip_size /= 4;
        }
        Textures[t].d3dtex = tex;
        Textures[t].isCubeMap = false;
        storm::render::SetEffectsTextureSource(tex, Textures[t].name, // relief (fx normal maps)
                                               std::filesystem::u8path(fio->ConvertPathResource(fn)));
    }
    else
    {
        // Download cubemap
        if (head.width != head.height)
        {
            if (bTrace)
            {
                core.Trace("Cube map texture can't has not squared sides %s, not loading it.", fn);
            }
            delete[] Textures[t].name;
            Textures[t].name = nullptr;
            fio->_CloseFile(fileS);
            return false;
        }
        // Number of mips
        D3DCAPS9 devcaps;
        if (CHECKD3DERR(d3d9->GetDeviceCaps(&devcaps)))
        {
            if (bTrace)
            {
                core.Trace("Cube map texture %s is not created (size: %i, num mips: %i, format: %s), not loading it.",
                           fn, head.width, head.nmips, formatTxt);
            }
            delete[] Textures[t].name;
            Textures[t].name = nullptr;
            fio->_CloseFile(fileS);
            return false;
        }
        if (!(devcaps.TextureCaps & D3DPTEXTURECAPS_MIPCUBEMAP))
        {
            head.nmips = 1;
        }
        // create the texture
        storm::shim::CubeTexture *tex = nullptr;
        if (CHECKD3DERR(d3d9->CreateCubeTexture(head.width, head.nmips, 0, d3dFormat, D3DPOOL_MANAGED, &tex, NULL)) ==
                true ||
            !tex)
        {
            if (bTrace)
            {
                core.Trace("Cube map texture %s is not created (size: %i, num mips: %i, format: %s), not loading it.",
                           fn, head.width, head.nmips, formatTxt);
            }
            delete[] Textures[t].name;
            Textures[t].name = nullptr;
            fio->_CloseFile(fileS);
            return false;
        }
        // Loading the sides
        bool isError = false;
        if (seekposition)
        {
            fio->_SetFilePointer(fileS, seekposition, std::ios::cur);
        }
        uint32_t sz =
            LoadCubmapSide(fileS, tex, D3DCUBEMAP_FACE_POSITIVE_Z, head.nmips, head.mip_size, head.width, isSwizzled,
                           fn);
        if (sz)
        {
            Textures[t].dwSize += sz;
            if (seekposition)
            {
                fio->_SetFilePointer(fileS, seekposition, std::ios::cur);
            }
            sz = LoadCubmapSide(fileS, tex, D3DCUBEMAP_FACE_POSITIVE_X, head.nmips, head.mip_size, head.width,
                                isSwizzled, fn);
            if (sz)
            {
                Textures[t].dwSize += sz;
                if (seekposition)
                {
                    fio->_SetFilePointer(fileS, seekposition, std::ios::cur);
                }
                sz = LoadCubmapSide(fileS, tex, D3DCUBEMAP_FACE_NEGATIVE_Z, head.nmips, head.mip_size, head.width,
                                    isSwizzled, fn);
                if (sz)
                {
                    Textures[t].dwSize += sz;
                    if (seekposition)
                    {
                        fio->_SetFilePointer(fileS, seekposition, std::ios::cur);
                    }
                    sz = LoadCubmapSide(fileS, tex, D3DCUBEMAP_FACE_NEGATIVE_X, head.nmips, head.mip_size, head.width,
                                        isSwizzled, fn);
                    if (sz)
                    {
                        Textures[t].dwSize += sz;
                        if (seekposition)
                        {
                            fio->_SetFilePointer(fileS, seekposition, std::ios::cur);
                        }
                        sz = LoadCubmapSide(fileS, tex, D3DCUBEMAP_FACE_POSITIVE_Y, head.nmips, head.mip_size,
                                            head.width, isSwizzled, fn);
                        if (sz)
                        {
                            Textures[t].dwSize += sz;
                            if (seekposition)
                            {
                                fio->_SetFilePointer(fileS, seekposition, std::ios::cur);
                            }
                            sz = LoadCubmapSide(fileS, tex, D3DCUBEMAP_FACE_NEGATIVE_Y, head.nmips, head.mip_size,
                                                head.width, isSwizzled, fn);
                            if (!sz)
                            {
                                isError = true;
                            }
                            Textures[t].dwSize += sz;
                        }
                        else
                        {
                            isError = true;
                        }
                    }
                    else
                    {
                        isError = true;
                    }
                }
                else
                {
                    isError = true;
                }
            }
            else
            {
                isError = true;
            }
        }
        else
        {
            isError = true;
        }

        if (isError)
        {
            if (bTrace)
            {
                core.Trace("Cube map texture %s can't loading (size: %i, num mips: %i, format: %s), not loading it.",
                           fn, head.width, head.nmips, formatTxt);
            }
            delete[] Textures[t].name;
            Textures[t].name = nullptr;
            fio->_CloseFile(fileS);
            tex->Release();
            return false;
        }
        Textures[t].d3dtex = tex;
        Textures[t].isCubeMap = true;
    }

    //---------------------------------------------------------------
    // print statistics
    //---------------------------------------------------------------
    if (texLog)
    {
        char s[256];
        if (totSize == 0)
        {
            fio->_DeleteFile("texLoad.txt");
        }
        auto fileS2 = fio->_CreateFile("texLoad.txt", std::ios::binary | std::ios::out | std::ios::app);
        totSize += Textures[t].dwSize;
        sprintf_s(s, "%.2f, size: %d, %d * %d, %s\n", totSize / 1024.0f / 1024.0f, Textures[t].dwSize, head.width,
                  head.height, Textures[t].name);
        fio->_WriteFile(fileS2, s, strlen(s));
        fio->_FlushFileBuffers(fileS2);
        fio->_CloseFile(fileS2);
    }
    dwTotalSize += Textures[t].dwSize;
    //---------------------------------------------------------------
    Textures[t].loaded = true;
    storm::render::MarkTextureImmutable(Textures[t].d3dtex); // the shim keeps no CPU copy after the upload
    // Close the file
    fio->_CloseFile(fileS);
    return true;
}

uint32_t Renderer::LoadCubmapSide(std::fstream &fileS, storm::shim::CubeTexture *tex, D3DCUBEMAP_FACES face,
                                   uint32_t numMips, uint32_t mipSize, uint32_t size, bool isSwizzled,
                                   const char *name)
{
    uint32_t texsize = 0;
    // Filling the levels
    for (uint32_t m = 0; m < numMips; m++)
    {
        // take into account the size of the mip
        texsize += mipSize;
        // Getting the mip surface
        bool isError = false;
        storm::shim::Surface *surface = nullptr;
        if (CHECKD3DERR(tex->GetCubeMapSurface(face, m, &surface)) == true || !surface)
        {
            isError = true;
        }
        else
        {
            // read the mip
            isError = !LoadTextureSurface(fileS, surface, mipSize, size, size, isSwizzled, name);
        }
        // Freeing the surface
        if (surface)
        {
            surface->Release();
        }
        // If there was an error, then interrupt the download
        if (isError)
        {
            if (bTrace)
            {
                core.Trace("Can't loading cubemap mip %i (side: %i), not loading it.", m, face);
            }
            return 0;
        }
        // recalculate the dimensions for the next mip
        size /= 2;
        mipSize /= 4;
    }
    return texsize;
}

bool Renderer::LoadTextureSurface(std::fstream &fileS, storm::shim::Surface *suface, uint32_t mipSize, uint32_t width,
                                   uint32_t height, bool isSwizzled, const char *name)
{
    // The file holds mipSize bytes for this level (the header's level-0 size over 4 per level); the surface holds the
    // level's real size, which differs for sides that do not halve evenly (800x600 level 4: 50x37, 7400 bytes, the
    // file 7500) or a damaged header. Read what fits, step over the rest of the level, report the mismatch.
    D3DLOCKED_RECT lock;
    if (CHECKD3DERR(suface->LockRect(&lock, NULL, 0L)) == true)
    {
        return false;
    }
    const uint32_t rows = isSwizzled ? std::max(1u, (height + 3) / 4) : std::max(1u, height); // DXT: block rows
    const uint32_t capacity = static_cast<uint32_t>(lock.Pitch) * rows;
    const uint32_t read = std::min(mipSize, capacity);
    bool ok = fio->_ReadFile(fileS, lock.pBits, read);
    if (ok && read < mipSize)
    {
        fio->_SetFilePointer(fileS, mipSize - read, std::ios::cur);
        ok = fileS.good();
    }
    if (mipSize != capacity)
        spdlog::error("texture {}: a {}x{} level holds {} bytes in the file and {} in the texture: {} read{}", name,
                      width, height, mipSize, capacity, read, read < capacity ? ", the rest left black" : "");
    // Surface release
    if (CHECKD3DERR(suface->UnlockRect()) == true)
    {
        return false;
    }
    return ok;
}

//################################################################################
bool Renderer::TextureSet(int32_t stage, int32_t texid)
{
    if (texid == -1)
    {
        if (CHECKD3DERR(d3d9->SetTexture(stage, NULL)) == true)
        {
            return false;
        }
        return true;
    }

    /*
    if(Textures[texid].loaded==false)
    {
    int tex2load[MAX_STEXTURES];
    int t2l=0;
    for(int32_t t=0; t<MAX_STEXTURES; t++)
    if(Textures[t].ref>0 && Textures[t].loaded==false)    tex2load[t2l++] = t;
    */
    /*/sort textures
    for(t=0; t<t2l; t++)
    for(int32_t tt=t; tt<t2l; tt++)
    if(_strcmpi(Textures[tex2load[tt]].name, Textures[tex2load[t]].name)<0)
    {
    int ttemp = tex2load[t];
    tex2load[t] = tex2load[tt];
    tex2load[tt] = ttemp;
    }*/
    /*
    for(t=0; t<t2l; t++)
    {
    Textures[tex2load[t]].loaded = true;
    TextureLoad(tex2load[t]);
    }
    //Textures[texid].loaded = true;    TextureLoad(texid);
    }
    */

    if (CHECKD3DERR(d3d9->SetTexture(stage, Textures[texid].d3dtex)) == true)
    {
        return false;
    }
    return true;
}

//################################################################################
bool Renderer::TextureRelease(int32_t texid)
{
    if (texid == -1)
    {
        return true;
    }
    Textures[texid].ref--;
    if (Textures[texid].ref != 0)
    {
        return false;
    }
    keptPixels_.erase(texid);
    if (Textures[texid].name != nullptr)
    {
        if (texLog)
        {
            auto fileS = fio->_CreateFile("texLoad.txt", std::ios::binary | std::ios::in | std::ios::out);
            const int bytes = fio->_GetFileSize("texLoad.txt");
            auto buf = new char[bytes + 1];
            fio->_ReadFile(fileS, buf, bytes);
            buf[bytes] = 0;

            char *str = strstr(buf, Textures[texid].name);
            if (str != nullptr)
            {
                fio->_SetFilePointer(fileS, str - buf, std::ios::beg);
                auto s = "*";
                fio->_WriteFile(fileS, s, 1);
            }
            delete[] buf;
            fio->_FlushFileBuffers(fileS);
            fio->_CloseFile(fileS);

            /*FILE *flstat = fopen("texLoad.txt", "r+b");
            totSize -= Textures[texid].dwSize;
            fseek(flstat, 0, SEEK_END);
            int bytes = ftell(flstat);
            char *buf = new char[bytes+1];
            fseek(flstat, 0, SEEK_SET);
            fread(buf, bytes, sizeof *buf, flstat);
            buf[bytes] = 0;

            char *str = strstr(buf, Textures[texid].name);
            if(str!=0)
            {
            fseek(flstat, str-buf, SEEK_SET);
            const char *s = "*";
            fwrite(s, 1, 1, flstat);
            }
            delete buf;
            fclose(flstat);*/
        }

        delete[] Textures[texid].name;
        Textures[texid].name = nullptr;
    }
    if (Textures[texid].loaded == false)
    {
        return false;
    }

    // Release returns the remaining reference count (a texture still bound to a stage keeps one), not an HRESULT.
    if (Textures[texid].d3dtex)
        Textures[texid].d3dtex->Release();
    Textures[texid].d3dtex = nullptr;
    dwTotalSize -= Textures[texid].dwSize;

    return true;
}

//################################################################################
bool Renderer::SetCamera(const glm::vec3 &pos, const glm::vec3 &ang, float fov)
{
    if (!SetCamera(pos, ang))
        return false;
    if (!SetPerspective(fov, aspectRatio))
        return false;

    ProcessScriptPosAng(pos, ang);
    return true;
}

bool Renderer::SetCamera(const glm::vec3 &pos, const glm::vec3 &ang)
{
    glm::mat4 mtx;
    const glm::vec3 vOldWordRelationPos = vWordRelationPos;
    /*if (!pos || !ang)
    {
      if (!pos && !ang) return true;
      //if(CHECKD3DERR(GetTransform(D3DTS_VIEW, mtx))==true) return false;
      d3d9->GetTransform(D3DTS_VIEW, mtx);
      if (pos)
      {
        //mtx.SetInversePosition(pos->x, pos->y, pos->z);
        Pos = *pos;
        vWordRelationPos = -Pos;
      }
      else
      {
        //glm::vec3 p;
        //mtx.MulToInvNorm(mtx.Pos(), p);
        mtx.BuildMatrix(*ang);
        mtx.Transposition3X3();
        //mtx.SetInversePosition(p.x, p.y, p.z);
        Ang = *ang;
      }
    }
    else*/
    {
        mtx = storm::math::eulerMatrix(ang);
        storm::math::transpose3x3(mtx);
        // mtx.SetInversePosition(pos->x, pos->y, pos->z);
        Pos = pos;
        Ang = ang;
        vWordRelationPos = -Pos;
    }
    storm::render::SetEffectsWorldOrigin({-vWordRelationPos.x, -vWordRelationPos.y, -vWordRelationPos.z});
    // if(CHECKD3DERR(SetTransform(D3DTS_VIEW, mtx))==true)    return false;

    d3d9->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(mtx));
    vViewRelationPos = -(storm::math::transformPoint(mtx, vWordRelationPos));
    d3d9->GetTransform(D3DTS_WORLD, storm::math::d3dMatrix(mtx));
    storm::math::pos(mtx) += vWordRelationPos - vOldWordRelationPos;
    d3d9->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(mtx));

    FindPlanes(d3d9);
    ProcessScriptPosAng(pos, ang);
    return true;
}

bool Renderer::SetCamera(glm::vec3 lookFrom, glm::vec3 lookTo, glm::vec3 up)
{
    glm::mat4 mtx;
    if (!storm::math::viewMatrix(mtx, lookFrom, lookTo, up))
        return false;
    // if(CHECKD3DERR(SetTransform(D3DTS_VIEW, mtx))==true)    return false;
    SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(mtx));
    Pos = lookFrom;

    // calculate the angle of the camera
    // Ang = 0.0f;
    const glm::vec3 vNorm = storm::math::normalizeOrZero(lookTo - lookFrom);
    Ang.y = atan2f(vNorm.x, vNorm.z);
    Ang.x = atan2f(-vNorm.y, sqrtf(vNorm.x * vNorm.x + vNorm.z * vNorm.z));
    Ang.z = 0.f;

    FindPlanes(d3d9);
    ProcessScriptPosAng(lookFrom, Ang);
    return true;
}

void Renderer::ProcessScriptPosAng(const glm::vec3 &vPos, const glm::vec3 &vAng)
{
    core.Event("CameraPosAng", "ffffff", vPos.x, vPos.y, vPos.z, vAng.x, vAng.y, vAng.z);
}

void Renderer::GetNearFarPlane(float &fNear, float &fFar)
{
    fNear = fNearClipPlane;
    fFar = fFarClipPlane;
}

void Renderer::SetNearFarPlane(float fNear, float fFar)
{
    fNearClipPlane = fNear;
    fFarClipPlane = fFar;

    SetPerspective(Fov, aspectRatio);
}

bool Renderer::SetPerspective(float perspective, float fAspectRatio)
{
    perspective *= FovMultiplier;

    const float near_plane = fNearClipPlane; // Distance to near clipping
    const float far_plane = fFarClipPlane;   // Distance to far clipping
    const float fov_horiz = perspective;     // Horizontal field of view  angle, in radians
    if (fAspectRatio < 0)
    {
        fAspectRatio = static_cast<float>(screen_size.y) / screen_size.x;
    }
    aspectRatio = fAspectRatio;
    const float fov_vert = 2.f * atanf(tanf(perspective / 2.f) * fAspectRatio); // Vertical field of view  angle, in radians

    const float w = 1.0f / tanf(fov_horiz * 0.5f);
    const float h = 1.0f / tanf(fov_vert * 0.5f);
    const float Q = far_plane / (far_plane - near_plane);

    D3DMATRIX mtx{};

    mtx._11 = w;
    mtx._22 = h;
    mtx._33 = Q;
    mtx._43 = -Q * near_plane;
    mtx._34 = 1.0f;

    if (CHECKD3DERR(d3d9->SetTransform(D3DTS_PROJECTION, &mtx)) == true)
        return false;
    Fov = perspective;
    FindPlanes(d3d9);
    return true;
}

//################################################################################
bool Renderer::SetCurrentMatrix(D3DMATRIX *mtx)
{
    // return CHECKD3DERR(SetTransform(D3DTS_WORLD, mtx)) == D3D_OK;
    SetTransform(D3DTS_WORLD, mtx);
    return true;
}

bool Renderer::SetMaterial(D3DMATERIAL9 &m)
{
    if (CHECKD3DERR(d3d9->SetMaterial(&m)))
        return false;
    return true;
}

bool Renderer::SetLight(uint32_t dwIndex, const D3DLIGHT9 *pLight)
{
    // Set the property information for the first light.
    D3DLIGHT9 tmpLight = *pLight;
    tmpLight.Position.x += vWordRelationPos.x;
    tmpLight.Position.y += vWordRelationPos.y;
    tmpLight.Position.z += vWordRelationPos.z;
    if (CHECKD3DERR(d3d9->SetLight(dwIndex, &tmpLight)))
        return false;
    return true;
}

bool Renderer::LightEnable(uint32_t dwIndex, bool bOn)
{
    if (CHECKD3DERR(d3d9->LightEnable(dwIndex, bOn)))
        return false;
    return true;
}

bool Renderer::GetLightEnable(uint32_t dwIndex, BOOL *pEnable)
{
    if (CHECKD3DERR(d3d9->GetLightEnable(dwIndex, pEnable)))
        return false;
    return true;
}

bool Renderer::GetLight(uint32_t dwIndex, D3DLIGHT9 *pLight)
{
    if (CHECKD3DERR(d3d9->GetLight(dwIndex, pLight)))
        return false;

    pLight->Position.x -= vWordRelationPos.x;
    pLight->Position.y -= vWordRelationPos.y;
    pLight->Position.z -= vWordRelationPos.z;
    return true;
}

//################################################################################

int32_t Renderer::CreateVertexBuffer(int32_t type, size_t size, uint32_t dwUsage, uint32_t dwPool)
{
    if (size <= 0)
        return -1; // fix

    int32_t b;
    for (b = 0; b < MAX_BUFFERS; b++)
        if (VertexBuffers[b].buff == nullptr)
            break;

    if (b == MAX_BUFFERS)
        return -1;

    if (CHECKD3DERR(d3d9->CreateVertexBuffer(size, dwUsage, type, static_cast<D3DPOOL>(dwPool),
                                             &VertexBuffers[b].buff, NULL)))
        return -1;

    VertexBuffers[b].type = type;
    VertexBuffers[b].size = size;
    VertexBuffers[b].dwUsage = dwUsage;

    return b;
}

int32_t Renderer::GetVertexBufferFVF(int32_t id)
{
    if (id < 0 || id >= MAX_BUFFERS)
        return 0;
    return VertexBuffers[id].type;
}

//################################################################################
int32_t Renderer::CreateIndexBuffer(size_t size, uint32_t dwUsage, D3DFORMAT format)
{
    int32_t b;
    for (b = 0; b < MAX_BUFFERS; b++)
        if (IndexBuffers[b].buff == nullptr)
            break;

    if (b == MAX_BUFFERS)
        return -1;

    if (CHECKD3DERR(d3d9->CreateIndexBuffer(size, dwUsage, format, D3DPOOL_DEFAULT, &IndexBuffers[b].buff, NULL)))
        return -1;

    IndexBuffers[b].size = size;
    IndexBuffers[b].dwUsage = dwUsage;

    return b;
}

//################################################################################
void Renderer::DrawBuffer(int32_t vbuff, int32_t stride, int32_t ibuff, int32_t minv, size_t numv, size_t startidx, size_t numtrg,
                           const char *cBlockName)
{
    bool bDraw = true;

    if (vbuff >= 0)
        if (CHECKD3DERR(SetFVF(VertexBuffers[vbuff].type)))
            return;
    // else VertexBuffer already set

    if (CHECKD3DERR(d3d9->SetIndices(IndexBuffers[ibuff].buff)))
        return;

    if (vbuff >= 0)
        if (CHECKD3DERR(d3d9->SetStreamSource(0, VertexBuffers[vbuff].buff, 0, stride)))
            return;
    // else VertexBuffer already set

    if (cBlockName && cBlockName[0])
        bDraw = TechniqueExecuteStart(cBlockName);

    if (bDraw)
        do
        {
            if (!storm::android::NativeInterfaceDrawSuppressed())
            {
                dwNumDrawPrimitive++;
                CHECKD3DERR(d3d9->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, minv, 0, numv, startidx, numtrg));
            }
        } while (cBlockName && cBlockName[0] && TechniqueExecuteNext());
}

void Renderer::DrawIndexedPrimitiveNoVShader(D3DPRIMITIVETYPE dwPrimitiveType, int32_t iVBuff, int32_t iStride, int32_t iIBuff,
                                              int32_t iMinV, int32_t iNumV, int32_t iStartIdx, int32_t iNumTrg,
                                              const char *cBlockName)
{
    bool bDraw = true;

    if (CHECKD3DERR(d3d9->SetIndices(IndexBuffers[iIBuff].buff)) == true)
        return;

    if (iVBuff >= 0)
    {
        if (CHECKD3DERR(d3d9->SetStreamSource(0, VertexBuffers[iVBuff].buff, 0, iStride)) == true)
            return;
    }

    if (cBlockName && cBlockName[0])
        bDraw = TechniqueExecuteStart(cBlockName);
    if (bDraw)
        do
        {
            if (!storm::android::NativeInterfaceDrawSuppressed())
            {
                dwNumDrawPrimitive++;
                CHECKD3DERR(d3d9->DrawIndexedPrimitive(dwPrimitiveType, iMinV, 0, iNumV, iStartIdx, iNumTrg));
            }
        } while (cBlockName && TechniqueExecuteNext());
}

void Renderer::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE dwPrimitiveType, uint32_t dwMinIndex, uint32_t dwNumVertices,
                                       uint32_t dwPrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
                                       const void *pVertexData, uint32_t dwVertexStride, const char *cBlockName)
{
    bool bDraw = true;
    if (cBlockName && cBlockName[0])
        bDraw = TechniqueExecuteStart(cBlockName);
    if (bDraw)
        do
        {
            if (!storm::android::NativeInterfaceDrawSuppressed())
            {
                dwNumDrawPrimitive++;
                CHECKD3DERR(d3d9->DrawIndexedPrimitiveUP(dwPrimitiveType, dwMinIndex, dwNumVertices, dwPrimitiveCount,
                                                         pIndexData, IndexDataFormat, pVertexData, dwVertexStride));
            }
        } while (cBlockName && TechniqueExecuteNext());
}

void Renderer::DrawPrimitiveUP(D3DPRIMITIVETYPE dwPrimitiveType, uint32_t dwVertexBufferFormat, uint32_t dwNumPT,
                                const void *pVerts, uint32_t dwStride, const char *cBlockName)
{
    bool bDraw = true;

    if (CHECKD3DERR(SetFVF(dwVertexBufferFormat)) == true)
        return;

    if (cBlockName && cBlockName[0])
        bDraw = TechniqueExecuteStart(cBlockName);
    if (bDraw)
        do
        {
            if (!storm::android::NativeInterfaceDrawSuppressed())
            {
                dwNumDrawPrimitive++;
                CHECKD3DERR(d3d9->DrawPrimitiveUP(dwPrimitiveType, dwNumPT, pVerts, dwStride));
            }
        } while (cBlockName && TechniqueExecuteNext());
}

void Renderer::DrawPrimitive(D3DPRIMITIVETYPE dwPrimitiveType, int32_t iVBuff, int32_t iStride, int32_t iStartV, int32_t iNumPT,
                              const char *cBlockName)
{
    bool bDraw = true;

    if (CHECKD3DERR(SetFVF(VertexBuffers[iVBuff].type)) == true)
        return;

    if (CHECKD3DERR(d3d9->SetStreamSource(0, VertexBuffers[iVBuff].buff, 0, iStride)) == true)
        return;

    if (cBlockName && cBlockName[0])
        bDraw = TechniqueExecuteStart(cBlockName);
    if (bDraw)
        do
        {
            if (!storm::android::NativeInterfaceDrawSuppressed())
            {
                dwNumDrawPrimitive++;
                CHECKD3DERR(d3d9->DrawPrimitive(dwPrimitiveType, iStartV, iNumPT));
            }
        } while (cBlockName && TechniqueExecuteNext());
}

//################################################################################
void Renderer::RenderAnimation(int32_t ib, void *src, int32_t numVrts, int32_t minv, int32_t numv, int32_t startidx, int32_t numtrg,
                                bool isUpdateVB)
{
    if (numVrts <= 0 || !src || ib < 0)
        return;
    const uint32_t type = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;
    const int32_t size = numVrts * sizeof(FVF_VERTEX);
    if (isUpdateVB || numVrts > numAniVerteces || !aniVBuffer)
    {
        // Create vertex buffer
        if (numVrts > numAniVerteces)
        {
            if (aniVBuffer)
                aniVBuffer->Release();
            aniVBuffer = nullptr;
            numAniVerteces = 0;
            if (CHECKD3DERR(d3d9->CreateVertexBuffer(size, D3DUSAGE_WRITEONLY, type, D3DPOOL_MANAGED, &aniVBuffer,
                                                     NULL)) == true)
                return;
            numAniVerteces = numVrts;
        }
        // Copy verteces
        uint8_t *ptr;
        RDTSC_B(_rdtsc);
        if (CHECKD3DERR(aniVBuffer->Lock(0, size, (void **)&ptr, 0)) == true)
            return;
        dwNumLV++;
        RDTSC_E(_rdtsc);
        memcpy(ptr, src, size);
        CHECKD3DERR(aniVBuffer->Unlock());
    }
    // Render
    if (CHECKD3DERR(SetFVF(type)) == true)
        return;

    if (CHECKD3DERR(d3d9->SetIndices(IndexBuffers[ib].buff)) == true)
        return;

    if (CHECKD3DERR(d3d9->SetStreamSource(0, aniVBuffer, 0, sizeof(FVF_VERTEX))) == true)
        return;

    if (!storm::android::NativeInterfaceDrawSuppressed())
    {
        CHECKD3DERR(d3d9->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, minv, 0, numv, startidx, numtrg));
        dwNumDrawPrimitive++;
    }
}

//################################################################################
// Buffer handles (the contract is in renderer.h): indices into VertexBuffers / IndexBuffers, -1 is "no
// buffer". Owners pass -1 to Release unchecked (BIImageMaterial::RemakeBuffers, ShipTracks); indexing the
// table with it read the member in front of the table as a buffer pointer (keptPixels_'s
// max_load_factor, 1.0f, crashed as address 0x3f800000).
bool Renderer::LiveBuffer(int32_t id, bool index, const char *call) const
{
    if (id >= 0 && id < MAX_BUFFERS && (index ? IndexBuffers[id].buff != nullptr : VertexBuffers[id].buff != nullptr))
        return true;
    spdlog::error("{}: {} buffer handle {} {}", call, index ? "index" : "vertex", id,
                  id >= 0 && id < MAX_BUFFERS ? "has no buffer" : "is out of range");
    return false;
}

void *Renderer::LockVertexBuffer(int32_t id, uint32_t dwFlags)
{
    if (!LiveBuffer(id, false, "LockVertexBuffer"))
        return nullptr;
    uint8_t *ptr;
    VertexBuffers[id].dwNumLocks++;
    if (CHECKD3DERR(VertexBuffers[id].buff->Lock(0, VertexBuffers[id].size, (void **)&ptr, dwFlags)))
        return nullptr;

    dwNumLV++;
    return ptr;
}

//################################################################################
void Renderer::UnLockVertexBuffer(int32_t id)
{
    if (!LiveBuffer(id, false, "UnLockVertexBuffer"))
        return;
    VertexBuffers[id].dwNumLocks--;
    CHECKD3DERR(VertexBuffers[id].buff->Unlock());
}

int32_t Renderer::GetVertexBufferSize(int32_t id)
{
    if (!LiveBuffer(id, false, "GetVertexBufferSize"))
        return 0;
    return VertexBuffers[id].size;
}

void *Renderer::LockIndexBuffer(int32_t id, uint32_t dwFlags)
{
    if (!LiveBuffer(id, true, "LockIndexBuffer"))
        return nullptr;
    uint8_t *ptr = nullptr;
    IndexBuffers[id].dwNumLocks++;
    if (CHECKD3DERR(IndexBuffers[id].buff->Lock(0, IndexBuffers[id].size, (void **)&ptr, dwFlags)))
        return nullptr;

    dwNumLI++;
    return ptr;
}

void Renderer::UnLockIndexBuffer(int32_t id)
{
    if (!LiveBuffer(id, true, "UnLockIndexBuffer"))
        return;
    IndexBuffers[id].dwNumLocks--;
    CHECKD3DERR(IndexBuffers[id].buff->Unlock());
}

//################################################################################
void Renderer::ReleaseVertexBuffer(int32_t id)
{
    if (id == -1)
        return; // no buffer
    if (!LiveBuffer(id, false, "ReleaseVertexBuffer"))
        return;
    VertexBuffers[id].buff->Release(); // returns a reference count, not an HRESULT
    VertexBuffers[id].buff = nullptr;
    VertexBuffers[id].dwNumLocks = 0;
}

//################################################################################
void Renderer::ReleaseIndexBuffer(int32_t id)
{
    if (id == -1)
        return; // no buffer
    if (!LiveBuffer(id, true, "ReleaseIndexBuffer"))
        return;
    IndexBuffers[id].buff->Release(); // returns a reference count, not an HRESULT
    IndexBuffers[id].buff = nullptr;
    IndexBuffers[id].dwNumLocks = 0;
}

void Renderer::SetTransform(int32_t type, D3DMATRIX *mtx)
{
    glm::mat4 m = *(glm::mat4 *)mtx;
    if (type == D3DTS_VIEW)
    {
        vViewRelationPos.x = -mtx->_41;
        vViewRelationPos.y = -mtx->_42;
        vViewRelationPos.z = -mtx->_43;

        glm::vec3 vDeltaWorld = vWordRelationPos;
        vWordRelationPos = storm::math::inverseTransformNormal(m, -vViewRelationPos);
        storm::render::SetEffectsWorldOrigin({-vWordRelationPos.x, -vWordRelationPos.y, -vWordRelationPos.z});
        vDeltaWorld -= vWordRelationPos;
        glm::mat4 mw;
        d3d9->GetTransform(D3DTS_WORLD, (D3DMATRIX *)&mw);
        storm::math::pos(mw) -= vDeltaWorld;
        d3d9->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(mw));

        storm::math::pos(m) += vViewRelationPos;
    }
    else if (type == D3DTS_WORLD)
    {
        storm::math::pos(m) += vWordRelationPos;
    }

    CHECKD3DERR(d3d9->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(type), (D3DMATRIX *)&m));
}

void Renderer::GetTransform(int32_t type, D3DMATRIX *mtx)
{
    CHECKD3DERR(d3d9->GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(type), mtx));

    if (type == D3DTS_VIEW)
    {
        mtx->_41 -= vViewRelationPos.x;
        mtx->_42 -= vViewRelationPos.y;
        mtx->_43 -= vViewRelationPos.z;
    }
    else if (type == D3DTS_WORLD)
    {
        mtx->_41 -= vWordRelationPos.x;
        mtx->_42 -= vWordRelationPos.y;
        mtx->_43 -= vWordRelationPos.z;
    }
}

bool Renderer::CreateState(ENTITY_STATE_GEN *state_gen)
{
    // state_gen->SetState("vm",sizeof(screen_size),screen_size,sizeof(bool),&window);
    return true;
}

bool Renderer::LoadState(ENTITY_STATE *state)
{
    // GUARD(Renderer::Init)
    // for(int32_t t=0; t<MAX_STEXTURES; t++)    Textures[t].d3dtex = NULL;
    // d3d = NULL;
    // d3d9 = NULL;
    // state->Struct(sizeof(screen_size),(char *)&screen_size);
    // state->MemoryBlock(sizeof(bool),(char *)&window);
    // InitDevice(window,core.GetAppHWND(),screen_size.x,screen_size.y);
    // UNGUARD
    return true;
}

void Renderer::LostRender()
{
    if (resourcesReleased)
    {
        return;
    }

    InvokeEntitiesLostRender();

    Release(pOriginalScreenSurface);
    Release(pOriginalDepthSurface);
    Release(rectsVBuffer);
    for (int32_t b = 0; b < MAX_BUFFERS; b++)
    {
        if (VertexBuffers[b].buff)
        {
            if (VertexBuffers[b].buff->Release() > 0)
                psnip_trap();
        }
        if (IndexBuffers[b].buff)
        {
            if (IndexBuffers[b].buff->Release() > 0)
                psnip_trap();
        }
    }

    resourcesReleased = true;
}

void Renderer::RestoreRender()
{
    d3d9->GetRenderTarget(0, &pOriginalScreenSurface);
    d3d9->GetDepthStencilSurface(&pOriginalDepthSurface);
    ClearSurface(pOriginalScreenSurface);
    d3d9->CreateVertexBuffer(rectsVBuffer_SizeInRects * 6 * sizeof(RECT_VERTEX), D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,
                             RS_RECT_VERTEX_FORMAT, D3DPOOL_DEFAULT, &rectsVBuffer, nullptr);
    for (int32_t b = 0; b < MAX_BUFFERS; b++)
    {
        if (VertexBuffers[b].buff)
        {
            CHECKD3DERR(d3d9->CreateVertexBuffer(VertexBuffers[b].size, VertexBuffers[b].dwUsage, VertexBuffers[b].type,
                                                 D3DPOOL_DEFAULT, &VertexBuffers[b].buff, NULL));
        }
        if (IndexBuffers[b].buff)
        {
            CHECKD3DERR(d3d9->CreateIndexBuffer(IndexBuffers[b].size, IndexBuffers[b].dwUsage, D3DFMT_INDEX16,
                                                D3DPOOL_DEFAULT, &IndexBuffers[b].buff, NULL));
        }
    }
    int32_t num_stages;
    num_stages = 8;
    for (int32_t s = 0; s < num_stages; s++)
    {
        // texture operation
        SetTextureStageState(s, D3DTSS_COLORARG1, D3DTA_CURRENT);
        SetTextureStageState(s, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        SetTextureStageState(s, D3DTSS_COLOROP, D3DTOP_DISABLE);

        // texture coord
        SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, s);

        // texture filtering
        SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    }
    // set base texture and diffuse+specular lighting
    SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    D3DLIGHT9 l{};
    l.Type = D3DLIGHT_POINT;
    l.Range = 100.0f;
    l.Attenuation0 = 1.0f;
    for (int i = 0; i < 8; i++)
    {
        CHECKD3DERR(d3d9->SetLight(i, &l));
        CHECKD3DERR(d3d9->LightEnable(i, false));
    }
    SetCommonStates();

    pTechnique = std::make_unique<CTechnique>(this);
    pTechnique->DecodeFiles();

    InvokeEntitiesRestoreRender();

    resourcesReleased = false;
}

#ifdef __ANDROID__
// The view changed size (split screen, foldables, `wm size`, a display size setting): the game
// target follows it at the same render scale as at start (a lower render resolution chosen in
// the graphics settings stays lower), instead of being letterboxed in the new surface. Between
// frames (RunStart). The device keeps every resource and state; only the back buffer and its
// frame buffers are made again, and the renderer takes the new back buffer.
void Renderer::ApplyViewResize()
{
    const storm::android::RenderExtent view = pendingViewResize_;
    pendingViewResize_ = {};
    const storm::android::RenderExtent wanted = storm::android::boundedRenderExtent(
        {std::clamp(static_cast<int>(std::lround(view.width * renderScale_[0])), 320, 8192),
         std::clamp(static_cast<int>(std::lround(view.height * renderScale_[1])), 320, 8192)});
    if (wanted.width <= 0 || (wanted.width == screen_size.x && wanted.height == screen_size.y))
        return;
    Release(pOriginalScreenSurface);
    Release(pOriginalDepthSurface);
    d3dpp.BackBufferWidth = wanted.width;
    d3dpp.BackBufferHeight = wanted.height;
    if (CHECKD3DERR(d3d9->ResizeBackBuffer(wanted.width, wanted.height)))
        throw std::runtime_error("Cannot resize the game picture to " + std::to_string(wanted.width) + "x" + std::to_string(wanted.height));
    d3d9->GetRenderTarget(0, &pOriginalScreenSurface);
    d3d9->GetDepthStencilSurface(&pOriginalDepthSurface);
    screen_size.x = wanted.width;
    screen_size.y = wanted.height;
    OriginalViewPort.Width = wanted.width; // every frame starts with it (BeginScene)
    OriginalViewPort.Height = wanted.height;
    m_fHeightDeformator = (float)(screen_size.y * 4.0f) / (float)(screen_size.x * 3.0f);
    if (auto *render = static_cast<VDATA *>(core.GetScriptVariable("Render")); render && render->GetAClass())
    {
        render->GetAClass()->SetAttributeUseDword("screen_x", screen_size.x);
        render->GetAClass()->SetAttributeUseDword("screen_y", screen_size.y);
    }
    core.Trace("AndroidRenderResolution: view resized to %dx%d, game picture %dx%d", view.width, view.height, screen_size.x, screen_size.y);
}
#endif

bool Renderer::ResetDevice()
{
    LostRender();

    if (CHECKD3DERR(d3d9->Reset(&d3dpp)))
        return false;

    RestoreRender();

    return true;
}

void Renderer::RunStart()
{
#ifdef __ANDROID__
    if (pendingViewResize_.width > 0)
        ApplyViewResize();
    ReconfigureGraphicsEffects(); // the settings screen stored new preferences
    AutoPresetFrame(); // the automatic graphics preset measures on the title screen
    // The pause menu covers a paused game: its last 3D frame is presented again instead of drawing the scene every
    // frame (perf audit F11). The hold begins once the renderer kept a full frame and ends with the menu; a settings
    // or size change while it lasts is drawn in one full frame, which is kept in its turn.
    storm::render::HoldGamePicture(storm::android::mobileScreen == "game_menu");
    storm::core::HoldSceneRealize(storm::render::GamePictureHeld());
#endif
    // The GPU self-test switched an effect stage off on this device: tell the player once (also logged).
    if (const std::string notice = storm::render::TakeEffectsNotice(); !notice.empty())
    {
        spdlog::warn("graphics effects: {}", notice);
#ifdef __ANDROID__
        storm::android::publishGameMessage(notice);
#endif
    }
    auto *pScriptRender = static_cast<VDATA *>(core.GetScriptVariable("Render"));
    ATTRIBUTES *pARender = pScriptRender->GetAClass();

    dwBackColor = pARender->GetAttributeAsDword("BackColor", 0);
    // Render.SeaEffect (locations_loader.c, underwater locations): the game's screen wobble, drawn by the effects
    // post chain on every preset. The phase advances at SeaEffectSpeed radians per second, as in the D3D9 engine.
    storm::render::EffectsWobble wobble;
    wobble.on = pARender->GetAttributeAsDword("SeaEffect", 0) != 0;
    if (wobble.on)
    {
        const float speed = pARender->GetAttributeAsFloat("SeaEffectSpeed", 0.0f);
        fWobblePhase = std::fmod(fWobblePhase + static_cast<float>(core.GetRDeltaTime()) * 0.001f * speed, 2.0f * PI);
        wobble.amplitude = pARender->GetAttributeAsFloat("SeaEffectSize", 0.0f);
        wobble.phase = fWobblePhase;
    }
    storm::render::SetEffectsWobble(wobble);
    SetScreenAsRenderTarget();

    ClearFrame(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | ((stencil_format == D3DFMT_D24S8) ? D3DCLEAR_STENCIL : 0));

    dwNumDrawPrimitive = 0;
    dwNumLV = 0;
    dwNumLI = 0;
    BeginScene();

    //------------------------------------------
    bInsideScene = true;

    // execute default technique for set default render/texture states
    // if (TechniqueExecuteStart("default")) do{} while (TechniqueExecuteNext());

    // boal del_cheat
    if (core.Controls->GetDebugAsyncKeyState(VK_SHIFT) < 0 && core.Controls->GetDebugAsyncKeyState(VK_F11) < 0)
    {
        InvokeEntitiesLostRender();
        pTechnique = std::make_unique<CTechnique>(this);
        pTechnique->DecodeFiles();
        InvokeEntitiesRestoreRender();
    }

    SetRenderState(D3DRS_FILLMODE, (core.Controls->GetDebugAsyncKeyState('F') < 0) ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
    // SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID); eddy

    PlayToTexture();
}

void Renderer::RunEnd()
{
    bInsideScene = false;
    DrawFrameStats();
    if (progressTexture >= 0)
    {
        const int32_t oldCnt = progressSafeCounter;
        ProgressView();
        progressSafeCounter = oldCnt;
        if (progressSafeCounter >= 50)
        {
            EndProgressView();
        }
        else
            progressSafeCounter++;
    }
}

char Buff_4k[4096];

int32_t Renderer::Print(int32_t x, int32_t y, const char *format, ...)
{
    // GUARD(Renderer::Print)
    if (idFontCurrent < 0 || idFontCurrent >= nFontQuantity)
        return 0;
    if (FontList[idFontCurrent].font == nullptr || FontList[idFontCurrent].ref == 0)
        return 0;

    va_list args;
    va_start(args, format);
    vsnprintf(Buff_4k, sizeof(Buff_4k), format, args);
    va_end(args);

    return FontList[idFontCurrent].font->Print(x, y, Buff_4k);
    // UNGUARD
}

int32_t Renderer::Print(int32_t nFontNum, uint32_t color, int32_t x, int32_t y, const char *format, ...)
{
    // GUARD(Renderer::Print)
    if (nFontNum < 0 || nFontNum >= nFontQuantity)
        return 0;
    if (FontList[nFontNum].font == nullptr || FontList[nFontNum].ref == 0)
        return 0;

    va_list args;
    va_start(args, format);
    vsnprintf(Buff_4k, sizeof(Buff_4k), format, args);
    va_end(args);

    const int32_t retVal = FontList[nFontNum].font->Print(x, y, Buff_4k, {.color = color});
    return retVal;
    // UNGUARD
}

int32_t Renderer::StringWidth(const char *string, int32_t nFontNum, float fScale, int32_t scrWidth)
{
    if (string == nullptr)
    {
        return 0;
    }
    return StringWidth(std::string_view(string), nFontNum, fScale, scrWidth);
}

int32_t Renderer::StringWidth(const std::string_view &string, int32_t nFontNum, float fScale, int32_t scrWidth)
{
    if (nFontNum < 0 || nFontNum >= nFontQuantity)
        return 0;
    FONT *pFont = FontList[nFontNum].font;
    if (FontList[nFontNum].ref == 0 || pFont == nullptr)
        return 0;


    const int32_t xs = screen_size.x;
    if (scrWidth == 0)
        scrWidth = xs;
    if (xs != scrWidth)
        fScale *= static_cast<float>(xs) / scrWidth;
    const int32_t retVal = pFont->GetStringWidth(string, fScale);
    return retVal;
}

int32_t Renderer::CharWidth(utf8::u8_char ch, int32_t nFontNum, float fScale, int32_t scrWidth)
{
    std::string str(ch.b, ch.l);
    return StringWidth(str.c_str(), nFontNum, fScale, scrWidth);
}

int32_t Renderer::CharHeight(int32_t fontID)
{
    if (fontID < 0 || fontID >= nFontQuantity)
        return 0;
    if (FontList[fontID].ref == 0 || FontList[fontID].font == nullptr)
        return 0;

    return FontList[fontID].font->GetHeight();
}

int32_t Renderer::ExtPrint(int32_t nFontNum, uint32_t foreColor, uint32_t backColor, int wAlign, bool bShadow, float fScale,
                         int32_t scrWidth, int32_t scrHeight, int32_t x, int32_t y, const char *format, ...)
{
    // GUARD(Renderer::ExtPrint)

    if (nFontNum < 0 || nFontNum >= nFontQuantity)
        return 0;
    FONT *pFont = FontList[nFontNum].font;
    if (FontList[nFontNum].ref == 0 || pFont == nullptr)
        return 0;

    va_list args;
    va_start(args, format);
    vsnprintf(Buff_4k, sizeof(Buff_4k), format, args);
    va_end(args);

    storm::shim::Surface *pRenderTarget;
    GetRenderTarget(&pRenderTarget);
    D3DSURFACE_DESC dscrSurface;
    pRenderTarget->GetDesc(&dscrSurface);
    const int32_t xs = dscrSurface.Width;
    const int32_t ys = dscrSurface.Height;
    pRenderTarget->Release();
    if (scrWidth == 0)
        scrWidth = xs;
    if (scrHeight == 0)
        scrHeight = ys;
    if (xs != scrWidth)
    {
        const float fHorzScale = static_cast<float>(xs) / scrWidth;
        x = static_cast<int32_t>(x * fHorzScale);
        fScale *= fHorzScale;
    }
    if (ys != scrHeight)
        y = static_cast<int32_t>(y * (static_cast<float>(ys) / scrHeight));

    switch (wAlign)
    {
    case PR_ALIGN_CENTER:
        x -= static_cast<int32_t>((pFont->GetStringWidth(Buff_4k) + 1L) / 2L * fScale);
        break;
    case PR_ALIGN_RIGHT:
        x -= static_cast<int32_t>(pFont->GetStringWidth(Buff_4k) * fScale);
        break;
    }

    const int32_t retVal = pFont->Print(x, y, Buff_4k,
                                        {
                                            .scale = fScale,
                                            .color = foreColor,
                                            .shadow = bShadow,
                                        });
    return retVal;
    // UNGUARD
}

int32_t Renderer::LoadFont(const char *fontName)
{
    if (fontName == nullptr)
        return -1L;
    char sDup[256];
    if (strlen(fontName) < sizeof(sDup) - 1)
        strcpy_s(sDup, fontName);
    else
    {
        strncpy_s(sDup, fontName, sizeof(sDup) - 1);
        sDup[sizeof(sDup) - 1] = 0;
    }

    std::ranges::for_each(sDup, [](char &c) { c = std::toupper(c); });
    fontName = sDup;

    const uint32_t hashVal = MakeHashValue(fontName);

    int32_t i;
    for (i = 0; i < nFontQuantity; i++)
        if (FontList[i].hash == hashVal && storm::iEquals(FontList[i].name, fontName))
        {
            if (FontList[i].ref > 0)
                FontList[i].ref++;
            else
            {
                FontList[i].ref = 1;
                FontList[i].font->RepeatInit();
            }
            return i;
        }
    if (nFontQuantity < MAX_FONTS)
    {
        if ((FontList[i].font = new FONT(*this, *d3d9)) == nullptr)
            throw std::runtime_error("allocate memory error");
        if (!FontList[i].font->Init(fontName, fontIniFileName))
        {
            delete FontList[i].font;
            core.Trace("Can't init font %s", fontName);
            return -1L;
        }
        FontList[i].hash = hashVal;
        FontList[i].ref = 1;
        const auto len = strlen(fontName) + 1;
        if ((FontList[i].name = new char[len]) == nullptr)
            throw std::runtime_error("allocate memory error");
        strcpy_s(FontList[i].name, len, fontName);
        nFontQuantity++;
    }
    else
        throw std::runtime_error("maximal font quantity exceeded");
    return i;
}

bool Renderer::UnloadFont(const char *fontName)
{
    if (fontName == nullptr)
        return false;
    char sDup[256];
    if (strlen(fontName) < sizeof(sDup) - 1)
        strcpy_s(sDup, fontName);
    else
    {
        strncpy_s(sDup, fontName, sizeof(sDup) - 1);
        sDup[sizeof(sDup) - 1] = 0;
    }
    std::ranges::for_each(sDup, [](char &c) { c = std::toupper(c); });
    fontName = sDup;
    const uint32_t hashVal = MakeHashValue(fontName);

    for (int i = 0; i < nFontQuantity; i++)
        if (FontList[i].hash == hashVal && storm::iEquals(FontList[i].name, fontName))
            return UnloadFont(i);
    core.Trace("Font name \"%s\" is not containing", fontName);
    return false;
}

bool Renderer::UnloadFont(int32_t fontID)
{
    if (fontID < 0 || fontID >= nFontQuantity)
        return false;

    if (FontList[fontID].ref > 0)
    {
        FontList[fontID].ref--;
        if (FontList[fontID].ref == 0)
        {
            FontList[fontID].font->TempUnload();
            // if (idFontCurrent == fontID)
            idFontCurrent = FONT_DEFAULT;
        }
        else
            return false;
    }

    return true;
}

bool Renderer::IncRefCounter(int32_t fontID)
{
    if (fontID < 0 || fontID >= nFontQuantity)
        return false;

    FontList[fontID].ref++;

    return true;
}

bool Renderer::SetCurFont(const char *fontName)
{
    if (fontName == nullptr)
        return false;
    char sDup[256];
    if (strlen(fontName) < sizeof(sDup) - 1)
        strcpy_s(sDup, fontName);
    else
    {
        strncpy_s(sDup, fontName, sizeof(sDup) - 1);
        sDup[sizeof(sDup) - 1] = 0;
    }
    std::ranges::for_each(sDup, [](char &c) { c = std::toupper(c); });
    fontName = sDup;
    const uint32_t hashVal = MakeHashValue(fontName);

    for (int i = 0; i < nFontQuantity; i++)
        if (FontList[i].hash == hashVal)
        {
            idFontCurrent = i;
            return true;
        }
    core.Trace("Font name \"%s\" is not containing", fontName);
    return false;
}

bool Renderer::SetCurFont(int32_t fontID)
{
    if (fontID < 0 || fontID >= nFontQuantity)
        return false;
    idFontCurrent = fontID;
    return true;
}

int32_t Renderer::GetCurFont()
{
    if (idFontCurrent >= 0 && idFontCurrent < nFontQuantity)
        return idFontCurrent;
    return -1L;
}

char *Renderer::GetFontIniFileName()
{
    return fontIniFileName;
}

bool Renderer::SetFontIniFileName(const char *iniName)
{
    if (fontIniFileName != nullptr && iniName != nullptr && storm::iEquals(fontIniFileName, iniName))
        return true;

    delete[] fontIniFileName;
    if (iniName == nullptr)
    {
        fontIniFileName = nullptr;
        return false;
    }
    const auto len = strlen(iniName) + 1;
    if ((fontIniFileName = new char[len]) == nullptr)
        throw std::runtime_error("allocate memory error");
    strcpy_s(fontIniFileName, len, iniName);

    for (int n = 0; n < nFontQuantity; n++)
    {
        delete FontList[n].font;

        if ((FontList[n].font = new FONT(*this, *d3d9)) == nullptr)
            throw std::runtime_error("allocate memory error");
        FontList[n].font->Init(FontList[n].name, fontIniFileName);
        if (FontList[n].ref == 0)
            FontList[n].font->TempUnload();
    }

    return true;
}

void Renderer::SetCommonStates()
{
    SetRenderState(D3DRS_DITHERENABLE, TRUE);
    SetRenderState(D3DRS_ALPHATESTENABLE, true);
    //----------Z---------
    SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

    SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    SetRenderState(D3DRS_ALPHABLENDENABLE, true);
    SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

    SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_EXP);

    SetRenderState(D3DRS_LIGHTING, TRUE);
    SetRenderState(D3DRS_LOCALVIEWER, FALSE);
    SetRenderState(D3DRS_AMBIENT, 0x505050);
    SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);

    SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    SetRenderState(D3DRS_COLORVERTEX, TRUE);
    SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    SetRenderState(D3DRS_ALPHAREF, 0xa0);
    SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);

    // *****************************************************************88
    // texture filtering
    SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
    SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 3);

    // unchanged texture stage states - both for base and detal texture
    SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
    SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_CURRENT);
    SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

    SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
    SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);

    // general
    SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    SetRenderState(D3DRS_ALPHAREF, 0xa0);
    SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);

    // lighting effects
    SetRenderState(D3DRS_AMBIENT, 0x404040);
    SetRenderState(D3DRS_LIGHTING, FALSE); // TRUE);
    SetRenderState(D3DRS_COLORVERTEX, TRUE);
    SetRenderState(D3DRS_SPECULARENABLE, FALSE); // TRUE);

    // AA
    SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
}

HRESULT Renderer::GetViewport(D3DVIEWPORT9 *pViewport)
{
    return CHECKD3DERR(d3d9->GetViewport(pViewport));
}

HRESULT Renderer::SetViewport(const D3DVIEWPORT9 *pViewport)
{
    return CHECKD3DERR(d3d9->SetViewport(pViewport));
}

HRESULT Renderer::SetScissorRect(const RECT *rect)
{
    return CHECKD3DERR(d3d9->SetScissorRect(rect));
}

uint32_t Renderer::SetRenderState(uint32_t State, uint32_t Value)
{
    return CHECKD3DERR(d3d9->SetRenderState(static_cast<D3DRENDERSTATETYPE>(State), Value));
}

uint32_t Renderer::GetRenderState(uint32_t State, uint32_t *pValue)
{
    return CHECKD3DERR(d3d9->GetRenderState(static_cast<D3DRENDERSTATETYPE>(State), (DWORD *)pValue));
}

uint32_t Renderer::GetSamplerState(uint32_t Sampler, D3DSAMPLERSTATETYPE Type, uint32_t *pValue)
{
    return CHECKD3DERR(d3d9->GetSamplerState(Sampler, Type, (DWORD *)pValue));
}

uint32_t Renderer::SetSamplerState(uint32_t Sampler, D3DSAMPLERSTATETYPE Type, uint32_t Value)
{
    return CHECKD3DERR(d3d9->SetSamplerState(Sampler, Type, Value));
}

uint32_t Renderer::SetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t Value)
{
    return CHECKD3DERR(d3d9->SetTextureStageState(Stage, static_cast<D3DTEXTURESTAGESTATETYPE>(Type), Value));
}

uint32_t Renderer::GetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t *pValue)
{
    return CHECKD3DERR(d3d9->GetTextureStageState(Stage, static_cast<D3DTEXTURESTAGESTATETYPE>(Type), (DWORD *)pValue));
}

void Renderer::GetCamera(glm::vec3 &pos, glm::vec3 &ang, float &perspective)
{
    pos = Pos;
    ang = Ang;
    perspective = Fov;
}

using TGA_H = struct tagTGA_H
{
    uint8_t byte1; // = 0
    uint8_t byte2; // = 0
    uint8_t type;
    uint8_t byte4_9[9]; // = 0
    uint16_t width;
    uint16_t height;
    uint8_t bpp; // bit per pixel
    uint8_t attr : 4;
    uint8_t rez : 1;
    uint8_t origin : 1;
    uint8_t storage : 2;
};

// WORD Temp[1600*4];

PLANE *Renderer::GetPlanes()
{
    FindPlanes(d3d9);
    return viewplane;
}

void Renderer::FindPlanes(storm::shim::Device *d3dDevice)
{
    D3DMATRIX m;
    glm::vec3 v[4];
    GetTransform(D3DTS_PROJECTION, static_cast<D3DMATRIX *>(&m));
    // left
    v[0].x = m.m[0][0];
    v[0].y = 0.0f;
    v[0].z = 1.0f;
    // right
    v[1].x = -m.m[0][0];
    v[1].y = 0.0f;
    v[1].z = 1.0f;
    // top
    v[2].x = 0.0f;
    v[2].y = -m.m[1][1];
    v[2].z = 1.0f;
    // bottom
    v[3].x = 0.0f;
    v[3].y = m.m[1][1];
    v[3].z = 1.0f;
    v[0] = storm::math::normalizeOrZero(v[0]);
    v[1] = storm::math::normalizeOrZero(v[1]);
    v[2] = storm::math::normalizeOrZero(v[2]);
    v[3] = storm::math::normalizeOrZero(v[3]);

    GetTransform(D3DTS_VIEW, static_cast<D3DMATRIX *>(&m));
    glm::vec3 pos;

    pos.x = -m._41 * m._11 - m._42 * m._12 - m._43 * m._13;
    pos.y = -m._41 * m._21 - m._42 * m._22 - m._43 * m._23;
    pos.z = -m._41 * m._31 - m._42 * m._32 - m._43 * m._33;

    viewplane[0].Nx = v[0].x * m._11 + v[0].y * m._12 + v[0].z * m._13;
    viewplane[0].Ny = v[0].x * m._21 + v[0].y * m._22 + v[0].z * m._23;
    viewplane[0].Nz = v[0].x * m._31 + v[0].y * m._32 + v[0].z * m._33;

    viewplane[1].Nx = v[1].x * m._11 + v[1].y * m._12 + v[1].z * m._13;
    viewplane[1].Ny = v[1].x * m._21 + v[1].y * m._22 + v[1].z * m._23;
    viewplane[1].Nz = v[1].x * m._31 + v[1].y * m._32 + v[1].z * m._33;

    viewplane[2].Nx = v[2].x * m._11 + v[2].y * m._12 + v[2].z * m._13;
    viewplane[2].Ny = v[2].x * m._21 + v[2].y * m._22 + v[2].z * m._23;
    viewplane[2].Nz = v[2].x * m._31 + v[2].y * m._32 + v[2].z * m._33;

    viewplane[3].Nx = v[3].x * m._11 + v[3].y * m._12 + v[3].z * m._13;
    viewplane[3].Ny = v[3].x * m._21 + v[3].y * m._22 + v[3].z * m._23;
    viewplane[3].Nz = v[3].x * m._31 + v[3].y * m._32 + v[3].z * m._33;

    viewplane[0].D = (pos.x * viewplane[0].Nx + pos.y * viewplane[0].Ny + pos.z * viewplane[0].Nz);
    viewplane[1].D = (pos.x * viewplane[1].Nx + pos.y * viewplane[1].Ny + pos.z * viewplane[1].Nz);
    viewplane[2].D = (pos.x * viewplane[2].Nx + pos.y * viewplane[2].Ny + pos.z * viewplane[2].Nz);
    viewplane[3].D = (pos.x * viewplane[3].Nx + pos.y * viewplane[3].Ny + pos.z * viewplane[3].Nz);
}

bool Renderer::TechniqueExecuteStart(const char *cBlockName)
{
    if (!cBlockName)
        return false;
    pTechnique->SetCurrentBlock(cBlockName, 0, nullptr);
    return pTechnique->ExecutePassStart();
}

bool Renderer::TechniqueExecuteNext()
{
    return pTechnique->ExecutePassNext();
}

void Renderer::DrawRects(RS_RECT *pRSR, uint32_t dwRectsNum, const char *cBlockName, uint32_t dwSubTexturesX,
                          uint32_t dwSubTexturesY, float fScaleX, float fScaleY)
{
    if (!pRSR || dwRectsNum == 0 || !rectsVBuffer)
        return;

    bool bDraw = true;

    static glm::mat4 camMtx, oldWorldMatrix, IMatrix;
    d3d9->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(camMtx));
    d3d9->GetTransform(D3DTS_WORLD, storm::math::d3dMatrix(oldWorldMatrix));

    fScaleY *= GetHeightDeformator();

    float du, dv;
    bool bUseSubTextures = false;
    if (dwSubTexturesX > 1)
    {
        bUseSubTextures = true;
        du = 1.0f / dwSubTexturesX;
    }
    else
    {
        dwSubTexturesX = 1;
        du = 1.0f;
    }
    if (dwSubTexturesY > 1)
    {
        bUseSubTextures = true;
        dv = 1.0f / dwSubTexturesY;
    }
    else
    {
        dwSubTexturesY = 1;
        dv = 1.0f;
    }

    d3d9->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(IMatrix));
    d3d9->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(IMatrix));

    for (uint32_t cnt = 0; cnt < dwRectsNum;)
    {
        // Number of rectangles to draw at a time
        uint32_t drawCount = dwRectsNum - cnt;
        if (drawCount > rectsVBuffer_SizeInRects)
            drawCount = rectsVBuffer_SizeInRects;
        // Buffer
        RECT_VERTEX *data = nullptr;
        if (rectsVBuffer->Lock(0, drawCount * 6 * sizeof(RECT_VERTEX), (void **)&data, D3DLOCK_DISCARD) != D3D_OK)
        {
            d3d9->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(camMtx));
            d3d9->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(oldWorldMatrix));
            return;
        }
        if (!data)
        {
            // Lock succeeded: balance it even if the driver supplied no data.
            rectsVBuffer->Unlock();
            d3d9->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(camMtx));
            d3d9->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(oldWorldMatrix));
            return;
        }
        // Filling the buffer
        for (uint32_t i = 0; i < drawCount && cnt < dwRectsNum; i++)
        {
            // Local array of a particle
            RECT_VERTEX *buffer = &data[i * 6];
            RS_RECT &rect = pRSR[cnt++];
            glm::vec3 pos = storm::math::transformPoint(camMtx, rect.vPos + vWordRelationPos);
            const float sizex = rect.fSize * fScaleX;
            const float sizey = rect.fSize * fScaleY;
            const float sn = sinf(rect.fAngle);
            const float cs = cosf(rect.fAngle);
            const int32_t color = rect.dwColor;
            float u1, v1, u2, v2;
            if (!bUseSubTextures)
            {
                v1 = u1 = 0.0f;
                v2 = u2 = 1.0f;
            }
            else
            {
                u1 = (rect.dwSubTexture % dwSubTexturesX) * du;
                v1 = ((rect.dwSubTexture / dwSubTexturesX) % dwSubTexturesY) * dv;
                u2 = u1 + du;
                v2 = v1 + dv;
            }
            // Filling the particle buffer
            buffer[0].pos = pos + glm::vec3(sizex * (-cs - sn), sizey * (sn - cs), 0.0f);
            buffer[0].color = color;
            buffer[0].u = u1;
            buffer[0].v = v2;
            buffer[1].pos = pos + glm::vec3(sizex * (-cs + sn), sizey * (sn + cs), 0.0f);
            buffer[1].color = color;
            buffer[1].u = u1;
            buffer[1].v = v1;
            buffer[2].pos = pos + glm::vec3(sizex * (cs + sn), sizey * (-sn + cs), 0.0f);
            buffer[2].color = color;
            buffer[2].u = u2;
            buffer[2].v = v1;
            buffer[3].pos = buffer[0].pos;
            buffer[3].color = color;
            buffer[3].u = u1;
            buffer[3].v = v2;
            buffer[4].pos = buffer[2].pos;
            buffer[4].color = color;
            buffer[4].u = u2;
            buffer[4].v = v1;
            buffer[5].pos = pos + glm::vec3(sizex * (cs - sn), sizey * (-sn - cs), 0.0f);
            buffer[5].color = color;
            buffer[5].u = u2;
            buffer[5].v = v2;
        }
        // Draw a buffer
        rectsVBuffer->Unlock();
        CHECKD3DERR(SetFVF(RS_RECT_VERTEX_FORMAT));
        if (cBlockName && cBlockName[0])
            bDraw = TechniqueExecuteStart(cBlockName);
        if (bDraw)
            do
            {
                CHECKD3DERR(d3d9->SetStreamSource(0, rectsVBuffer, 0, sizeof(RECT_VERTEX)));
                CHECKD3DERR(DrawPrimitive(D3DPT_TRIANGLELIST, 0, drawCount * 2));
            } while (cBlockName && TechniqueExecuteNext());
    }

    d3d9->SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(camMtx));
    d3d9->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(oldWorldMatrix));
}

void Renderer::DrawSprites(RS_SPRITE *pRSS, uint32_t dwSpritesNum, const char *cBlockName)
{
    uint32_t i;
#define RS_SPRITE_VERTEX_FORMAT (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
    if (dwSpritesNum == 0)
        return;

    auto *pIndices = new uint16_t[dwSpritesNum * 6];

    for (i = 0; i < dwSpritesNum; i++)
    {
        pIndices[i * 6 + 0] = static_cast<uint16_t>(i * 4 + 0);
        pIndices[i * 6 + 1] = static_cast<uint16_t>(i * 4 + 3);
        pIndices[i * 6 + 2] = static_cast<uint16_t>(i * 4 + 2);
        pIndices[i * 6 + 3] = static_cast<uint16_t>(i * 4 + 0);
        pIndices[i * 6 + 4] = static_cast<uint16_t>(i * 4 + 2);
        pIndices[i * 6 + 5] = static_cast<uint16_t>(i * 4 + 1);
    }

    SetFVF(RS_SPRITE_VERTEX_FORMAT);

    bool bDraw = true;
    if (cBlockName && cBlockName[0])
        bDraw = TechniqueExecuteStart(cBlockName);
    if (bDraw)
        do
        {
            DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, dwSpritesNum * 4, dwSpritesNum * 2, pIndices, D3DFMT_INDEX16,
                                   pRSS, sizeof(RS_SPRITE));
        } while (cBlockName && TechniqueExecuteNext());
    delete[] pIndices;
}

void Renderer::DrawLines(RS_LINE *pRSL, uint32_t dwLinesNum, const char *cBlockName)
{
    if (!pRSL || dwLinesNum == 0)
        return;

#define RS_LINE_VERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_DIFFUSE)

    bool bDraw = true;

    if (cBlockName && cBlockName[0])
        bDraw = TechniqueExecuteStart(cBlockName);
    if (bDraw)
        do
        {
            DrawPrimitiveUP(D3DPT_LINELIST, RS_LINE_VERTEX_FORMAT, dwLinesNum, pRSL, sizeof(RS_LINE));
        } while (cBlockName && TechniqueExecuteNext());
}

void Renderer::DrawLines2D(RS_LINE2D *pRSL2D, size_t dwLinesNum, const char *cBlockName)
{
    if (!pRSL2D || dwLinesNum == 0)
        return;

#define RS_LINE2D_VERTEX_FORMAT (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

    bool bDraw = true;

    if (cBlockName && cBlockName[0])
        bDraw = TechniqueExecuteStart(cBlockName);
    if (bDraw)
        do
        {
            DrawPrimitiveUP(D3DPT_LINELIST, RS_LINE2D_VERTEX_FORMAT, dwLinesNum, pRSL2D, sizeof(RS_LINE2D));
        } while (cBlockName && TechniqueExecuteNext());
}

//-----------------------
HRESULT Renderer::CreateVertexBuffer(UINT Length, uint32_t Usage, uint32_t FVF, D3DPOOL Pool,
                                      storm::shim::VertexBuffer **ppVertexBuffer)
{
    return CHECKD3DERR(d3d9->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, NULL));
}

HRESULT Renderer::SetFVF(uint32_t handle)
{
    return CHECKD3DERR(d3d9->SetVertexShader(NULL)) || CHECKD3DERR(d3d9->SetFVF(handle));
}

void Renderer::SetStreamSource(UINT StreamNumber, int32_t vertexBuffer, UINT Stride)
{
    if (vertexBuffer == -1)
    {
        CHECKD3DERR(d3d9->SetStreamSource(StreamNumber, nullptr, 0, 0));
        return;
    }
    if (!LiveBuffer(vertexBuffer, false, "SetStreamSource"))
        return;
    CHECKD3DERR(d3d9->SetStreamSource(StreamNumber, VertexBuffers[vertexBuffer].buff, 0, Stride));
}

HRESULT Renderer::DrawPrimitive(D3DPRIMITIVETYPE dwPrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
    if (storm::android::NativeInterfaceDrawSuppressed()) return D3D_OK;
    dwNumDrawPrimitive++;
    return CHECKD3DERR(d3d9->DrawPrimitive(dwPrimitiveType, StartVertex, PrimitiveCount));
}

HRESULT Renderer::Release(storm::shim::Object *pObject)
{
    if (pObject)
    {
        const auto result = pObject->Release();
        pObject = nullptr;
        return result;
    }

    return D3D_OK;
}

HRESULT Renderer::GetRenderTarget(storm::shim::Surface **ppRenderTarget)
{
    return CHECKD3DERR(d3d9->GetRenderTarget(0, ppRenderTarget));
}

HRESULT Renderer::SetRenderTarget(storm::shim::Surface *pRenderTarget, storm::shim::Surface *pNewZStencil)
{
    auto result = !CHECKD3DERR(d3d9->SetDepthStencilSurface(pNewZStencil)) &&
                  !CHECKD3DERR(d3d9->SetRenderTarget(0, pRenderTarget));
    return result ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Renderer::Clear(uint32_t Count, const D3DRECT *pRects, uint32_t Flags, D3DCOLOR Color, float Z,
                         uint32_t Stencil)
{
    return CHECKD3DERR(d3d9->Clear(Count, pRects, Flags, Color, Z, Stencil));
}

static bool isInScene = false;

HRESULT Renderer::BeginScene()
{
    if (!isInScene)
    {
        isInScene = true;
        return CHECKD3DERR(d3d9->BeginScene());
    }

    return D3D_OK;
}

HRESULT Renderer::EndScene()
{
    if (isInScene)
    {
        isInScene = false;
        return CHECKD3DERR(d3d9->EndScene());
    }

    return D3D_OK;
}

HRESULT Renderer::SetClipPlane(uint32_t Index, const float *pPlane)
{
    // return d3d9->SetClipPlane( Index, pPlane );
    return D3D_OK;
}

HRESULT Renderer::CreateTexture(UINT Width, UINT Height, UINT Levels, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool,
                                 storm::shim::Texture2D **ppTexture)
{
    return CHECKD3DERR(d3d9->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, NULL));
}

HRESULT Renderer::CreateCubeTexture(UINT EdgeLength, UINT Levels, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool,
                                     storm::shim::CubeTexture **ppCubeTexture)
{
    return CHECKD3DERR(d3d9->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, NULL));
}

HRESULT Renderer::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                             storm::shim::Surface **ppSurface)
{
    return CHECKD3DERR(d3d9->CreateDepthStencilSurface(Width, Height, Format, MultiSample, 0, TRUE, ppSurface, NULL));
}

storm::render::VertexDeclarationId Renderer::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *pVertexElements)
{
    std::vector<D3DVERTEXELEMENT9> elements;
    for (const auto *element = pVertexElements; element->Stream != 0xFF; ++element)
        elements.push_back(*element);
    elements.push_back(D3DDECL_END());
    const auto same = [&](const VertexDeclaration &declaration) {
        return declaration.elements.size() == elements.size() &&
               std::memcmp(declaration.elements.data(), elements.data(),
                           elements.size() * sizeof(D3DVERTEXELEMENT9)) == 0;
    };
    if (const auto it = std::ranges::find_if(vertexDeclarations_, same); it != vertexDeclarations_.end())
        return {static_cast<int32_t>(it - vertexDeclarations_.begin())};

    storm::shim::VertexDeclaration *declaration = nullptr;
    if (CHECKD3DERR(d3d9->CreateVertexDeclaration(elements.data(), &declaration)))
    {
        spdlog::error("Renderer: vertex declaration with {} elements rejected", elements.size() - 1);
        return {};
    }
    vertexDeclarations_.push_back({std::move(elements), declaration});
    return {static_cast<int32_t>(vertexDeclarations_.size() - 1)};
}

void Renderer::SetVertexDeclaration(storm::render::VertexDeclarationId declaration)
{
    if (!declaration || static_cast<size_t>(declaration.value) >= vertexDeclarations_.size())
    {
        spdlog::error("Renderer: SetVertexDeclaration with invalid id {}", declaration.value);
        return;
    }
    CHECKD3DERR(d3d9->SetVertexDeclaration(vertexDeclarations_[declaration.value].declaration));
}

HRESULT Renderer::CreateVertexShader(const uint32_t *pFunction, storm::shim::VertexShader **ppShader)
{
    return CHECKD3DERR(d3d9->CreateVertexShader((const DWORD *)pFunction, ppShader));
}

HRESULT Renderer::CreatePixelShader(const uint32_t *pFunction, storm::shim::PixelShader **ppShader)
{
    return CHECKD3DERR(d3d9->CreatePixelShader((const DWORD *)pFunction, ppShader));
}

HRESULT Renderer::GetVertexShader(storm::shim::VertexShader **ppShader)
{
    return CHECKD3DERR(d3d9->GetVertexShader(ppShader));
}

HRESULT Renderer::GetPixelShader(storm::shim::PixelShader **ppShader)
{
    return CHECKD3DERR(d3d9->GetPixelShader(ppShader));
}


HRESULT Renderer::SetTexture(uint32_t Stage, storm::shim::BaseTexture *pTexture)
{
    return CHECKD3DERR(d3d9->SetTexture(Stage, pTexture));
}

// The shim's render targets are never multisampled surfaces (MSAA is a bgfx texture flag of the scene target).
HRESULT Renderer::GetRenderTargetData(storm::shim::Surface *pRenderTarget, storm::shim::Surface *pDestSurface)
{
    return CHECKD3DERR(d3d9->GetRenderTargetData(pRenderTarget, pDestSurface));
}

HRESULT Renderer::SetVertexShader(storm::shim::VertexShader *pShader)
{
    return CHECKD3DERR(d3d9->SetVertexShader(pShader));
}

HRESULT Renderer::SetPixelShader(storm::shim::PixelShader *pShader)
{
    return CHECKD3DERR(d3d9->SetPixelShader(pShader));
}

HRESULT Renderer::SetVertexShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4iCount)
{
    return CHECKD3DERR(d3d9->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4iCount));
}

HRESULT Renderer::SetPixelShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4iCount)
{
    return CHECKD3DERR(d3d9->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4iCount));
}

uint32_t Renderer::GetMaxActiveLights()
{
    D3DCAPS9 caps{};
    if (CHECKD3DERR(d3d9->GetDeviceCaps(&caps)))
        return 0;
    return caps.MaxActiveLights;
}

CVideoTexture *Renderer::GetVideoTexture(const char *sVideoName)
{
    if (sVideoName == nullptr)
        return nullptr;
    CVideoTexture *retVal = nullptr;
    VideoTextureEntity *pVTLcur = pVTL;

    // check already loaded
    const uint32_t newHash = MakeHashValue(sVideoName);
    while (pVTLcur != nullptr)
    {
        if (pVTLcur->hash == newHash && storm::iEquals(pVTLcur->name, sVideoName))
        {
            if (core.GetEntityPointer(pVTLcur->videoTexture_id))
            {
                pVTLcur->ref++;
                return pVTLcur->VideoTexture;
            }
            VideoTextureEntity *pVTmp = pVTLcur;
            pVTLcur = pVTLcur->next;
            delete[] pVTmp->name;
            delete pVTmp;
            continue;
        }
        pVTLcur = pVTLcur->next;
    }

    // create new video texture
    pVTLcur = new VideoTextureEntity;
    if (pVTLcur == nullptr)
        throw std::runtime_error("memory allocate error");
    pVTLcur->next = pVTL;
    pVTLcur->VideoTexture = nullptr;
    pVTLcur->hash = newHash;
    pVTLcur->ref = 1;
    const auto len = strlen(sVideoName) + 1;
    if ((pVTLcur->name = new char[len]) == nullptr)
        throw std::runtime_error("memory allocate error");
    strcpy_s(pVTLcur->name, len, sVideoName);
    const entid_t ei = core.CreateEntity("TextureSequence");
    pVTLcur->VideoTexture = static_cast<CVideoTexture *>(core.GetEntityPointer(ei));
    if (pVTLcur->VideoTexture != nullptr)
    {
        pVTLcur->videoTexture_id = ei;
        if (pVTLcur->VideoTexture->Initialize(this, sVideoName, true) < 0)
        {
            delete pVTLcur;
            core.EraseEntity(ei);
        }
        else
        {
            pVTL = pVTLcur;
            retVal = pVTLcur->VideoTexture;
        }
    }
    else
    {
        delete pVTLcur;
        core.EraseEntity(ei);
    }

    return retVal;
}

void Renderer::ReleaseVideoTexture(CVideoTexture *pVTexture)
{
    VideoTextureEntity *cur = pVTL;
    VideoTextureEntity *prev = nullptr;

    if (cur != nullptr)
        do
        {
            if (cur->VideoTexture == pVTexture)
            {
                cur->ref--;
                if (cur->ref > 0)
                    return;
                if (prev == nullptr)
                    pVTL = cur->next;
                else
                    prev->next = cur->next;
                core.EraseEntity(cur->videoTexture_id);
                delete[] cur->name;
                delete cur;
                break;
            }
            prev = cur;
        } while ((cur = cur->next) != nullptr);
}

void Renderer::PlayToTexture()
{
    VideoTextureEntity *cur = pVTL;
    while (cur != nullptr)
    {
        if (core.GetEntityPointer(pVTL->videoTexture_id))
        {
            cur->VideoTexture->FrameUpdate();
            cur = cur->next;
        }
        else
        {
            core.Trace("ERROR: void Renderer::PlayToTexture()");
            delete[] cur->name;
            VideoTextureEntity *pcur = cur;
            cur = cur->next;
            if (pVTL == pcur)
                pVTL = cur;
            delete pcur;
        }
    }
}

HRESULT Renderer::ImageBlt(int32_t TextureID, RECT *pDstRect, RECT *pSrcRect)
{
    struct F3DVERTEX
    {
        float x, y, z, rhw;
        float tu, tv;
    };
    RECT dr;
    F3DVERTEX v[6];
    HRESULT hRes;

    if (pDstRect)
    {
        dr = *pDstRect;
    }
    else
    {
        dr.left = 0;
        dr.top = 0;
        dr.right = screen_size.x - 1;
        dr.bottom = screen_size.y - 1;
    }

    for (uint32_t n = 0; n < 6; n++)
    {
        v[n].rhw = 1.0f;
        v[n].z = 0.5f;
    }

    v[0].x = static_cast<float>(dr.left);
    v[0].y = static_cast<float>(dr.top);
    v[0].tu = 0.0f;
    v[0].tv = 0.0f;

    v[1].x = static_cast<float>(dr.left);
    v[1].y = static_cast<float>(dr.bottom);
    v[1].tu = 0.0f;
    v[1].tv = 1.0f;

    v[2].x = static_cast<float>(dr.right);
    v[2].y = static_cast<float>(dr.top);
    v[2].tu = 1.0f;
    v[2].tv = 0.0f;

    v[3].x = static_cast<float>(dr.right);
    v[3].y = static_cast<float>(dr.top);
    v[3].tu = 1.0f;
    v[3].tv = 0.0f;

    v[4].x = static_cast<float>(dr.left);
    v[4].y = static_cast<float>(dr.bottom);
    v[4].tu = 0.0f;
    v[4].tv = 1.0f;

    v[5].x = static_cast<float>(dr.right);
    v[5].y = static_cast<float>(dr.bottom);
    v[5].tu = 1.0f;
    v[5].tv = 1.0f;

    TextureSet(0, TextureID);

    const bool bDraw = TechniqueExecuteStart("texturedialogfon");
    if (bDraw)
        do
        {
            SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXTUREFORMAT2);
            hRes = D3D_OK;
            if (!storm::android::NativeInterfaceDrawSuppressed())
            {
                hRes = d3d9->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, &v, sizeof(F3DVERTEX));
                dwNumDrawPrimitive++;
            }
        } while (TechniqueExecuteNext());

    return hRes;
}

HRESULT Renderer::ImageBlt(const char *pName, RECT *pDstRect, RECT *pSrcRect)
{
    int32_t TextureID;
    TextureID = TextureCreate(pName);
    const HRESULT hRes = ImageBlt(TextureID, pDstRect, pSrcRect);
    TextureRelease(TextureID);
    SetProgressImage(pName);
    return hRes;
}

void Renderer::SetProgressImage(const char *image)
{
    if (!image || !image[0])
    {
        if (progressImageSize > 0 && progressImage)
            progressImage[0] = 0;
        return;
    }
    const int32_t s = strlen(image) + 1;
    if (s > progressImageSize)
    {
        progressImageSize = s;
        delete[] progressImage;
        progressImage = new char[progressImageSize];
    }
    strcpy_s(progressImage, s, image);
}

void Renderer::SetProgressBackImage(const char *image)
{
    if (!image || !image[0])
    {
        if (progressBackImageSize > 0 && progressBackImage)
            progressBackImage[0] = 0;
        return;
    }
    const int32_t s = strlen(image) + 1;
    if (s > progressBackImageSize)
    {
        progressBackImageSize = s;
        delete[] progressBackImage;
        progressBackImage = new char[progressBackImageSize];
    }
    strcpy_s(progressBackImage, s, image);
}

void Renderer::SetTipsImage(const char *image)
{
    if (!image || !image[0])
    {
        if (progressTipsImageSize > 0 && progressTipsImage)
            progressTipsImage[0] = 0;
        return;
    }
    const int32_t s = strlen(image) + 1;
    if (s > progressTipsImageSize)
    {
        progressTipsImageSize = s;
        delete[] progressTipsImage;
        progressTipsImage = new char[progressTipsImageSize];
    }
    memcpy(progressTipsImage, image, s);
}

char *Renderer::GetTipsImage()
{
    return progressTipsImage;
}

void Renderer::StartProgressView()
{
#ifdef __ANDROID__
    storm::android::publishLoading(true);
#endif
    progressSafeCounter = 0;
    if (progressTexture < 0)
    {
        // Loading the texture
        loadFrame = 0;
        isInPViewProcess = true;
        const int32_t t = TextureCreate("Loading\\progress.tga");
        isInPViewProcess = false;
        if (t < 0)
        {
            core.Trace("Progress error!");
            return;
        }
        progressTexture = t;
    }
    else
        return;
    // Loading an unscaled background image
    if (progressBackImage && progressBackImage[0])
    {
        isInPViewProcess = true;
        if (back0Texture >= 0)
            TextureRelease(back0Texture);
        back0Texture = -1;
        back0Texture = TextureCreate(progressBackImage);
        isInPViewProcess = false;
    }
    else
    {
        back0Texture = -1;
    }
    // Loading a scalable background image
    if (progressImage && progressImage[0])
    {
        isInPViewProcess = true;
        if (backTexture >= 0)
            TextureRelease(backTexture);
        backTexture = -1;
        backTexture = TextureCreate(progressImage);
        isInPViewProcess = false;
    }
    else
    {
        backTexture = -1;
    }
    // Loading tips
    if (progressTipsImage && progressTipsImage[0])
    {
        isInPViewProcess = true;
        if (progressTipsTexture >= 0)
            TextureRelease(progressTipsTexture);
        progressTipsTexture = -1;
        progressTipsTexture = TextureCreate(progressTipsImage);
        isInPViewProcess = false;
    }
    progressUpdateTime = SDL_GetTicks() - 1000;
}

void Renderer::ProgressView()
{
    // get the texture
    if (progressTexture < 0)
        return;
    if (isInPViewProcess)
        return;
    // Analyzing time
    const uint32_t time = SDL_GetTicks();
    if (abs(static_cast<int32_t>(progressUpdateTime - time)) < 50)
        return;
    progressUpdateTime = time;
    isInPViewProcess = true;
    progressSafeCounter = 0;
    // Drawing mode
    BeginScene();
    // Filling the vertices of the texture
    struct LoadVertex
    {
        float x, y, z, rhw;
        uint32_t color;
        float u, v;
    } v[4];
    uint32_t i;
    for (i = 0; i < 4; i++)
    {
        v[i].z = 0.5;
        v[i].rhw = 2.0;
        v[i].color = 0xffffffff;
    }
    // Calculate the rectangle in which to draw
    D3DVIEWPORT9 vp;
    GetViewport(&vp);

    v[0].x = 0.0f;
    v[0].y = 0.0f;
    v[1].x = float(vp.Width);
    v[1].y = 0.0f;
    v[2].x = 0.0f;
    v[2].y = float(vp.Height);
    v[3].x = float(vp.Width);
    v[3].y = float(vp.Height);
    v[0].u = 0.0f;
    v[0].v = 0.0f;
    v[1].u = 1.0f;
    v[1].v = 0.0f;
    v[2].u = 0.0f;
    v[2].v = 1.0f;
    v[3].u = 1.0f;
    v[3].v = 1.0f;
    TextureSet(0, back0Texture);
    if (back0Texture < 0)
        for (i = 0; i < 4; i++)
            v[i].color = 0;
    if (back0Texture >= 0)
    {
        DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v, sizeof(v[0]),
                        "ProgressBackTech");
    }

    float dy = 0.0f;
    float dx = ((float(vp.Width) - (4.0f * float(vp.Height) / 3.0f)) / 2.0f);
    if (dx < 10.0f)
        dx = 0.0f;
    else
    {
        dy = 25.0f;
        dx = ((float(vp.Width) - (4.0f * (float(vp.Height) - 2.0f * dy) / 3.0f)) / 2.0f);
    }

    v[0].x = 0.0f + dx;
    v[0].y = 0.0f + dy;
    v[1].x = float(vp.Width) - dx;
    v[1].y = 0.0f + dy;
    v[2].x = 0.0f + dx;
    v[2].y = float(vp.Height) - dy;
    v[3].x = float(vp.Width) - dx;
    v[3].y = float(vp.Height) - dy;
    v[0].u = 0.0f;
    v[0].v = 0.0f;
    v[1].u = 1.0f;
    v[1].v = 0.0f;
    v[2].u = 0.0f;
    v[2].v = 1.0f;
    v[3].u = 1.0f;
    v[3].v = 1.0f;

    TextureSet(0, backTexture);
    if (backTexture < 0)
    {
        for (i = 0; i < 4; i++)
            v[i].color = 0;
    }
    else
    {
        if (progressTipsTexture >= 0)
        {
            TextureSet(1, progressTipsTexture);
            DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v, sizeof(v[0]),
                            "ProgressBackTechWithTips");
        }
        else
        {
            DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v, sizeof(v[0]),
                            "ProgressBackTech");
        }
    }
    if (backTexture < 0)
        for (i = 0; i < 4; i++)
            v[i].color = 0xffffffff;
    // Animated object
    m_fHeightDeformator = ((float)vp.Height * 4.0f) / ((float)vp.Width * 3.0f);
    // core.Trace(" size_x %f", (vp.Width - dx * 2.0f)*progressFramesWidth);
    glm::vec3 pos((vp.Width - dx * 2.0f) * progressFramesPosX + dx, (vp.Height - dy * 2.0f) * progressFramesPosY + dy,
                0.0f);
    glm::vec3 size((vp.Width - dx * 2.0f) * progressFramesWidth,
                 (vp.Height - dy * 2.0f) * progressFramesHeight * 4.0f / 3.0f, 0.0f);
    v[0].x = pos.x;
    v[0].y = pos.y;
    v[1].x = pos.x + size.x + 0.5f;
    v[1].y = pos.y;
    v[2].x = pos.x;
    v[2].y = pos.y + size.y + 0.5f;
    v[3].x = pos.x + size.x + 0.5f;
    v[3].y = pos.y + size.y + 0.5f;
    v[3].y = pos.y + size.y + 0.5f;
    // Frame grid size
    int32_t sizeX = progressFramesCountX;
    int32_t sizeY = progressFramesCountY;
    // Position of the current frame
    int32_t fx = loadFrame % sizeX;
    int32_t fy = loadFrame / sizeY;
    v[0].u = fx / float(sizeX);
    v[0].v = fy / float(sizeY);
    v[1].u = (fx + 1.0f) / float(sizeX);
    v[1].v = fy / float(sizeY);
    v[2].u = fx / float(sizeX);
    v[2].v = (fy + 1.0f) / float(sizeY);
    v[3].u = (fx + 1.0f) / float(sizeX);
    v[3].v = (fy + 1.0f) / float(sizeY);
    // Draw
#ifdef __ANDROID__
    // One coverage rule (android_hud_presenter.hpp): the loading/saving spinner of the original
    // interface does not draw over a touch UI; the game's loading picture stays.
    if (!storm::android::legacyDrawRefused("loading spinner"))
#endif
    {
        TextureSet(0, progressTexture);
        DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v, sizeof(v[0]),
                        "ProgressTech");
    }
    EndScene();
#ifdef __ANDROID__
    // A progress/loading present is not the admitted world-map frame.
    storm::android::worldMapHud.presented(false);
#endif
    d3d9->Present();
    BeginScene();
    // Next frame
    loadFrame++;
    if (loadFrame >= sizeX * sizeY)
        loadFrame = 0;
    // leave
    isInPViewProcess = false;
}

void Renderer::EndProgressView()
{
#ifdef __ANDROID__
    storm::android::publishLoading(false);
#endif
    if (progressTexture >= 0)
        TextureRelease(progressTexture);
    progressTexture = -1;
    if (backTexture >= 0)
        TextureRelease(backTexture);
    backTexture = -1;
    if (back0Texture >= 0)
        TextureRelease(back0Texture);
    back0Texture = -1;
    if (progressTipsTexture >= 0)
        TextureRelease(progressTipsTexture);
    progressTipsTexture = -1;
    if (progressImage && progressImageSize > 0)
        progressImage[0] = 0;
    if (progressBackImage && progressBackImageSize > 0)
        progressBackImage[0] = 0;
    if (progressTipsImage && progressTipsImageSize > 0)
        progressTipsImage[0] = 0;
}

void Renderer::SetColorParameters(float fGamma, float fBrightness, float fContrast)
{
    uint16_t rgb[256];
    for (uint32_t i = 0; i < 256; i++)
    {
        float fRamp = std::clamp(fContrast * 255.0f * 256.0f * powf(static_cast<float>(i / 255.0f), 1.0f / fGamma) +
                                     fBrightness * 256.0f,
                                 0.0f, 65535.0f);
        rgb[i] = static_cast<uint16_t>(fRamp);
    }
    core.GetWindow()->SetGamma(rgb, rgb, rgb);
}

void Renderer::MakeDrawVector(RS_LINE *pLines, uint32_t dwNumSubLines, const glm::mat4 &mMatrix, glm::vec3 vUp, glm::vec3 v1,
                               glm::vec3 v2, float fScale, uint32_t dwColor)
{
    uint32_t i;
    uint32_t k;

    // for (i=0; i<dwNumSubLines * 2 + 2; i++) pLines[i].dwColor = dwColor;
    k = dwNumSubLines * 2 + 2; // boal optimization if the for loop runs the calculations every iteration
    for (i = 0; i < k; i++)
    {
        pLines[i].dwColor = dwColor;
    }
    pLines[0].vPos = v1;
    pLines[1].vPos = v1 + (fScale * v2);

    const float fRadius = 0.03f * fScale;
    const float fDist = 0.85f * fScale * sqrtf(storm::math::lengthSquared(v2));

    for (i = 0; i < dwNumSubLines; i++)
    {
        const float fAng = PIm2 * static_cast<float>(i) / static_cast<float>(dwNumSubLines);

        const float x = fRadius * sinf(fAng);
        const float z = fRadius * cosf(fAng);

        glm::vec3 vRes;

        if (fabsf(vUp.x) < 1e-5f)
            vRes = glm::vec3(fDist, x, z);
        if (fabsf(vUp.y) < 1e-5f)
            vRes = glm::vec3(x, fDist, z);
        if (fabsf(vUp.z) < 1e-5f)
            vRes = glm::vec3(x, z, fDist);
        vRes = storm::math::transformPoint((glm::mat4 &)mMatrix, vRes);
        pLines[2 + i * 2 + 0].vPos = vRes;
        pLines[2 + i * 2 + 1].vPos = pLines[1].vPos;
    }
}

void Renderer::DrawVector(const glm::vec3 &v1, const glm::vec3 &v2, uint32_t dwColor, const char *pTechniqueName)
{
    RS_LINE lines[51 * 2];
    glm::mat4 mView;

    const float fScale = sqrtf(storm::math::lengthSquared(v2 - v1));
    if (!storm::math::viewMatrix(mView, v1, v2, glm::vec3(0.0f, 1.0f, 0.0f)))
        if (!storm::math::viewMatrix(mView, v1, v2, glm::vec3(1.0f, 0.0f, 0.0f)))
            return;

    storm::math::invertRigid(mView);

    MakeDrawVector(&lines[0], 50, mView, glm::vec3(1.0f, 1.0f, 0.0f), storm::math::pos(mView), storm::math::vz(mView), fScale, dwColor);

    const glm::mat4 mWorldSave;
    GetTransform(D3DTS_WORLD, storm::math::d3dMatrix(mWorldSave));
    SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(glm::mat4()));
    DrawLines(lines, 51, pTechniqueName);
    SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(mWorldSave));
}

void Renderer::DrawSphere(const glm::vec3 &vPos, float fRadius, uint32_t dwColor)
{
    glm::mat4 m;
    m = storm::math::translation(vPos.x, vPos.y, vPos.z);
    m[0][0] = fRadius;
    m[1][1] = fRadius;
    m[2][2] = fRadius;

    SetRenderState(D3DRS_TEXTUREFACTOR, dwColor);
    SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(m));
    DrawPrimitiveUP(D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_DIFFUSE, sphereNumTrgs, sphereVertex,
                    sizeof(SphereVertex), "DXSphere");
}


void Renderer::DrawEllipsoid(const glm::vec3 &vPos, float a, float b, float c, float ay, uint32_t dwColor)
{
    glm::mat4 trans, scale, rot;
    trans = storm::math::translation(vPos.x, vPos.y, vPos.z);
    scale = storm::math::scaling(a, b, c);
    rot = storm::math::rotationY(ay);

    SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(trans * (rot * scale)));
    SetRenderState(D3DRS_TEXTUREFACTOR, dwColor);
    DrawPrimitiveUP(D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_DIFFUSE, sphereNumTrgs, sphereVertex,
                    sizeof(SphereVertex), "DXEllipsoid");
}

void Renderer::SetLoadTextureEnable(bool bEnable)
{
    bLoadTextureEnabled = bEnable;
}

bool Renderer::PushRenderTarget()
{
    RenderTarget renderTarget{};
    GetRenderTarget(&renderTarget.pRenderTarget);
    CHECKD3DERR(d3d9->GetDepthStencilSurface(&renderTarget.pDepthSurface));
    GetViewport(&renderTarget.ViewPort);
    stRenderTarget.push(renderTarget);

    return true;
}

bool Renderer::PopRenderTarget()
{
    if (stRenderTarget.empty())
    {
        core.Trace("Renderer: Try to pop RenderTarget, but RenderTarget stack is empty");
        return false;
    }

    auto top = stRenderTarget.top();
    SetRenderTarget(top.pRenderTarget, top.pDepthSurface);
    SetViewport(&top.ViewPort);
    RELEASE(top.pRenderTarget);
    RELEASE(top.pDepthSurface);
    stRenderTarget.pop();

    return true;
}

bool Renderer::SetRenderTarget(storm::shim::CubeTexture *pRenderTarget, uint32_t FaceType, uint32_t dwLevel,
                                storm::shim::Surface *pZStencil)
{
    storm::shim::Surface *pSurface;
    return !CHECKD3DERR(pRenderTarget->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(FaceType), dwLevel, &pSurface)) &&
           !CHECKD3DERR(SetRenderTarget(pSurface, pZStencil)) && Release(pSurface) == D3D_OK;
}

void Renderer::SetView(const glm::mat4 &mView)
{
    SetTransform(D3DTS_VIEW, storm::math::d3dMatrix(mView));
}

void Renderer::SetWorld(const glm::mat4 &mWorld)
{
    SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(mWorld));
}

void Renderer::SetProjection(const glm::mat4 &mProjection)
{
    SetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(mProjection));
}

const glm::mat4 &Renderer::GetView()
{
    GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(mView));
    return mView;
}

const glm::mat4 &Renderer::GetWorld()
{
    GetTransform(D3DTS_WORLD, storm::math::d3dMatrix(mWorld));
    return mWorld;
}

const glm::mat4 &Renderer::GetProjection()
{
    GetTransform(D3DTS_PROJECTION, storm::math::d3dMatrix(mProjection));
    return mProjection;
}

storm::shim::BaseTexture *Renderer::GetTextureFromID(int32_t nTextureID)
{
    if (nTextureID < 0)
        return nullptr;
    return Textures[nTextureID].d3dtex;
}

bool Renderer::keepTexturePixels(int32_t texture)
{
    if(texture<0 || size_t(texture)>=MAX_STEXTURES) return false;
    if(keptPixels_.contains(texture)) return true;
    auto* base=Textures[texture].d3dtex;
    D3DSURFACE_DESC desc{};
    D3DLOCKED_RECT lock{};
    if(!base || base->GetType()!=D3DRTYPE_TEXTURE ||
       FAILED(static_cast<storm::shim::Texture2D*>(base)->GetLevelDesc(0,&desc)) ||
       (desc.Format!=D3DFMT_A8R8G8B8 && desc.Format!=D3DFMT_X8R8G8B8) ||
       FAILED(static_cast<storm::shim::Texture2D*>(base)->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)))
    {
        core.Trace("keepTexturePixels: texture %d (%s) has no readable A8R8G8B8/X8R8G8B8 level 0", texture,
                   Textures[texture].name ? Textures[texture].name : "?");
        return false;
    }
    auto& kept=keptPixels_[texture];
    kept.width=desc.Width; kept.height=desc.Height; kept.opaque=desc.Format==D3DFMT_X8R8G8B8;
    kept.bytes.resize(size_t(desc.Width)*desc.Height*4);
    for(uint32_t y=0;y<desc.Height;++y)
        std::memcpy(kept.bytes.data()+size_t(y)*desc.Width*4,
                    static_cast<const uint8_t*>(lock.pBits)+size_t(y)*lock.Pitch,size_t(desc.Width)*4);
    static_cast<storm::shim::Texture2D*>(base)->UnlockRect(0);
    return true;
}

bool Renderer::sampleTextureArgb(int32_t texture,float u,float v,uint32_t& result)
{
    const auto it=keptPixels_.find(texture);
    if(it==keptPixels_.end()) return false;
    const KeptPixels& kept=it->second;
    const storm::render::TexturePixels pixels{kept.bytes,kept.width,kept.height,size_t(kept.width)*4,
                                              storm::render::PixelOrder::bgra8};
    uint32_t color=0;
    if(!storm::render::sampleArgb(pixels,u,v,color)) return false;
    result=kept.opaque ? color|0xff000000u : color;
    return true;
}

int32_t Renderer::GetRenderTargetAsTexture()
{
    storm::shim::Surface *renderTarget = nullptr;
    D3DSURFACE_DESC desc;
    // No render target (no surface: an autosave while the app goes to the background) is no copy.
    if (CHECKD3DERR(GetRenderTarget(&renderTarget)) || !renderTarget)
        return -1;
    storm::shim::Texture2D *texture = nullptr;
    storm::shim::Surface *level = nullptr;
    const bool copied = !CHECKD3DERR(renderTarget->GetDesc(&desc)) &&
                        !CHECKD3DERR(CreateTexture(desc.Width, desc.Height, 1, 0, desc.Format, D3DPOOL_DEFAULT, &texture)) &&
                        !CHECKD3DERR(texture->GetSurfaceLevel(0, &level)) &&
                        !CHECKD3DERR(GetRenderTargetData(renderTarget, level));
    if (level)
        level->Release();
    renderTarget->Release();
    if (!copied)
    {
        if (texture)
            texture->Release();
        spdlog::error("Renderer: cannot copy the render target into a texture");
        return -1;
    }
    return AddCodeTexture(texture, desc.Width * desc.Height * 4, false);
}

POINT Renderer::GetRenderTargetSize()
{
    storm::shim::Surface *renderTarget = nullptr;
    D3DSURFACE_DESC desc;
    if (CHECKD3DERR(GetRenderTarget(&renderTarget)))
        return {};
    const bool known = !CHECKD3DERR(renderTarget->GetDesc(&desc));
    renderTarget->Release();
    if (!known)
        return {};
    return {static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
}

