#include "mobile_widget.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <strings.h>

namespace storm { bool iEquals(const char *a, const char *b) { return strcasecmp(a, b) == 0; } }

// Execute the production MobileCollect/MobileApply methods with a setter spy.
// Rendering and script events remain owned by the existing SetNewValue method.
struct CXI_SLIDELINE
{
    const char *m_nodeName = "SOUND_SLIDE";
    bool m_bUse = true, m_bSelected = true, m_bClickable = true;
    bool m_bLockedNode = false, m_bLockStatus = false;
    struct { int top = 0, left = 0; } m_rect;
    int m_nCurValue = 50, m_nGrateQuantity = 100, m_nMinValue = -1, m_nMaxValue = -1;
    int setterCalls = 0;
    void SetNewValue(int value) { ++setterCalls; m_nCurValue = value; }
    void MobileCollect(std::vector<MobileWidget> &) const;
    bool MobileApply(int, const std::string &);
};
#include "slider_methods.inc"
#include "slider_conversion.inc"

int main()
{
    assert(static_cast<int>(0.59f * 100) == 58); // Original regression.
    for (int initial = 0; initial <= 100; ++initial)
    {
        int position = initial;
        for (int reopen = 0; reopen < 20; ++reopen)
        {
            char saved[64];
            std::snprintf(saved, sizeof(saved), "%.7f", static_cast<float>(position) / 100);
            position = SliderPositionFromNormalized(std::strtof(saved, nullptr), 100);
            assert(position == initial);
        }
    }
    assert(SliderPositionFromNormalized(0.595f, 100) == 60);
    assert(SliderPositionFromNormalized(0.594f, 100) == 59);
    assert(SliderPositionFromNormalized(std::numeric_limits<float>::quiet_NaN(), 100) == 0);
    assert(SliderPositionFromNormalized(std::numeric_limits<float>::max(), 100) == INT32_MAX);
    int value = -1;
    assert(MobileSliderSetPosition("set@7:100", 7, value) && value == 100);
    for (const std::string &bad : {"set@6:50", "set@8:50", "set@7:-1", "set@7:+1", "set@7:1.5",
             "set@7: 1", "set@7:1 ", "set@7:", "set@7:1:2", "set@7:2147483648",
             "set@18446744073709551616:1", "50", "press"})
        assert(!MobileSliderSetPosition(bad, 7, value));
    assert(!MobileSliderPosition(std::string("12\0tail", 7), 0, 100, value));
    assert(MobileSliderSetPosition("set@18446744073709551615:0", UINT64_MAX, value));

    CXI_SLIDELINE slider;
    std::vector<MobileWidget> widgets;
    slider.MobileCollect(widgets);
    assert(widgets.size() == 1 && widgets[0].value == "50" && widgets[0].min == 0 && widgets[0].max == 100);
    assert(widgets[0].enabled && widgets[0].step == 1 && widgets[0].actions == std::vector<std::string>{"set"});
    assert(slider.MobileApply(-1, "0") && slider.MobileApply(-1, "100") && slider.setterCalls == 2);
    for (auto *flag : {&slider.m_bSelected, &slider.m_bClickable, &slider.m_bUse})
    {
        *flag = false;
        assert(!slider.MobileApply(-1, "20"));
        *flag = true;
    }
    for (auto *flag : {&slider.m_bLockedNode, &slider.m_bLockStatus})
    {
        *flag = true;
        assert(!slider.MobileApply(-1, "20"));
        *flag = false;
    }
    assert(slider.setterCalls == 2);
    slider.m_nMinValue = 20; slider.m_nMaxValue = 80;
    assert(!slider.MobileApply(-1, "19") && !slider.MobileApply(-1, "81") && !slider.MobileApply(0, "50"));
    assert(slider.MobileApply(-1, "20") && slider.MobileApply(-1, "80"));
    slider.m_nMaxValue = 200;
    assert(!slider.MobileApply(-1, "101"));
    slider.m_nMinValue = 101;
    assert(!slider.MobileApply(-1, "100"));
    slider.m_nMinValue = -1; slider.m_nGrateQuantity = 0;
    assert(!slider.MobileApply(-1, "0"));
    slider.m_nGrateQuantity = 100;
    for (const char *name : {"GAMMA_SLIDE", "bright_slide", "CONTRAST_SLIDE"})
    {
        slider.m_nodeName = name; widgets.clear(); slider.MobileCollect(widgets);
        assert(!widgets[0].enabled && widgets[0].actions.empty());
        assert(widgets[0].unavailableReason == "android_color_correction");
        assert(!slider.MobileApply(-1, "50"));
    }
    assert(slider.setterCalls == 4);
    MobileWidget radio{"HERB_CHECKBOX", "check", "High", "1", 0};
    radio.exclusive = true; widgets.push_back(radio);
    std::cout << MobileWidgetJson("options", widgets, 7, 1, "russian") << '\n';
}
