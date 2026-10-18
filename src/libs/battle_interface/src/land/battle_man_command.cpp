#include "battle_man_command.h"
#include "platform/android_land_diagnostics.hpp"
#include "platform/android_script_errors.hpp"
#include "core.h"
#include "shared/battle_interface/msg_control.h"
#include "vma.hpp"

BIManCommandList::BIManCommandList(entid_t eid, ATTRIBUTES *pA, VDX9RENDER *rs) : BICommandList(eid, pA, rs)
{
    Init();
}

BIManCommandList::~BIManCommandList()
{
    Release();
}

void BIManCommandList::FillIcons()
{
    int32_t nIconsQuantity = 0;

    if (m_nCurrentCommandMode & BI_COMMODE_COMMAND_SELECT)
        nIconsQuantity += CommandAdding();

    if (m_nCurrentCommandMode & BI_COMMODE_USER_ICONS)
        nIconsQuantity += UserIconsAdding();

    if (m_nCurrentCommandMode & BI_COMMODE_ABILITY_ICONS)
        nIconsQuantity += AbilityAdding();

    nIconsQuantity += AddCancelIcon();

    /*if( nIconsQuantity == 0 )
      AddCancelIcon();*/
}

void BIManCommandList::Init()
{
    BICommandList::Init();

    m_nIconShowMaxQuantity = 5;
    // boal -->
    auto *pA = m_pARoot;
    if (pA)
        pA = pA->GetAttributeClass("CommandList");
    if (pA)
    {
        m_nIconShowMaxQuantity = pA->GetAttributeAsDword("CommandMaxIconQuantity", m_nIconShowMaxQuantity);
    }
    // boal <--
}

void BIManCommandList::Release()
{
}

int32_t BIManCommandList::CommandAdding()
{
    m_fillingKind = "command";
    core.Event("BI_SetPossibleCommands", "l", m_nCurrentCommandCharacterIndex);
    int32_t retVal = 0;
    auto *pAttr = m_pARoot->GetAttributeClass("Commands");
    if (!pAttr)
        return 0;
    const size_t attrQuant = pAttr->GetAttributesNum();

    for (int32_t i = 0; i < attrQuant; i++)
    {
        auto *pA = pAttr->GetAttributeClass(i);
        if (pA == nullptr)
            continue; // no such attribute
        if (pA->GetAttributeAsDword("enable", 0) == 0)
            continue; // command not available
        const int32_t pictureNum = pA->GetAttributeAsDword("picNum", 0);
        const int32_t selPictureNum = pA->GetAttributeAsDword("selPicNum", 0);
        const int32_t texNum = pA->GetAttributeAsDword("texNum", -1);
        const char *eventName = pA->GetAttribute("event");
        const auto added = AddToIconList(texNum, pictureNum, selPictureNum, -1, -1, eventName, -1, nullptr, pA->GetAttribute("note"));
        retVal += added;
        if (added) {
            m_aUsedCommand.back().sLandSource = eventName ? eventName : "";
            m_aUsedCommand.back().sLandIdentity = pA->GetThisName();
        }
    }

    return retVal;
}

int32_t BIManCommandList::UserIconsAdding()
{
    int32_t retVal = 0;
    auto *pAttr = m_pARoot->GetAttributeClass("UserIcons");
    if (!pAttr)
        return 0;
    const size_t attrQuant = pAttr->GetAttributesNum();

    for (int32_t i = 0; i < attrQuant; i++)
    {
        auto *pA = pAttr->GetAttributeClass(i);
        if (pA == nullptr)
            continue; // no such attribute
        if (pA->GetAttributeAsDword("enable", 0) == 0)
            continue; // command not available
        const int32_t pictureNum = pA->GetAttributeAsDword("pic", 0);
        const int32_t selPictureNum = pA->GetAttributeAsDword("selpic", 0);
        const int32_t textureNum = pA->GetAttributeAsDword("tex", -1);
        const char *eventName = pA->GetAttribute("event");
        const char *potion = pA->GetAttribute("potion"), *location = pA->GetAttribute("location"), *name = pA->GetAttribute("name");
        m_fillingKind = name && *name && potion && *potion ? "potion" : name && *name && location && *location ? "travel" : "unknown";
        const auto added = AddToIconList(textureNum, pictureNum, selPictureNum, -1, -1, eventName, i + 1,
                                pA->GetAttribute("name"), pA->GetAttribute("note"));
        retVal += added;
        if (added) {
            auto &item = m_aUsedCommand.back();
            item.sLandSource = pA->GetAttribute("potion") ? pA->GetAttribute("potion") : pA->GetThisName();
            item.sLandIdentity = pA->GetThisName();
            for (auto key : {"name", "location", "locator", "potion"}) {
                const std::string value = pA->GetAttribute(key) ? pA->GetAttribute(key) : "";
                item.sLandIdentity += ":" + std::to_string(value.size()) + ":" + value;
            }
        }
    }

    return retVal;
}

