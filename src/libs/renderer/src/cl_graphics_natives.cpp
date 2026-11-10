// Graphics natives Caribbean Legend 2.0 declares in Program/seadogs.c (the Graphics_* settings of
// its options screen, GetTexturePath, SetCameraShake). Registered by DX9RENDER_SCRIPT_LIBRIARY.
//
// CL's desktop renderer exposes settings our mobile bgfx renderer does not have or handles itself.
// Each native below is one of:
// - real: brightness / contrast / gamma drive DX9RENDER::SetColorParameters like TEHO's
//   XI_SetColorCorrection; GetTexturePath reads the texture search path.
// - fx: the setting corresponds to an option of the graphics effects package (fx_* options,
//   renderer_bgfx/fx/include/storm/fx/settings.hpp). The runtime fx configuration is not part of
//   this engine branch yet, so the value is kept (the getter returns it) and the first call logs
//   which fx option it will drive; kFxMapping is the table to wire when it lands.
// - unsupported: a desktop-only feature (DLSS, DLAA, frame generation, TAA, exclusive full
//   screen, display modes, VSync control). The "is supported" queries return 0, so CL's options
//   screen hides those entries; setters are ignored, getters return the fixed mobile value.
// - kept: no counterpart; the value is only stored so the options screen reads back what it set.
// Every non-real native logs once through storm::reportScriptStub.

#include "cl_graphics_natives.hpp"

#include "s_device.h"

#include "core.h"
#include "script_stub.hpp"
#include "v_s_stack.h"

#include <algorithm>
#include <string>

