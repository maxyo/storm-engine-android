#include "model_realizer.h"

#include "core.h"
#include "lights.h"
#include "model.h"

// ============================================================================================
// Construction, destruction
// ============================================================================================

extern float fCausticScale, fCausticDelta, fFogDensity, fCausticDistance;
extern glm::vec4 v4CausticColor;
extern bool bCausticEnable;
extern float fCausticFrame;
extern int32_t iCausticTex[32];

LocModelRealizer::LocModelRealizer()
{
    lights = nullptr;
    bShow = true;
}

LocModelRealizer::~LocModelRealizer()
{
}

// Initialization
bool LocModelRealizer::Init()
{
    rs = static_cast<VRenderer *>(core.GetService("renderer"));
    gs = static_cast<VGEOMETRY *>(core.GetService("geometry"));
    return true;
}

// Execution
void LocModelRealizer::Execute(uint32_t delta_time)
{
}

void LocModelRealizer::Realize(uint32_t delta_time) const
{
    if (!bShow)
        return;
    auto *pE = core.GetEntityPointer(eid_model);
    if (pE)
    {
        BOOL bLight0Enable;
        uint32_t dwLighting;
        if (lights)
        {
            rs->GetRenderState(D3DRS_LIGHTING, &dwLighting);
            rs->GetLightEnable(0, &bLight0Enable);

            // The sources around the model itself (wherever the camera is)
            lights->SetLightsAt(storm::math::pos(static_cast<MODEL *>(pE)->mtx));

            rs->SetRenderState(D3DRS_LIGHTING, TRUE);
            rs->LightEnable(0, TRUE);
        }

        pE->ProcessStage(Stage::realize, delta_time);
        if (lights)
        {
            lights->UnsetLights();
            rs->SetRenderState(D3DRS_LIGHTING, dwLighting);
            rs->LightEnable(0, bLight0Enable);
        }

        if (bCausticEnable)
        {
            // constants
            // 10 - (caustic scale, caustic frame, 0, 0)
            // 11 - diffuse
            // 12 - (fog density, fog_start, 0, 0)
            // 13 - (0, 0, 0, 0)

            fCausticDelta = fCausticFrame - static_cast<int32_t>(fCausticFrame);
            const auto vec1 = glm::vec4(fCausticScale, fCausticDelta, 0.0f, 0.0f);
            const auto vec2 = glm::vec4(fFogDensity, 0.0f, 0.0f, 0.0f);
            const auto vec3 = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            const auto vec4 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            const auto vec5 = glm::vec4(1.0f / fCausticDistance, 1.0f, 0.0f, 0.0f);
            rs->SetVertexShaderConstantF(10, reinterpret_cast<const float *>(&vec1), 1);
            rs->SetVertexShaderConstantF(11, reinterpret_cast<const float *>(&v4CausticColor), 1);
            rs->SetVertexShaderConstantF(12, reinterpret_cast<const float *>(&vec2), 1);
            rs->SetVertexShaderConstantF(13, reinterpret_cast<const float *>(&vec3), 1);
            rs->SetVertexShaderConstantF(14, reinterpret_cast<const float *>(&vec4), 1);
            rs->SetVertexShaderConstantF(15, reinterpret_cast<const float *>(&vec5), 1);

            rs->TextureSet(1, iCausticTex[static_cast<int32_t>(fCausticFrame) % 32]);
            rs->TextureSet(2, iCausticTex[(static_cast<int32_t>(fCausticFrame) + 1) % 32]);

            // draw caustics
            gs->SetCausticMode(true);
            pE->ProcessStage(Stage::realize, 0);
            gs->SetCausticMode(false);
        }
    }
}

// Messages
uint64_t LocModelRealizer::ProcessMessage(MESSAGE &message)
{
    switch (message.Long())
    {
    case 1:
        eid_model = message.EntityID();
        lights = (Lights *)message.Pointer();
        break;
    case 2:
        bShow = message.Long() != 0;
        break;
    }
    return 0;
}
