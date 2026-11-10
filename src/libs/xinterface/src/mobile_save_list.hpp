#pragma once
#include "mobile_widget.hpp"
#include "v_data.h"
#include "platform/android_ui.hpp"
#include "v_file_service.h"
#include <array>
#include <cstdlib>
#include <ctime>
#include <map>
#include <optional>
#include <spdlog/spdlog.h>

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
// What the save list shows of a record. The game stores a description in every save
// (interface.c GetSaveDataString: "label@faces@HH:MM  DD.MM.YYYY@play time@language");
// SFLB_DoSaveFileData appends "@hero@location" of the game in progress. A record written
// before that has no hero; its label (the name the game gave it: the location) stands for
// the location. Published as the row's context: hero, location, gameDate (YYYY-MM-DD),
// gameTime (HH:MM), created (local YYYY-MM-DD HH:MM of the file), playMinutes.
struct SaveFacts
{
    std::string hero, location, gameDate, gameTime, created;
    int playMinutes = -1;
};

inline std::vector<std::string> SplitSaveDescription(const std::string &text)
{
    std::vector<std::string> fields(1);
    for (const char c : text)
        if (c == '@') fields.emplace_back(); else fields.back() += c;
    return fields;
}

// "[D days - ]H hours M min." (interface.c GetPlayTime; H counts all hours, D repeats them).
inline int SavePlayMinutes(const std::string &text)
{
    std::vector<int> numbers;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] >= '0' && text[i] <= '9')
        {
            numbers.push_back(std::atoi(text.c_str() + i));
            while (i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9') ++i;
        }
    if (numbers.size() < 2 || numbers.size() > 3) return -1;
    int hours = numbers[numbers.size() - 2];
    if (numbers.size() == 3 && hours < numbers[0] * 24) hours += numbers[0] * 24;
    return hours * 60 + numbers.back();
}

inline void SaveStamp(const std::string &stamp, SaveFacts &facts);

inline SaveFacts ParseSaveDescription(const std::string &text)
{
    SaveFacts facts;
    const auto fields = SplitSaveDescription(text);
    if (fields.size() < 5) return facts;
    facts.location = fields.size() >= 7 && !fields[6].empty() ? fields[6] : fields[0];
    if (fields.size() >= 7) facts.hero = fields[5];
    SaveStamp(fields[2], facts);
    facts.playMinutes = SavePlayMinutes(fields[3]);
    return facts;
}

// The game's "12:20  01.10.1654" → gameDate "1654-10-01", gameTime "12:20".
inline void SaveStamp(const std::string &stamp, SaveFacts &facts)
{
    const size_t colon = stamp.find(':'), dot = stamp.find('.');
    if (colon != std::string::npos && colon >= 1 && dot != std::string::npos && dot >= 2 &&
        stamp.find('.', dot + 1) != std::string::npos)
    {
        const int hour = std::atoi(stamp.c_str() + (colon >= 2 ? colon - 2 : 0)), minute = std::atoi(stamp.c_str() + colon + 1);
        const int day = std::atoi(stamp.c_str() + dot - 2), month = std::atoi(stamp.c_str() + dot + 1);
        const int year = std::atoi(stamp.c_str() + stamp.find('.', dot + 1) + 1);
        if (day >= 1 && day <= 31 && month >= 1 && month <= 12 && year > 0 && hour >= 0 && hour < 24 && minute >= 0 && minute < 60)
        {
            char date[16], clock[8];
            std::snprintf(date, sizeof(date), "%04d-%02d-%02d", year, month, day);
            std::snprintf(clock, sizeof(clock), "%02d:%02d", hour, minute);
            facts.gameDate = date;
            facts.gameTime = clock;
        }
    }
}

// "<hero>@<location>@<play time>@<HH:MM  DD.MM.YYYY>" of the game in progress, from the app
// script's handler (mobile-ui/PROGRAM/interface/android_save_identity.c); nothing without a
// game or handler.
inline std::optional<std::array<std::string, 4>> CurrentSaveIdentity()
{
    VDATA *result = core.Event("AndroidSaveIdentity");
    const char *text = nullptr;
    if (!result || !result->Get(text) || !text)
    {
        static bool reported = false;
        if (!std::exchange(reported, true))
            spdlog::error("save list: no AndroidSaveIdentity handler answered; records lose hero and location");
        return {};
    }
    const auto fields = SplitSaveDescription(text);
    if (fields.size() != 4 || fields[0].empty()) return {};
    return std::array<std::string, 4>{fields[0], fields[1], fields[2], fields[3]};
}

