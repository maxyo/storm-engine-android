#include <platform/android_ui.hpp>
#include "battle_land.h"
#include "battle_man_sign.h"
#include "core.h"
#include "message.h"
#include "string_compare.hpp"
#include "shared/battle_interface/msg_control.h"

BATTLE_LAND_INTERFACE::BATTLE_LAND_INTERFACE() : m_bShowCommandos(false)
{
    m_pRS = nullptr;
    m_pManSign = nullptr;
}

BATTLE_LAND_INTERFACE::~BATTLE_LAND_INTERFACE()
{
    Release();
}

bool BATTLE_LAND_INTERFACE::Init()
{
    m_pRS = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
    if (!m_pRS)
    {
        throw std::runtime_error("Can`t create render service");
    }

    SetShowParameters();
    return true;
}

void BATTLE_LAND_INTERFACE::Execute(uint32_t delta_time) const
{
    CONTROL_STATE cs;

    if (m_bShowCommandos && m_pManSign)
    {
        if (!m_pManSign->IsActive())
        {
            core.Controls->GetControlState(BI_COMMANDS_ACTIVATE_LAND, cs);
            if (cs.state == CST_ACTIVATED)
            {
                m_pManSign->SetActive(true);
            }
        }
        else
        {
            m_pManSign->MakeControl();
        }
    }
}

void BATTLE_LAND_INTERFACE::Realize(uint32_t delta_time)
{
    if (m_bShowCommandos)
    {
        m_pRS->MakePostProcess();
        if (m_pManSign)
            m_pManSign->Draw();

        m_Images.Draw();
#ifdef __ANDROID__
        std::string info;
        auto *texts = AttributesPointer ? AttributesPointer->GetAttributeClass("textinfo") : nullptr;
        if (texts) for (uint32_t i=0; i<texts->GetAttributesNum(); ++i) {
            auto *item=texts->GetAttributeClass(i);
            if (!item) continue;
            const auto name=item->GetThisName();
            const bool mobile=storm::iEquals(name,"islandname") || storm::iEquals(name,"villagename") ||
                storm::iEquals(name,"locationname") || storm::iEquals(name,"datatext") || storm::iEquals(name,"timetext");
            if (mobile) {
                const char *value=item->GetAttribute("text");
                if(value && *value) {
                    std::string_view display=value;
                    const auto colon=display.find(':');
                    if(colon!=std::string_view::npos) {
                        const auto prefix=display.substr(0,colon);
                        if(prefix=="Остров" || prefix=="Поселение" || prefix=="Дата" ||
                           storm::iEquals(prefix,"Island") || storm::iEquals(prefix,"Colony") || storm::iEquals(prefix,"Date")) {
                            display.remove_prefix(colon+1);
                            while(!display.empty() && display.front()==' ')display.remove_prefix(1);
                        }
                    }
                    if(!display.empty()){if(!info.empty())info+='\n';info+=display;}
                }
            } else if(i<m_TextInfo.size()) m_TextInfo[i].Print();
        }
        storm::android::publishWorldInfo(info);
#else
        BIUtils::PrintTextInfoArray(m_TextInfo);
#endif
    }
}

uint64_t BATTLE_LAND_INTERFACE::ProcessMessage(MESSAGE &message)
{
    switch (message.Long())
    {
    case MSG_BATTLE_LAND_START:
        if (m_pManSign)
            m_pManSign->SetUpdate();
        break;

    case MSG_BATTLE_LAND_END:
        EndShow();
        break;

    case BI_IN_SET_COMMAND_MODE:
        if (m_bShowCommandos && m_pManSign)
        {
            auto comMode = message.Long();
            auto startTextureNumber = message.Long();
            auto startPictureNumber = message.Long();
            auto characterNum = message.Long();
        }
        break;

    case MSG_BATTLE_LAND_SET_SHOW:
        m_bShowCommandos = (message.Long() != 0);
        break;

    case MSG_BATTLE_LAND_SET_MSGICONS:
        EnableMessageIcons(message.ScriptVariablePointer());
        break;

    case BI_MSG_ADD_NEWTEXTURE: {
        const std::string &param = message.String();
        const int hQ = message.Long();
        const int vQ = message.Long();
        if (m_pManSign)
            return m_pManSign->AddTexture(param.c_str(), hQ, vQ);
        return -1;
    }
    break;

    case MSG_BATTLE_LAND_MAKE_COMMAND: {
        const std::string &param = message.String();
        if (storm::iEquals(param, "cancel"))
        {
            if (m_pManSign)
                m_pManSign->ExecuteCommand(BI_MSG_COMMAND_DEACTIVATE);
        }
    }
    break;

    case BI_MSG_CARE_COMMANDLIST_UPDATE:
        m_pManSign->SetUpdate();
        break;
    }
    return 0;
}

void BATTLE_LAND_INTERFACE::SetParameters()
{
    UpdateCommandos();
    UpdateAlarm();
}

void BATTLE_LAND_INTERFACE::Release()
{
#ifdef __ANDROID__
    storm::android::publishWorldInfo("");
    storm::android::publishVitals("");
#endif
    m_pRS = nullptr;
    STORM_DELETE(m_pManSign);

    m_TextInfo.clear();
}

void BATTLE_LAND_INTERFACE::EndShow()
{
}

void BATTLE_LAND_INTERFACE::SetShowParameters()
{
    auto *const pA = AttributesPointer ? AttributesPointer->GetAttributeClass("Parameters") : nullptr;
    m_bShowCommandos = 0 != BIUtils::GetIntFromAttr(pA, "DoShowCommandos", true);

    m_pManSign = new BIManSign(GetId(), m_pRS);
    Assert(m_pManSign);
    m_pManSign->Init(AttributesPointer,
                     (AttributesPointer ? AttributesPointer->GetAttributeClass("ManSign") : nullptr));

    BIUtils::FillTextInfoArray(m_pRS, AttributesPointer ? AttributesPointer->GetAttributeClass("textinfo") : nullptr,
                               m_TextInfo);

    m_Images.Init(m_pRS, AttributesPointer ? AttributesPointer->GetAttributeClass("imageslist") : nullptr);
}

void BATTLE_LAND_INTERFACE::UpdateCommandos() const
{
    auto *pA = AttributesPointer ? AttributesPointer->GetAttributeClass("data") : nullptr;
    if (pA)
        pA = pA->GetAttributeClass("icons");
    if (!pA)
        return;
}

void BATTLE_LAND_INTERFACE::UpdateAlarm()
{
}

void BATTLE_LAND_INTERFACE::EnableMessageIcons(VDATA *pvdat)
{
}

void BATTLE_LAND_INTERFACE::DisableMessageIcons()
{
}

void BATTLE_LAND_INTERFACE::SetTextData()
{
}
