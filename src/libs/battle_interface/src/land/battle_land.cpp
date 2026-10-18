#include <platform/android_ui.hpp>
#include "battle_land.h"
#include "platform/android_land_diagnostics.hpp"
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

#ifdef __ANDROID__
#include "../../../xinterface/src/string_service.h"
#include "../../../core/src/data.h"
storm::android::LandCommandSource BATTLE_LAND_INTERFACE::ReadAndroidLand(storm::android::LandCommandsFrame &frame)
{
    storm::android::traceLandAdapter(4, std::string("show-commandos=") + (m_bShowCommandos ? "1" : "0") +
        " man-sign=" + (m_pManSign ? "1" : "0"));
    if (!m_bShowCommandos) return storm::android::LandCommandSource::Forbidden;
    return m_pManSign ? m_pManSign->ReadAndroidLand(frame) : storm::android::LandCommandSource::Missing;
}
bool BATTLE_LAND_INTERFACE::ExecuteAndroidLand(const storm::android::LandCommandsRequest &request)
{
    return m_bShowCommandos && m_pManSign && m_pManSign->ExecuteAndroidLand(request);
}
void BATTLE_LAND_INTERFACE::CloseAndroidLand()
{
    if (m_pManSign) m_pManSign->CloseAndroidLand();
}
namespace storm::android {
namespace {
BATTLE_LAND_INTERFACE *landOwner()
{
    auto *value = static_cast<VDATA *>(core.GetScriptVariable("objLandInterface"));
    return value ? dynamic_cast<BATTLE_LAND_INTERFACE *>(core.GetEntityPointer(value->GetEntityID())) : nullptr;
}
std::string landOwnerKey(BATTLE_LAND_INTERFACE *battle)
{
    const auto player = core.GetEntityId("Player");
    auto *character = core.Entity_GetAttributePointer(player);
    if (!character) return {};
    const char *location = character->GetAttribute("location");
    std::string key = std::to_string(battle->GetId()) + ":" + std::to_string(player) + ":" +
        std::to_string(core.GetEntityId("location")) + ":" + (location ? location : "");
    const auto add = [&](ATTRIBUTES *root, const char *path) {
        auto *a = root ? root->FindAClass(root, path) : nullptr;
        const std::string v = a && a->HasValue() ? std::string(a->GetValue()) : "";
        key += ":" + std::to_string(v.size()) + ":" + v;
    };
    for (const auto *path : {"id", "index", "dialogready", "boxname", "activeItem", "activeItem.pick"}) add(character, path);
    auto *meta = battle->AttributesPointer ? battle->AttributesPointer->GetAttributeClass("AndroidLand") : nullptr;
    add(meta, "reload");
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    for (const auto *path : {"dialog", "officer", "body"}) {
        add(meta, path);
        const int index = meta ? static_cast<int>(meta->GetAttributeAsDword(path, -1)) : -1;
        if (characters && index >= 0 && static_cast<uint32_t>(index) < characters->GetElementsNum()) {
            auto *entry = characters->GetArrayElement(index);
            key += ":" + std::to_string(entry ? entry->GetEntityID() : invalid_entity);
            add(characters->GetAClass(index), "id");
        }
    }
    return key;
}
}
void refreshLandCommands(bool ready)
{
    auto *battle = landOwner();
    if (!ready || !battle) {
        traceLandAdapter(2, !ready ? "readiness-blocked" : "no-land-owner");
        if (battle) battle->CloseAndroidLand();
        landCommands.update({}, {}); return;
    }
    LandCommandsFrame frame;
    if (auto *strings = static_cast<VSTRSERVICE *>(core.GetService("STRSERVICE")))
        if (const auto *language = strings->GetLanguage()) frame.language = language;
    const auto source = battle->ReadAndroidLand(frame);
    if (source != LandCommandSource::Ready) {
        traceLandAdapter(2, source == LandCommandSource::Missing ? "missing-source" : "forbidden");
        if (landCommands.releaseUnavailableSource(source)) battle->CloseAndroidLand();
        return;
    }
    const auto key = landOwnerKey(battle);
    if (key.empty()) { traceLandAdapter(2, "empty-owner-key"); battle->CloseAndroidLand(); landCommands.update({}, {}); return; }
    const auto ownerKey = key + ":list:" + frame.ownerIdentity;
    const auto tick = SDL_GetTicks();
    traceLandAdapter(2, "ready owner=" + std::to_string(battle->GetId()) + " active=" + (frame.active ? "1" : "0") +
        " complete=" + (frame.complete ? "1" : "0") + " items=" + std::to_string(frame.items.size()));
    landCommands.update(ownerKey, std::move(frame), tick);
    // Inspect fresh support before timeout: a vanished adapter restores native
    // fallback, whereas a still-known pending transition closes quietly.
    if (landCommands.expireHandoff(tick)) {
        battle->CloseAndroidLand();
        refreshLandCommands(ready);
    }
}
bool executeLandCommand(const LandCommandsRequest &request)
{
    if (request.scene != landCommands.sceneEpoch() || request.revision != landCommands.revision() ||
        request.activity != landCommands.activityGeneration() || !landCommands.frame().available) return false;
    auto *battle = landOwner(); if (!battle) return false;
    // Rebuild original predicates and child entries immediately before accepting an action.
    refreshLandCommands(true);
    if (request.scene != landCommands.sceneEpoch() || request.revision != landCommands.revision()) return false;
    const auto &frame = landCommands.frame();
    if (request.verb == "activateItem" && (request.id != frame.selectedItemId ||
        std::none_of(frame.items.begin(), frame.items.end(), [&](const auto &item) { return item.id == request.id && item.enabled; }))) return false;
    const bool result = battle->ExecuteAndroidLand(request);
    landCommands.finishAndroidOperation(request, result, SDL_GetTicks()); return result;
}
void closeLandCommands()
{
    if (auto *battle = landOwner()) battle->CloseAndroidLand();
    landCommands.update({}, {});
}
}
#endif
