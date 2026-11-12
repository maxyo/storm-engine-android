#pragma once
// Where a line of the game's log leads (a tap on its toast opens that screen), told by the game
// function that wrote it, never by its text: the line is written while that function runs, so the
// script call stack names it and its arguments say what it is about. TEHO 1.7.3 scripts:
//   ApplayNewSkill(ref chref, …)              «Level Up», «Personal/Ship abilities + 1»: the
//                                              character's abilities (perks) — "character:<index>"
//   AddQuestRecordEx(string idQuest, …),       «Logbook Update», «Document Update»: that quest in
//   AddQuestRecordInfo(string idQuest, …)       the journal — "journal:<quest id>"
//   TakeNItems(ref chref, string item, int n)  «You take item» (n > 0): the inventory — "items"
// Anything else (money, trade, the rest) leads nowhere: "". The innermost of these calls decides.
#include <functional>
#include <optional>
#include <string>

namespace storm::android
{
// What the line's writer is, read from the running script (battle_interface adapts the
// interpreter's call stack; tests give a fake).
struct ScriptCalls
{
    std::function<int(const char *function)> depth; // 0 = innermost call, -1 = not running
    std::function<std::optional<std::string>(const char *function, unsigned index)> text;
    std::function<std::optional<int>(const char *function, unsigned index)> number;
    // attribute `name` of the object argument `index` (a `ref` character: its "index")
    std::function<std::optional<std::string>(const char *function, unsigned index, const char *name)> attribute;
};

inline std::string gameMessageTarget(const ScriptCalls &calls)
{
    std::string target;
    int nearest = -1;
    const auto consider = [&](const char *function, auto &&make) {
        const int depth = calls.depth(function);
        if (depth < 0 || (nearest >= 0 && depth >= nearest)) return;
        std::string made = make(function);
        if (made.empty()) return;
        nearest = depth;
        target = std::move(made);
    };
    consider("ApplayNewSkill", [&](const char *function) {
        const auto index = calls.attribute(function, 0, "index");
        return index && !index->empty() ? "character:" + *index : std::string();
    });
    for (const char *function : {"AddQuestRecordEx", "AddQuestRecordInfo"})
        consider(function, [&](const char *name) {
            const auto quest = calls.text(name, 0);
            return quest && !quest->empty() ? "journal:" + *quest : std::string();
        });
    consider("TakeNItems", [&](const char *function) {
        const auto count = calls.number(function, 2);
        return count && *count > 0 ? std::string("items") : std::string();
    });
    return target;
}
} // namespace storm::android
