#pragma once
#include "mobile_widget.hpp"
#include "v_data.h"
#include "platform/android_ui.hpp"
#include <optional>

namespace storm::mobile
{
struct SaveListSnapshot
{
    uint64_t revision = 0;
    std::string profile;
    std::vector<std::string> files;
    int selected = -1;
    bool saving = false;
};
inline std::optional<SaveListSnapshot> ReadSaveList()
{
    if (storm::android::mobileScreen != "save_load") return {};
    const auto number = [](const char *name, int32_t &value) {
        auto *v = static_cast<VDATA *>(core.GetScriptVariable(name)); return v && v->Get(value);
    };
    auto *profile = static_cast<VDATA *>(core.GetScriptVariable("currentProfile"));
    auto *container = static_cast<VDATA *>(core.GetScriptVariable("g_oSaveContainer"));
    const char *name = nullptr; int32_t count, selected, saving;
    if (!profile || !profile->Get(name) || !name || !container || !container->GetAClass() ||
        !number("g_nSaveQuantity", count) || !number("g_nCurrentSaveIndex", selected) ||
        !number("bThisSave", saving) || count < 0 || count > 10000) return {};
    SaveListSnapshot result{storm::android::mobileScreenRevision, name, {}, selected, saving != 0};
    for (int i = 0; i < count; ++i)
    {
        auto *entry = container->GetAClass()->GetAttributeClass(("s" + std::to_string(i)).c_str());
        const char *file = entry ? entry->GetAttribute("savename") : nullptr;
        if (!file || !*file) return {};
        result.files.emplace_back(file);
    }
    return result;
}
// Identity is bound to the complete current list, not to a reusable visual slot.
class SaveListCapability
{
    SaveListSnapshot published_;
    std::vector<std::string> ids_;
    uint64_t next_ = 0;
public:
    struct Target { SaveListSnapshot owner; int index; std::string verb; };
    std::optional<Target> pending;
    bool sameList(const SaveListSnapshot &state) const
    {
        return published_.revision == state.revision && published_.profile == state.profile &&
            published_.saving == state.saving && published_.files == state.files;
    }
    void collect(const SaveListSnapshot &state, bool enabled, std::vector<MobileWidget> &widgets)
    {
        if (!sameList(state))
        {
            published_ = state; ids_.clear();
            for (size_t i = 0; i < state.files.size() + (state.saving ? 1 : 0); ++i)
                ids_.push_back(std::to_string(++next_));
        }
        for (size_t i = 0; i < ids_.size(); ++i)
        {
            const bool fresh = i == state.files.size();
            MobileWidget row{"ANDROID_SAVE_LIST", "save", fresh ? "" : state.files[i],
                state.selected == static_cast<int>(i) ? "1" : "0", static_cast<int>(i), static_cast<int>(i), enabled};
            row.rowId = ids_[i];
            if (enabled) { row.actions.push_back("activate"); if (!fresh) row.actions.push_back("delete"); }
            if (fresh) row.unavailableReason = "new";
            widgets.push_back(std::move(row));
        }
    }
    std::optional<Target> accept(const SaveListSnapshot &state, int index, const std::string &value) const
    {
        if (!sameList(state) || index < 0 || static_cast<size_t>(index) >= ids_.size()) return {};
        for (const auto *verb : {"activate", "delete"})
            if (value == std::string(verb) + "@" + std::to_string(state.revision) + ":" + ids_[index] &&
                (std::string(verb) != "delete" || static_cast<size_t>(index) < state.files.size()))
                return Target{state, index, verb};
        return {};
    }
    static bool current(const Target &target, const SaveListSnapshot &state)
    {
        return target.owner.revision == state.revision && target.owner.profile == state.profile &&
            target.owner.saving == state.saving && target.owner.files == state.files &&
            target.index >= 0 && static_cast<size_t>(target.index) <= state.files.size();
    }
};
inline SaveListCapability saveListCapability;

// Select through the original script so its ten-slot cache and delete shifting
// stay consistent, even when Android displays a row far beyond the native page.
inline bool SelectSaveTarget(const SaveListCapability::Target &target)
{
    auto state = ReadSaveList(); if (!state || !SaveListCapability::current(target, *state)) return false;
    auto *first = static_cast<VDATA *>(core.GetScriptVariable("g_nFirstSaveIndex"));
    auto *selected = static_cast<VDATA *>(core.GetScriptVariable("g_nCurrentSaveIndex"));
    int32_t page = -1; if (!first || !first->Get(page) || !selected) return false;
    const int wanted = (target.index / 5) * 5;
    if (page != wanted)
    {
        const int lines = static_cast<int>(state->files.size()) / 5;
        const float position = lines ? (wanted / 5 == lines ? 1.f : (wanted / 5 + .25f) / lines) : 0.f;
        core.Event("ScrollPosChange", "fs", position, "SAVESCROLL");
        if (!first->Get(page) || page != wanted) return false;
    }
    // The delayed initial-selection event may have changed only this index.
    // Restoring it before dispatch prevents confirmation from targeting another save.
    const std::string node = "SAVEIMG" + std::to_string(target.index - page + 1);
    core.Event("eventSaveClick", "ls", ACTION_MOUSECLICK, node.c_str());
    int32_t actual = -1;
    return selected->Get(actual) && actual == target.index;
}
}
namespace storm::mobile
{
inline bool RestoreSaveTarget(const SaveListCapability::Target &target)
{
    auto state = ReadSaveList();
    if (!state || !SaveListCapability::current(target, *state)) return false;
    auto *selected = static_cast<VDATA *>(core.GetScriptVariable("g_nCurrentSaveIndex"));
    auto *first = static_cast<VDATA *>(core.GetScriptVariable("g_nFirstSaveIndex"));
    int32_t page = -1;
    if (!selected || !first || !first->Get(page) || target.index < page || target.index >= page + 10) return false;
    selected->Set(static_cast<int32_t>(target.index));
    return true;
}
}
