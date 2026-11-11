#pragma once

#include "../hover_trigger.hpp"
#include "../inode.h"

// TRIGGER (Caribbean Legend): runs the hover triggers of the interface's nodes (hover_trigger.hpp)
// and draws the filling circle next to the mouse. Ini: texture, time (seconds to fill), color,
// radius, offset_x / offset_y (circle centre from the mouse point).
class CXI_TRIGGER : public CINODE
{
  public:
    CXI_TRIGGER();
    ~CXI_TRIGGER() override;

    void Draw(bool bSelected, uint32_t Delta_Time) override;
    bool Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VDX9RENDER *rs, XYRECT &hostRect,
              XYPOINT &ScreenSize) override;
    void ReleaseAll() override;
    void SetUsing(bool bUsing) override;

    int CommandExecute(int wActCode) override
    {
        return 0;
    }
    bool IsClick(int buttonID, int32_t xPos, int32_t yPos) override
    {
        return false;
    }
    void MouseThis(float fX, float fY) override
    {
    }
    void ChangePosition(XYRECT &rNewPos) override
    {
        m_rect = rNewPos;
    }
    void SaveParametersToIni() override
    {
    }

  protected:
    void LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2) override;

  private:
    static void Post(const std::vector<storm::xinterface::HoverTrigger::Emitted> &events);
    void DrawCircle(float progress) const;

    storm::xinterface::HoverTrigger m_trigger;
    int32_t m_idTex = -1;
    float m_fillTime = 1.f;
    uint32_t m_color = 0xFFFFFFFF;
    float m_radius = 16.f;
    FXYPOINT m_offset{};
};
