#include "xi_trigger.h"

#include "core.h"
#include "v_data.h"

#include <cmath>
#include <numbers>

CXI_TRIGGER::CXI_TRIGGER()
{
    m_nNodeType = NODETYPE_TRIGGER;
}

CXI_TRIGGER::~CXI_TRIGGER()
{
    ReleaseAll();
}

bool CXI_TRIGGER::Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VDX9RENDER *rs,
                       XYRECT &hostRect, XYPOINT &ScreenSize)
{
    if (!CINODE::Init(ini1, name1, ini2, name2, rs, hostRect, ScreenSize))
        return false;
    m_bSelected = false; // never takes focus or clicks
    return true;
}

void CXI_TRIGGER::LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2)
{
    char param[255];
    if (ReadIniString(ini1, name1, ini2, name2, "texture", param, sizeof(param), ""))
        m_idTex = m_rs->TextureCreate(param);
    m_fillTime = GetIniFloat(ini1, name1, ini2, name2, "time", 1.f);
    m_color = GetIniARGB(ini1, name1, ini2, name2, "color", 0xFFFFFFFF);
    m_radius = GetIniFloat(ini1, name1, ini2, name2, "radius", 16.f);
    m_offset.x = GetIniFloat(ini1, name1, ini2, name2, "offset_x", 0.f);
    m_offset.y = GetIniFloat(ini1, name1, ini2, name2, "offset_y", 0.f);
}

void CXI_TRIGGER::ReleaseAll()
{
    m_trigger.reset(); // the interface is going away: no script events from its teardown
    if (m_idTex >= 0)
        m_rs->TextureRelease(m_idTex);
    m_idTex = -1;
}

void CXI_TRIGGER::SetUsing(bool bUsing)
{
    if (!bUsing)
        Post(m_trigger.reset());
    CINODE::SetUsing(bUsing);
}

void CXI_TRIGGER::Draw(bool bSelected, uint32_t Delta_Time)
{
    const auto mouse = ptrOwner->GetMousePoint();
    const auto *node = ptrOwner->FindTriggerNode(mouse.x, mouse.y);
    const std::string hovered = node && node->m_nodeName ? node->m_nodeName : "";
    Post(m_trigger.update(hovered, Delta_Time * 0.001f, m_fillTime, [] {
        float delay = 0.f;
        auto *answer = core.Event("Event_GetTriggerDelay");
        if (!answer || !answer->Get(delay))
            throw std::runtime_error("interface TRIGGER: Event_GetTriggerDelay returned no number");
        return delay;
    }));
    if (const float progress = m_trigger.progress(m_fillTime); progress >= 0.f)
        DrawCircle(std::min(progress, 1.f));
}

void CXI_TRIGGER::Post(const std::vector<storm::xinterface::HoverTrigger::Emitted> &events)
{
    using Event = storm::xinterface::HoverTrigger::Event;
    for (const auto &e : events)
        switch (e.event)
        {
        case Event::Start:
            core.Event("Event_NodeStartTrigger", "s", e.node.c_str());
            break;
        case Event::End:
            core.Event("Event_NodeEndTrigger", "s", e.node.c_str());
            break;
        case Event::Cancel:
            core.Event("Event_NullTrigger");
            break;
        }
}

// A pie from 12 o'clock clockwise over the circle texture, `progress` of the full turn.
void CXI_TRIGGER::DrawCircle(float progress) const
{
    constexpr int kSegments = 48;
    const int segments = std::max(1, static_cast<int>(std::ceil(progress * kSegments)));
    const auto mouse = ptrOwner->GetMousePoint();
    const float cx = mouse.x + m_offset.x;
    const float cy = mouse.y + m_offset.y;
    XI_ONETEX_VERTEX v[kSegments * 3];
    for (int i = 0; i < segments; i++)
    {
        const float a0 = 2.f * std::numbers::pi_v<float> * progress * i / segments;
        const float a1 = 2.f * std::numbers::pi_v<float> * progress * (i + 1) / segments;
        const float corners[3][2] = {{0.f, 0.f}, {std::sin(a0), -std::cos(a0)}, {std::sin(a1), -std::cos(a1)}};
        for (int k = 0; k < 3; k++)
        {
            auto &vertex = v[i * 3 + k];
            vertex.pos = {cx + corners[k][0] * m_radius, cy + corners[k][1] * m_radius, 1.f};
            vertex.color = m_color;
            vertex.tu = 0.5f + corners[k][0] * 0.5f;
            vertex.tv = 0.5f + corners[k][1] * 0.5f;
        }
    }
    if (m_idTex >= 0)
        m_rs->TextureSet(0, m_idTex);
    m_rs->DrawPrimitiveUP(D3DPT_TRIANGLELIST, XI_ONETEX_FVF, segments, v, sizeof(XI_ONETEX_VERTEX), "iVideo");
}