// The pause menu's line «<hero> · <location> · <date>» (V2-Menu-Game): the game in
// progress as the ANDROID_GAME text, its parts as context (hero, location, gameDate,
// gameTime). Asked once per opening of the menu.
inline std::optional<MobileWidget> GameIdentityWidget(uint64_t opening)
{
    static std::pair<uint64_t, std::optional<std::array<std::string, 4>>> cached{~0ull, {}};
    if (cached.first != opening) cached = {opening, CurrentSaveIdentity()};
    if (!cached.second) return {};
    const auto &identity = *cached.second;
    MobileWidget widget{"ANDROID_GAME", "text", identity[0] + " · " + identity[1], "", -1, -1, true};
    SaveFacts facts;
    SaveStamp(identity[3], facts);
    widget.context = {{"hero", identity[0]}, {"location", identity[1]}};
    if (!facts.gameDate.empty()) widget.context["gameDate"] = facts.gameDate, widget.context["gameTime"] = facts.gameTime;
    return widget;
}

// The description of a record file, read once per file time.
class SaveFactsCache
{
    struct Entry { std::time_t written = 0; SaveFacts facts; };
    std::map<std::string, Entry> entries_;
public:
    template <typename Read> const SaveFacts &get(const std::string &path, Read &&readDescription)
    {
        const std::time_t written = fio->_FileOrDirectoryExists(path.c_str()) ? fio->_ToTimeT(fio->_GetLastWriteTime(path.c_str())) : 0;
        auto found = entries_.find(path);
        if (found != entries_.end() && found->second.written == written) return found->second.facts;
        Entry entry{written, ParseSaveDescription(readDescription(path))};
        if (written)
        {
            std::tm local{};
            localtime_r(&written, &local);
            char created[24];
            std::strftime(created, sizeof(created), "%Y-%m-%d %H:%M", &local);
            entry.facts.created = created;
        }
        return entries_[path] = std::move(entry), entries_[path].facts;
    }
};

// Identity is bound to the complete current list, not to a reusable visual slot.
class SaveListCapability
{
    SaveListSnapshot published_;
    std::vector<std::string> ids_;
    uint64_t next_ = 0;
    SaveFactsCache facts_;
    std::pair<uint64_t, std::optional<std::array<std::string, 4>>> identity_{~0ull, {}};
public:
    struct Target { SaveListSnapshot owner; int index; std::string verb; };
    std::optional<Target> pending;
    bool sameList(const SaveListSnapshot &state) const
    {
        return published_.revision == state.revision && published_.profile == state.profile &&
            published_.saving == state.saving && published_.files == state.files;
    }
    // Fills a row's context from the record's description (see SaveFacts); `read` returns
    // the description stored in a file.
    template <typename Read>
    void collect(const SaveListSnapshot &state, bool enabled, std::vector<MobileWidget> &widgets, Read &&read)
    {
        if (!sameList(state))
        {
            published_ = state; ids_.clear();
            for (size_t i = 0; i < state.files.size() + (state.saving ? 1 : 0); ++i)
                ids_.push_back(std::to_string(++next_));
        }
        // The list itself, also when there is no record (V2-Save-Empty): value = record count.
        widgets.push_back(MobileWidget{"ANDROID_SAVES", "list", "", std::to_string(state.files.size()), -1, -1, enabled});
        for (size_t i = 0; i < ids_.size(); ++i)
        {
            const bool fresh = i == state.files.size();
            MobileWidget row{"ANDROID_SAVE_LIST", "save", fresh ? "" : state.files[i],
                state.selected == static_cast<int>(i) ? "1" : "0", static_cast<int>(i), static_cast<int>(i), enabled};
            row.rowId = ids_[i];
            if (enabled) { row.actions.push_back("activate"); if (!fresh) row.actions.push_back("delete"); }
            if (fresh) row.unavailableReason = "new";
            if (fresh)
            {
                // The record about to be written: the game in progress, no dates yet.
                if (identity_.first != state.revision) identity_ = {state.revision, CurrentSaveIdentity()};
                if (const auto &identity = identity_.second)
                {
                    row.context["hero"] = (*identity)[0];
                    row.context["location"] = (*identity)[1];
                    if (const int minutes = SavePlayMinutes((*identity)[2]); minutes >= 0)
                        row.context["playMinutes"] = std::to_string(minutes);
                }
            }
            else
            {
                const SaveFacts &facts = facts_.get("SAVE\\" + state.profile + "\\" + state.files[i], read);
                for (const auto &[key, value] : {std::pair<const char *, const std::string &>{"hero", facts.hero}, {"location", facts.location},
                                                 {"gameDate", facts.gameDate}, {"gameTime", facts.gameTime}, {"created", facts.created}})
                    if (!value.empty()) row.context[key] = value;
                if (facts.playMinutes >= 0) row.context["playMinutes"] = std::to_string(facts.playMinutes);
            }
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