namespace
{
// What CL's scripts last set (they read these back on the options screen).
struct ClGraphicsState
{
    int32_t shadows = 1;
    int32_t dynamic_shadows = 0;
    int32_t character_shadows = 1;
    int32_t ssao = 0;
    int32_t ao_mode = 0;
    int32_t sea_reflections = 1;
    float sea_texture_multiplier = 1.0f;
    int32_t sea_geometry_multiplier = 2;
    int32_t sea_refraction = 1;
    float taa_sharpness = 0.5f;
    int32_t fxaa = 0;
    int32_t ssaa = 0;
    int32_t anisotropy = 0;
    float temperature = 0.0f;
    float saturation = 1.0f;
    // CL's defaults (Program/interface/interface.c GAMMA_DEFAULT etc.)
    float gamma = 1.06f;
    float contrast = 1.006f;
    float brightness = 0.0f;
} g_state;

// CL setting -> fx option it will drive once the runtime fx configuration is in this branch.
struct FxMapping
{
    const char *native;
    const char *fx_option;
};
constexpr FxMapping kFxMapping[] = {
    {"Graphics_SetShadows", "fx_shadows (0 off, 1 low, 2+ soft)"},
    {"Graphics_SetDynamicShadows", "fx_shadows (0 off, 1 low, 2+ soft)"},
    {"Graphics_SetCharacterShadows", "fx_contact_shadows"},
    {"Graphics_SetSSAO", "fx_ssao"},
    {"Graphics_SetAOMode", "fx_ssao (mode > 0)"},
    {"Graphics_SetSeaDetail", "fx_water_refraction (refraction argument)"},
    {"Graphics_SetFXAA", "fx_fxaa (CL passes selector + 1; > 1 = on)"},
    {"Graphics_SetSSAA", "fx_msaa (CL passes selector + 1; > 1 = 4 samples)"},
    {"Graphics_SetAnisotropicFiltering", "fx_anisotropy (CL passes 0, 4, 8, 16)"},
    {"Graphics_PreviewAnisotropicFiltering", "fx_anisotropy (CL passes 0, 4, 8, 16)"},
    {"Graphics_SetTAASharpness", "fx_sharpen / fx sharpness"},
};

void reportFx(const char *native)
{
    for (const auto &m : kFxMapping)
        if (std::string_view(m.native) == native)
        {
            storm::reportScriptStub(native, std::string("kept; will drive ") + m.fx_option +
                                                " when the runtime fx configuration is merged into this branch");
            return;
        }
}

int32_t popInt(VS_STACK *s)
{
    auto *v = static_cast<VDATA *>(s->Pop());
    int32_t value = 0;
    if (v && !v->Get(value))
    {
        float f = 0.0f;
        if (v->Get(f))
            value = static_cast<int32_t>(f);
    }
    return value;
}

float popFloat(VS_STACK *s)
{
    auto *v = static_cast<VDATA *>(s->Pop());
    float value = 0.0f;
    if (v && !v->Get(value))
    {
        int32_t i = 0;
        if (v->Get(i))
            value = static_cast<float>(i);
    }
    return value;
}

uint32_t pushInt(VS_STACK *s, int32_t value)
{
    auto *v = static_cast<VDATA *>(s->Push());
    if (!v)
        return IFUNCRESULT_FAILED;
    v->Set(value);
    return IFUNCRESULT_OK;
}

uint32_t pushFloat(VS_STACK *s, float value)
{
    auto *v = static_cast<VDATA *>(s->Push());
    if (!v)
        return IFUNCRESULT_FAILED;
    v->Set(value);
    return IFUNCRESULT_OK;
}

DX9RENDER *renderer()
{
    if (!DX9RENDER::pRS)
        core.GetService("dx9render");
    return DX9RENDER::pRS;
}

// --- real -------------------------------------------------------------------------------------

void applyColorCorrection()
{
    // CL's brightness runs -1..1; the renderer's ramp offset uses TEHO's -50..50 scale.
    if (auto *r = renderer())
        r->SetColorParameters(g_state.gamma, g_state.brightness * 50.0f, g_state.contrast);
}

uint32_t setGamma(VS_STACK *s)
{
    g_state.gamma = std::max(popFloat(s), 0.05f);
    applyColorCorrection();
    return IFUNCRESULT_OK;
}

uint32_t setContrast(VS_STACK *s)
{
    g_state.contrast = popFloat(s);
    applyColorCorrection();
    return IFUNCRESULT_OK;
}

uint32_t setBrightness(VS_STACK *s)
{
    g_state.brightness = popFloat(s);
    applyColorCorrection();
    return IFUNCRESULT_OK;
}

uint32_t getTexturePath(VS_STACK *s)
{
    const int32_t level = popInt(s);
    auto *v = static_cast<VDATA *>(s->Push());
    if (!v)
        return IFUNCRESULT_FAILED;
    auto *r = renderer();
    v->Set(r ? r->GetTexturePath(level) : "");
    return IFUNCRESULT_OK;
}

// --- fx and kept settings ---------------------------------------------------------------------

#define STORM_INT_SETTER(fn, native, field)                                                                         \
    uint32_t fn(VS_STACK *s)                                                                                        \
    {                                                                                                               \
        g_state.field = popInt(s);                                                                                  \
        reportFx(native);                                                                                           \
        return IFUNCRESULT_OK;                                                                                      \
    }
#define STORM_INT_GETTER(fn, field)                                                                                 \
    uint32_t fn(VS_STACK *s)                                                                                        \
    {                                                                                                               \
        return pushInt(s, g_state.field);                                                                           \
    }

STORM_INT_SETTER(setShadows, "Graphics_SetShadows", shadows)
STORM_INT_SETTER(setDynamicShadows, "Graphics_SetDynamicShadows", dynamic_shadows)
STORM_INT_GETTER(getDynamicShadows, dynamic_shadows)
STORM_INT_SETTER(setCharacterShadows, "Graphics_SetCharacterShadows", character_shadows)
STORM_INT_GETTER(getCharacterShadows, character_shadows)
STORM_INT_SETTER(setSsao, "Graphics_SetSSAO", ssao)
STORM_INT_SETTER(setAoMode, "Graphics_SetAOMode", ao_mode)
STORM_INT_GETTER(getAoMode, ao_mode)
STORM_INT_SETTER(setFxaa, "Graphics_SetFXAA", fxaa)
STORM_INT_SETTER(setSsaa, "Graphics_SetSSAA", ssaa)
STORM_INT_SETTER(setAnisotropy, "Graphics_SetAnisotropicFiltering", anisotropy)
STORM_INT_SETTER(previewAnisotropy, "Graphics_PreviewAnisotropicFiltering", anisotropy)
STORM_INT_GETTER(getAnisotropy, anisotropy)
#undef STORM_INT_SETTER
#undef STORM_INT_GETTER

uint32_t setSeaReflections(VS_STACK *s)
{
    g_state.sea_reflections = popInt(s);
    storm::reportScriptStub("Graphics_SetSeaReflections", "kept only: the sea reflection switch has no counterpart");
    return IFUNCRESULT_OK;
}

uint32_t getSeaReflections(VS_STACK *s)
{
    return pushInt(s, g_state.sea_reflections);
}

// Graphics_GetSeaDetail(&obj): writes the attributes CL's scripts pass back to Graphics_SetSeaDetail
// (Program/interface/interface.c reads SeaTextureMultiplier, SeaGeometryMultiplier, SeaRefraction).
uint32_t getSeaDetail(VS_STACK *s)
{
    auto *v = static_cast<VDATA *>(s->Pop());
    ATTRIBUTES *a = v ? v->GetAClass() : nullptr;
    if (!a)
        return IFUNCRESULT_FAILED;
    a->SetAttribute("SeaTextureMultiplier", std::to_string(g_state.sea_texture_multiplier));
    a->SetAttribute("SeaGeometryMultiplier", std::to_string(g_state.sea_geometry_multiplier));
    a->SetAttribute("SeaRefraction", std::to_string(g_state.sea_refraction));
    return IFUNCRESULT_OK;
}

// Graphics_SetSeaDetail(textureMultiplier, geometryMultiplier, refraction) -> 1 when applied.
uint32_t setSeaDetail(VS_STACK *s)
{
    g_state.sea_refraction = popInt(s);
    g_state.sea_geometry_multiplier = popInt(s);
    g_state.sea_texture_multiplier = popFloat(s);
    reportFx("Graphics_SetSeaDetail");
    return pushInt(s, 1);
}

uint32_t setTaaSharpness(VS_STACK *s)
{
    g_state.taa_sharpness = popFloat(s);
    reportFx("Graphics_SetTAASharpness");
    return IFUNCRESULT_OK;
}

uint32_t getTaaSharpness(VS_STACK *s)
{
    return pushFloat(s, g_state.taa_sharpness);
}

uint32_t setTemperature(VS_STACK *s)
{
    g_state.temperature = popFloat(s);
    storm::reportScriptStub("Graphics_SetTemperature", "kept only: no colour temperature control in the renderer");
    return IFUNCRESULT_OK;
}

uint32_t getTemperature(VS_STACK *s)
{
    return pushFloat(s, g_state.temperature);
}

uint32_t setSaturation(VS_STACK *s)
{
    g_state.saturation = popFloat(s);
    storm::reportScriptStub("Graphics_SetSaturation", "kept only: no saturation control in the renderer");
    return IFUNCRESULT_OK;
}

uint32_t getSaturation(VS_STACK *s)
{
    return pushFloat(s, g_state.saturation);
}

// --- unsupported desktop features ---------------------------------------------------------------

template <int Arguments> void popN(VS_STACK *s)
{
    for (int i = 0; i < Arguments; ++i)
        s->Pop();
}

uint32_t unsupportedQuery(VS_STACK *s) // Is*Supported, Get of a desktop-only feature: 0
{
    return pushInt(s, 0);
}

#define STORM_UNSUPPORTED_SETTER(fn, native, arguments, why)                                                        \
    uint32_t fn(VS_STACK *s)                                                                                        \
    {                                                                                                               \
        popN<arguments>(s);                                                                                         \
        storm::reportScriptStub(native, why);                                                                       \
        return IFUNCRESULT_OK;                                                                                      \
    }
STORM_UNSUPPORTED_SETTER(setTaa, "Graphics_SetTAA", 1, "unsupported on mobile (no TAA); ignored, Graphics_GetTAA returns 0")
STORM_UNSUPPORTED_SETTER(setDlss, "Graphics_SetDLSS", 1, "unsupported (NVIDIA desktop only); ignored")
STORM_UNSUPPORTED_SETTER(setFrameGeneration, "Graphics_SetFrameGeneration", 1, "unsupported (desktop only); ignored")
STORM_UNSUPPORTED_SETTER(setVSync, "Graphics_SetVSync", 1, "the mobile display is always synchronised; ignored, Graphics_GetVSync returns 1")
STORM_UNSUPPORTED_SETTER(setDisplayResolution, "Graphics_SetDisplayResolution", 2, "the surface size is the device screen; ignored")
STORM_UNSUPPORTED_SETTER(getDisplayResolutions, "Graphics_GetDisplayResolutions", 1, "no display modes on mobile; the object is left empty")
STORM_UNSUPPORTED_SETTER(setFullScreen, "Graphics_SetFullScreen", 1, "always full screen on mobile; ignored, Graphics_GetFullScreen returns 1")
STORM_UNSUPPORTED_SETTER(setCameraShake, "SetCameraShake", 8, "camera shake is not wired into the cameras yet; no shake")
#undef STORM_UNSUPPORTED_SETTER

uint32_t alwaysOn(VS_STACK *s) // VSync, full screen
{
    return pushInt(s, 1);
}

uint32_t getVramBudget(VS_STACK *s)
{
    // CL compares option VRAM estimates against this budget (MB). Unknown on GLES: 0 = no budget.
    storm::reportScriptStub("Graphics_GetVRAMBudget", "GLES reports no video memory budget; returns 0");
    return pushFloat(s, 0.0f);
}
} // namespace