int32_t BIManCommandList::AbilityAdding()
{
    m_fillingKind = "ability";
    core.Event("evntSetUsingAbility", "l", m_nCurrentCommandCharacterIndex);
    int32_t retVal = 0;
    auto *pAttr = m_pARoot->GetAttributeClass("AbilityIcons");
    if (!pAttr)
        return 0;
    const size_t attrQuant = pAttr->GetAttributesNum();

    for (int32_t i = 0; i < attrQuant; i++)
    {
        auto *pA = pAttr->GetAttributeClass(i);
        if (pA == nullptr)
            continue; // no such attribute
        if (pA->GetAttributeAsDword("enable", 0) == 0)
            continue; // command not available
        const int32_t pictureNum = pA->GetAttributeAsDword("picNum", 0);
        const int32_t selPictureNum = pA->GetAttributeAsDword("selPicNum", 0);
        const int32_t textureNum = pA->GetAttributeAsDword("texNum", -1);
        // retVal += AddToIconList( textureNum, pictureNum, selPictureNum, -1, -1, pA->GetThisName(), i, null,
        // pA->GetAttribute("note") );
        const char *eventName = pA->GetAttribute("event");
        const auto added = AddToIconList(textureNum, pictureNum, selPictureNum, -1, -1, eventName, -1, nullptr,
                                pA->GetAttribute("note"));
        retVal += added;
        if (added) {
            m_aUsedCommand.back().sLandSource = eventName ? eventName : "";
            m_aUsedCommand.back().sLandIdentity = pA->GetThisName();
        }
    }

    return retVal;
}

int32_t BIManCommandList::AddCancelIcon()
{
    m_fillingKind = "cancel";
    auto *pA = m_pARoot->GetAttributeClass("Commands");
    if (pA)
        pA = pA->GetAttributeClass("Cancel");
    if (!pA)
        return 0;
    const int32_t pictureNum = pA->GetAttributeAsDword("picNum", 0);
    const int32_t selPictureNum = pA->GetAttributeAsDword("selPicNum", 0);
    const int32_t textureNum = pA->GetAttributeAsDword("texNum", -1);
    return AddToIconList(textureNum, pictureNum, selPictureNum, -1, -1, pA->GetAttribute("event"), -1, nullptr,
                         pA->GetAttribute("note"));
}

