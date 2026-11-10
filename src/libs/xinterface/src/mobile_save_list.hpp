#pragma once
#include "mobile_widget.hpp"
#include "v_data.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>

namespace storm::mobile
{
// One record: the game keeps saves per profile folder (SAVE\<profile>\<file>). This port has
// no profiles (owner decision): the list shows the records of every profile together.
struct SaveRecord
{
    std::string profile, file;
    bool operator==(const SaveRecord &) const = default;
};
struct SaveListSnapshot
{
    uint64_t revision = 0;
    std::string profile;            // the game's current profile (its save list below)
    std::vector<std::string> files; // the current profile's records in the game's order
    int selected = -1;              // index into files
    bool saving = false;
    // The rows: when loading every profile's records, newest first; when saving the current
    // profile's (a save goes to the profile of the game in progress).
    std::vector<SaveRecord> records;
};
// The save list of the open save_load interface, read from the game script. The platform
// host defines it, because only the host knows which interface is on screen and its
// revision (Android: xinterface.cpp); portable code here only parses and validates.
std::optional<SaveListSnapshot> ReadSaveList();
// Makes `profile` the game's current profile through its own chooser (save_load.c
// ProcChooseProfile: SetCurrentProfile and the chooser closed); true when it is current.
// Defined by the platform host with ReadSaveList.
bool ChooseSaveProfile(const std::string &profile);
// What the save list shows of a record. The game stores a description in every save
// (interface.c GetSaveDataString: "label@faces@HH:MM  DD.MM.YYYY@play time@language");
// SFLB_DoSaveFileData appends "@hero@location" of the game in progress (mobile_save_facts). A record written
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
            char date[24], clock[16];
            std::snprintf(date, sizeof(date), "%04d-%02d-%02d", year, month, day);
            std::snprintf(clock, sizeof(clock), "%02d:%02d", hour, minute);
            facts.gameDate = date;
            facts.gameTime = clock;
        }
    }
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
    // The rows' identity is the record list, not the game's current profile (choosing a
    // record of another profile switches it).
    bool sameList(const SaveListSnapshot &state) const
    {
        return published_.revision == state.revision && published_.saving == state.saving && published_.records == state.records;
    }
    // `context(i)` gives row i its facts (mobile_save_facts.hpp SaveRowContext; i ==
    // records.size() is the new record).
    void collect(const SaveListSnapshot &state, bool enabled, std::vector<MobileWidget> &widgets,
                 const std::function<std::map<std::string, std::string>(size_t)> &context)
    {
        if (!sameList(state))
        {
            published_ = state; ids_.clear();
            for (size_t i = 0; i < state.records.size() + (state.saving ? 1 : 0); ++i)
                ids_.push_back(std::to_string(++next_));
        }
        // The list itself, also when there is no record (V2-Save-Empty): value = record count.
        widgets.push_back(MobileWidget{"ANDROID_SAVES", "list", "", std::to_string(state.records.size()), -1, -1, enabled});
        const SaveRecord chosen = state.selected >= 0 && static_cast<size_t>(state.selected) < state.files.size()
                                      ? SaveRecord{state.profile, state.files[static_cast<size_t>(state.selected)]}
                                      : SaveRecord{};
        for (size_t i = 0; i < ids_.size(); ++i)
        {
            const bool fresh = i == state.records.size();
            MobileWidget row{"ANDROID_SAVE_LIST", "save", fresh ? "" : state.records[i].file,
                !fresh && state.records[i] == chosen ? "1" : "0", static_cast<int>(i), static_cast<int>(i), enabled};
            row.rowId = ids_[i];
            if (enabled) { row.actions.push_back("activate"); if (!fresh) row.actions.push_back("delete"); }
            if (fresh) row.unavailableReason = "new";
            row.context = context(i);
            widgets.push_back(std::move(row));
        }
    }
    std::optional<Target> accept(const SaveListSnapshot &state, int index, const std::string &value) const
    {
        if (!sameList(state) || index < 0 || static_cast<size_t>(index) >= ids_.size()) return {};
        for (const auto *verb : {"activate", "delete"})
            if (value == std::string(verb) + "@" + std::to_string(state.revision) + ":" + ids_[index] &&
                (std::string(verb) != "delete" || static_cast<size_t>(index) < state.records.size()))
                return Target{state, index, verb};
        return {};
    }
    static bool current(const Target &target, const SaveListSnapshot &state)
    {
        return target.owner.revision == state.revision && target.owner.saving == state.saving &&
            target.owner.records == state.records && target.index >= 0 && static_cast<size_t>(target.index) <= state.records.size();
    }
    // The target's place in the game's list of the current profile: its record's index, the
    // new slot (files.size()) for the new record; -1 when the record is not in that list
    // (another profile is current).
    static int slot(const Target &target, const SaveListSnapshot &state)
    {
        const auto index = static_cast<size_t>(target.index);
        if (index == target.owner.records.size())
            return state.saving ? static_cast<int>(state.files.size()) : -1;
        const SaveRecord &record = target.owner.records[index];
        if (record.profile != state.profile) return -1;
        for (size_t i = 0; i < state.files.size(); ++i)
            if (state.files[i] == record.file) return static_cast<int>(i);
        return -1;
    }
};
inline SaveListCapability saveListCapability;