void registerClGraphicsNatives()
{
    using storm::registerScriptFunction;
    registerScriptFunction("GetTexturePath", "string", 1, getTexturePath);
    registerScriptFunction("SetCameraShake", "void", 8, setCameraShake);
    registerScriptFunction("Graphics_SetShadows", "void", 1, setShadows);
    registerScriptFunction("Graphics_SetDynamicShadows", "void", 1, setDynamicShadows);
    registerScriptFunction("Graphics_GetDynamicShadows", "int", 0, getDynamicShadows);
    registerScriptFunction("Graphics_SetCharacterShadows", "void", 1, setCharacterShadows);
    registerScriptFunction("Graphics_GetCharacterShadows", "int", 0, getCharacterShadows);
    registerScriptFunction("Graphics_SetSSAO", "void", 1, setSsao);
    registerScriptFunction("Graphics_SetAOMode", "void", 1, setAoMode);
    registerScriptFunction("Graphics_GetAOMode", "int", 0, getAoMode);
    registerScriptFunction("Graphics_SetSeaReflections", "void", 1, setSeaReflections);
    registerScriptFunction("Graphics_GetSeaReflections", "int", 0, getSeaReflections);
    registerScriptFunction("Graphics_GetSeaDetail", "void", 1, getSeaDetail);
    registerScriptFunction("Graphics_SetSeaDetail", "int", 3, setSeaDetail);
    registerScriptFunction("Graphics_SetTAA", "void", 1, setTaa);
    registerScriptFunction("Graphics_GetTAA", "int", 0, unsupportedQuery);
    registerScriptFunction("Graphics_SetTAASharpness", "void", 1, setTaaSharpness);
    registerScriptFunction("Graphics_GetTAASharpness", "float", 0, getTaaSharpness);
    registerScriptFunction("Graphics_IsDLAASupported", "int", 0, unsupportedQuery);
    registerScriptFunction("Graphics_IsDLSSSupported", "int", 0, unsupportedQuery);
    registerScriptFunction("Graphics_SetDLSS", "void", 1, setDlss);
    registerScriptFunction("Graphics_GetDLSS", "int", 0, unsupportedQuery);
    registerScriptFunction("Graphics_IsFrameGenerationSupported", "int", 0, unsupportedQuery);
    registerScriptFunction("Graphics_SetFrameGeneration", "void", 1, setFrameGeneration);
    registerScriptFunction("Graphics_GetFrameGeneration", "int", 0, unsupportedQuery);
    registerScriptFunction("Graphics_SetVSync", "void", 1, setVSync);
    registerScriptFunction("Graphics_GetVSync", "int", 0, alwaysOn);
    registerScriptFunction("Graphics_SetFXAA", "void", 1, setFxaa);
    registerScriptFunction("Graphics_SetDisplayResolution", "void", 2, setDisplayResolution);
    registerScriptFunction("Graphics_GetDisplayResolutions", "void", 1, getDisplayResolutions);
    registerScriptFunction("Graphics_SetFullScreen", "void", 1, setFullScreen);
    registerScriptFunction("Graphics_GetFullScreen", "int", 0, alwaysOn);
    registerScriptFunction("Graphics_SetAnisotropicFiltering", "void", 1, setAnisotropy);
    registerScriptFunction("Graphics_PreviewAnisotropicFiltering", "void", 1, previewAnisotropy);
    registerScriptFunction("Graphics_GetAnisotropicFiltering", "int", 0, getAnisotropy);
    registerScriptFunction("Graphics_GetVRAMBudget", "float", 0, getVramBudget);
    registerScriptFunction("Graphics_SetSSAA", "void", 1, setSsaa);
    registerScriptFunction("Graphics_SetGamma", "void", 1, setGamma);
    registerScriptFunction("Graphics_SetTemperature", "void", 1, setTemperature);
    registerScriptFunction("Graphics_GetTemperature", "float", 0, getTemperature);
    registerScriptFunction("Graphics_SetSaturation", "void", 1, setSaturation);
    registerScriptFunction("Graphics_GetSaturation", "float", 0, getSaturation);
    registerScriptFunction("Graphics_SetContrast", "void", 1, setContrast);
    registerScriptFunction("Graphics_SetBrightness", "void", 1, setBrightness);
}