#ifdef __ANDROID__
bool BIManCommandList::SelectAndroidLand(const std::string &id)
{
    const auto before = m_nSelectedCommandIndex;
    if (!SelectAndroidItem(id)) return false;
    if (before != m_nSelectedCommandIndex)
        core.Event("evntBattleCommandSound", "s", m_nSelectedCommandIndex < before ? "left" : "right");
    return true;
}
storm::android::LandCommandSource BIManCommandList::RefreshAndroidSource()
{
    const auto clear = [&] {
        if (auto *meta = m_pARoot ? m_pARoot->GetAttributeClass("AndroidLand") : nullptr)
            m_pARoot->DeleteAttributeClassX(meta);
    };
    // Never let a failed/partial event inherit an earlier target or completion marker.
    clear();
    const auto errorsBefore = storm::android::scriptErrorEpoch;
    core.Event("AndroidLandRefresh", "s", m_sCurrentCommandName.c_str());
    const auto missing = [&](const char *reason) {
        clear(); storm::android::traceLandAdapter(3, reason);
        return storm::android::LandCommandSource::Missing;
    };
    if (storm::android::scriptErrorEpoch != errorsBefore) return missing("script-error");
    auto *meta = m_pARoot ? m_pARoot->GetAttributeClass("AndroidLand") : nullptr;
    const auto field = [&](const char *name) -> std::optional<std::string_view> {
        auto *value = meta ? meta->GetAttributeClass(name) : nullptr;
        if (!value || !value->HasValue()) return {};
        return value->GetValue();
    };
    if (field("version") != "1") return missing("missing-fresh-version");
    const auto index = [&](const char *name) {
        auto value = field(name); int32_t parsed = -1;
        if (!value || value->empty()) return false;
        const auto result = std::from_chars(value->data(), value->data() + value->size(), parsed);
        return result.ec == std::errc() && result.ptr == value->data() + value->size() && parsed >= -1;
    };
    const auto pending = field("pendingAllowed"), reload = field("reload");
    if (!index("dialog")) return missing("invalid-dialog-target");
    if (!index("officer")) return missing("invalid-officer-target");
    if (!index("body")) return missing("invalid-body-target");
    if (pending != "0" && pending != "1") return missing("invalid-pending-allowed");
    if (!reload || !storm::android::landCommandsTextValid(*reload, 1024)) return missing("invalid-reload-target");
    const bool allowed = pending == "1";
    storm::android::traceLandAdapter(3, allowed ? "fresh-version-1-allowed" : "fresh-version-1-forbidden");
    return allowed ? storm::android::LandCommandSource::Ready : storm::android::LandCommandSource::Forbidden;
}
void BIManCommandList::ReadAndroidLand(storm::android::LandCommandsFrame &frame) const
{
    frame.mode = m_nCurrentCommandMode;
    frame.stage = m_sCurrentCommandName == "BI_FastReload" ? "travel" : m_sCurrentCommandName == "BI_ItemsUse" ? "potions" :
        m_sCurrentCommandName.empty() ? "commands" : "abilities";
    if (!m_sCurrentCommandName.empty()) {
        auto *root = m_pARoot ? m_pARoot->GetAttributeClass("Commands") : nullptr;
        if (root) for (size_t i = 0; i < root->GetAttributesNum(); ++i) {
            auto *entry = root->GetAttributeClass(i);
            const char *event = entry ? entry->GetAttribute("event") : nullptr;
            if (event && m_sCurrentCommandName == event)
                if (const char *note = entry->GetAttribute("note")) frame.pendingLabel = note;
        }
        if (frame.pendingLabel.empty()) frame.complete = false;
    }
    if (m_nCurrentCommandMode & ~(BI_COMMODE_COMMAND_SELECT | BI_COMMODE_USER_ICONS | BI_COMMODE_ABILITY_ICONS)) frame.complete = false;
    if (m_aUsedCommand.size() > 256) { frame.complete = false; return; }
    bool cancel = false;
    for (size_t i = 0; i < m_aUsedCommand.size(); ++i) {
        const auto &command = m_aUsedCommand[i];
        storm::android::LandCommandItem item;
        item.id = "i" + std::to_string(i); item.kind = command.sKind; item.label = command.sNote;
        item.source = command.sLandSource;
        item.cooldown = command.fCooldownFactor;
        item.enabled = command.nCooldownPictureIndex < 0 || command.fCooldownFactor >= 1.f;
        const auto part = [](const std::string &v) { return std::to_string(v.size()) + ":" + v; };
        item.identity = part(command.sLandIdentity) + part(command.sCommandName) + part(command.sLocName) +
            ":" + std::to_string(command.nTargetIndex) + ":" + std::to_string(command.nCharIndex);
        cancel |= item.kind == "cancel";
        if (item.label.empty() || item.kind == "unknown" || (item.source.empty() && item.kind != "cancel")) frame.complete = false;
        frame.items.push_back(std::move(item));
        if (static_cast<int32_t>(i) == m_nSelectedCommandIndex) frame.selectedItemId = frame.items.back().id;
    }
    frame.complete &= cancel;
}
#endif
