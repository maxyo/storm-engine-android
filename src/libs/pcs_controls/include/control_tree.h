#pragma once

#include "controls.h"
#include "vma.hpp"
#include <string>
#include <vector>
#include <functional>

struct ControlTree
{
  public:
    struct ControlChild
    {
        ControlChild();
        ~ControlChild();

        void Process(float fDeltaTime, ControlTree *pControlTree);
        bool ControlInAction(ControlTree *pControlTree, const char *pcControlName, int32_t _nLayer);

        int32_t index;
        std::string sControlName;
        std::vector<ControlChild> aChild;
        bool bActive;
        bool bWaitReleaseControl;
        float fTimeOut;
        float fCurTime;
        std::string sOutControlName;
        int32_t nLayer;
    };

    struct OutControlInfo
    {
        std::string sControlName;
        CONTROL_STATE_TYPE state;
    };

  public:
    ControlTree();
    ~ControlTree();

    void Process();

    int32_t AddControlChild(int32_t nParentIdx, const char *pcControlName, const char *pcOutControlName, float fTimeOut);
    CONTROL_STATE_TYPE CheckControlActivated(const char *pcControlName);

    bool AddOutControl(const char *pcOutControlName, bool isActive);
    void ControlInAction(const char *pcControlName, int32_t nLayer);
    bool ExcludeControlFromActive(const char *pcControlName);
    bool HasAvailableOutput(const std::string &output, const std::function<bool(const std::string &)> &available) const
    {
        const auto visit = [&](const auto &self, const ControlChild &node, bool parents) -> bool {
            const bool enabled = parents && (node.sControlName.empty() || available(node.sControlName));
            if (!enabled) return false;
            if (node.sOutControlName == output) return true;
            for (const auto &child : node.aChild) if (self(self, child, enabled)) return true;
            return false;
        };
        return visit(visit, m_RootControl, true);
    }

  protected:
    void Init();
    void Release();
    ControlChild *FindControlChild(int32_t idx);
    ControlChild *FindControlChild(int32_t idx, ControlChild *pParent);

  protected:
    int32_t m_nControlsNum;
    ControlChild m_RootControl;

    std::vector<OutControlInfo> m_aOutControlList;
};
