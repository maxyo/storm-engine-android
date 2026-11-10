// mobile_save_list.hpp expects the engine's core and interface messages included first.
#include "core.h"
#include "shared/interface/messages.h"
#include "v_data.h"
#include "v_file_service.h"

#include "mobile_save_facts.hpp"

#include <spdlog/spdlog.h>

#include <ctime>
#include <filesystem>
#include <utility>

namespace storm::mobile
{
std::optional<std::array<std::string, 4>> CurrentSaveIdentity()
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

std::optional<MobileWidget> GameIdentityWidget(uint64_t opening)
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
namespace
{
class SaveFactsCache
{
    struct Entry { std::time_t written = 0; SaveFacts facts; };
    std::map<std::string, Entry> entries_;
public:
    const SaveFacts &get(const std::string &path, const SaveDescriptionReader &readDescription)
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
SaveFactsCache saveFacts;
} // namespace

std::map<std::string, std::string> SaveRowContext(const SaveListSnapshot &state, size_t index, const SaveDescriptionReader &read)
{
    std::map<std::string, std::string> context;
    if (index >= state.files.size())
    {
        // The record about to be written: the game in progress, no dates yet.
        static std::pair<uint64_t, std::optional<std::array<std::string, 4>>> identity{~0ull, {}};
        if (identity.first != state.revision) identity = {state.revision, CurrentSaveIdentity()};
        if (const auto &game = identity.second)
        {
            context["hero"] = (*game)[0];
            context["location"] = (*game)[1];
            if (const int minutes = SavePlayMinutes((*game)[2]); minutes >= 0) context["playMinutes"] = std::to_string(minutes);
        }
        return context;
    }
    const SaveFacts &facts = saveFacts.get("SAVE\\" + state.profile + "\\" + state.files[index], read);
    for (const auto &[key, value] : {std::pair<const char *, const std::string &>{"hero", facts.hero}, {"location", facts.location},
                                     {"gameDate", facts.gameDate}, {"gameTime", facts.gameTime}, {"created", facts.created}})
        if (!value.empty()) context[key] = value;
    if (facts.playMinutes >= 0) context["playMinutes"] = std::to_string(facts.playMinutes);
    return context;
}

std::optional<MobileWidget> ContinueWidget(uint64_t opening, const SaveDescriptionReader &read)
{
    static std::pair<uint64_t, std::string> newest{~0ull, {}};
    if (newest.first != opening)
    {
        auto *profile = static_cast<VDATA *>(core.GetScriptVariable("PlayerProfile"));
        const char *profileName = profile && profile->GetAClass() ? profile->GetAClass()->GetAttribute("name") : nullptr;
        if (!profileName || !*profileName) return {};
        newest = {opening, {}};
        const std::string folder = std::string("SAVE\\") + profileName;
        const char *savePath = folder.c_str();
        std::filesystem::file_time_type best{};
        for (const auto &path : fio->_GetFsPathsByMask(savePath, nullptr, true))
        {
            const auto utf8 = path.filename().u8string();
            const std::string name(utf8.begin(), utf8.end());
            std::error_code error;
            const auto written = std::filesystem::last_write_time(path, error);
            const auto ends = [&name](const std::string &suffix) {
                return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
            };
            if (error || ends(".storm-tmp") || ends(".storm-bak") || (!newest.second.empty() && written <= best)) continue;
            best = written;
            newest.second = std::string(savePath) + "\\" + name;
        }
    }
    if (newest.second.empty()) return {};
    const SaveFacts &facts = saveFacts.get(newest.second, read);
    if (facts.location.empty()) return {};
    MobileWidget widget{"ANDROID_CONTINUE", "text", facts.hero.empty() ? facts.location : facts.hero + " · " + facts.location, "", -1, -1, true};
    widget.context = {{"location", facts.location}};
    if (!facts.hero.empty()) widget.context["hero"] = facts.hero;
    return widget;
}
} // namespace storm::mobile