// Select through the original script so its ten-slot cache and delete shifting
// stay consistent, even when Android displays a row far beyond the native page.
inline bool SelectSaveTarget(const SaveListCapability::Target &target)
{
    auto state = ReadSaveList(); if (!state || !SaveListCapability::current(target, *state)) return false;
    // A record of another profile: that profile becomes the game's current one first.
    if (static_cast<size_t>(target.index) < target.owner.records.size() &&
        target.owner.records[static_cast<size_t>(target.index)].profile != state->profile)
    {
        if (!ChooseSaveProfile(target.owner.records[static_cast<size_t>(target.index)].profile)) return false;
        state = ReadSaveList(); if (!state) return false;
    }
    const int index = SaveListCapability::slot(target, *state);
    if (index < 0) return false;
    auto *first = static_cast<VDATA *>(core.GetScriptVariable("g_nFirstSaveIndex"));
    auto *selected = static_cast<VDATA *>(core.GetScriptVariable("g_nCurrentSaveIndex"));
    int32_t page = -1; if (!first || !first->Get(page) || !selected) return false;
    const int wanted = (index / 5) * 5;
    if (page != wanted)
    {
        const int lines = static_cast<int>(state->files.size()) / 5;
        const float position = lines ? (wanted / 5 == lines ? 1.f : (wanted / 5 + .25f) / lines) : 0.f;
        core.Event("ScrollPosChange", "fs", position, "SAVESCROLL");
        if (!first->Get(page) || page != wanted) return false;
    }
    // The delayed initial-selection event may have changed only this index.
    // Restoring it before dispatch prevents confirmation from targeting another save.
    const std::string node = "SAVEIMG" + std::to_string(index - page + 1);
    core.Event("eventSaveClick", "ls", ACTION_MOUSECLICK, node.c_str());
    int32_t actual = -1;
    return selected->Get(actual) && actual == index;
}
}
namespace storm::mobile
{
inline bool RestoreSaveTarget(const SaveListCapability::Target &target)
{
    auto state = ReadSaveList();
    if (!state || !SaveListCapability::current(target, *state)) return false;
    const int index = SaveListCapability::slot(target, *state);
    auto *selected = static_cast<VDATA *>(core.GetScriptVariable("g_nCurrentSaveIndex"));
    auto *first = static_cast<VDATA *>(core.GetScriptVariable("g_nFirstSaveIndex"));
    int32_t page = -1;
    if (index < 0 || !selected || !first || !first->Get(page) || index < page || index >= page + 10) return false;
    selected->Set(static_cast<int32_t>(index));
    return true;
}
}
