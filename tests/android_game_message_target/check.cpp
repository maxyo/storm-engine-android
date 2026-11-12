// Where a game log line leads, by the script function that wrote it (never by its text).
#include "platform/android_game_message_target.hpp"
#include <cassert>
#include <map>
#include <string>
#include <vector>

namespace
{
struct Call
{
    std::string function;
    std::vector<std::string> texts;
    std::vector<int> numbers;
    std::map<std::string, std::string> object; // attributes of argument 0
};

storm::android::ScriptCalls Stack(const std::vector<Call> &calls) // innermost last
{
    const auto find = [calls](const char *function) -> const Call * {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->function == function) return &*it;
        return nullptr;
    };
    storm::android::ScriptCalls out;
    out.depth = [calls](const char *function) {
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[calls.size() - 1 - i].function == function) return static_cast<int>(i);
        return -1;
    };
    out.text = [find](const char *function, unsigned index) -> std::optional<std::string> {
        const Call *call = find(function);
        return call && index < call->texts.size() ? std::optional<std::string>(call->texts[index]) : std::nullopt;
    };
    out.number = [find](const char *function, unsigned index) -> std::optional<int> {
        const Call *call = find(function);
        return call && index < call->numbers.size() ? std::optional<int>(call->numbers[index]) : std::nullopt;
    };
    out.attribute = [find](const char *function, unsigned index, const char *name) -> std::optional<std::string> {
        const Call *call = find(function);
        if (!call || index != 0 || !call->object.count(name)) return std::nullopt;
        return call->object.at(name);
    };
    return out;
}
} // namespace

int main()
{
    using storm::android::gameMessageTarget;
    // «Level Up» / ability points: the character's abilities
    assert(gameMessageTarget(Stack({{"AddCharacterExpToSkill"}, {"ApplayNewSkill", {}, {}, {{"index", "12"}}}, {"Log_SetStringToLog"}})) == "character:12");
    // «Logbook Update»: that quest; «Document Update»: that document
    assert(gameMessageTarget(Stack({{"AddQuestRecord"}, {"AddQuestRecordEx", {"Sharlie", "Sharlie", "1"}}, {"Log_SetStringToLog"}})) == "journal:Sharlie");
    assert(gameMessageTarget(Stack({{"AddQuestRecordInfo", {"Letter_1", "1"}}, {"Log_SetStringToLog"}})) == "journal:Letter_1");
    // «You take item»: the inventory; «You give item» (n < 0) leads nowhere
    assert(gameMessageTarget(Stack({{"TakeNItems", {"", "pistol1"}, {0, 0, 1}}, {"Log_Info"}})) == "items");
    assert(gameMessageTarget(Stack({{"TakeNItems", {"", "pistol1"}, {0, 0, -1}}, {"Log_Info"}})).empty());
    // money, trade, anything else: nowhere
    assert(gameMessageTarget(Stack({{"AddMoneyToCharacter"}, {"Log_Info"}})).empty());
    assert(gameMessageTarget(Stack({})).empty());
    // the innermost writer decides: a quest record written while an item is taken is the journal
    assert(gameMessageTarget(Stack({{"TakeNItems", {"", "letter"}, {0, 0, 1}}, {"AddQuestRecordEx", {"Delivery"}}, {"Log_SetStringToLog"}})) == "journal:Delivery");
    // a writer without the identity it needs is skipped for the next one out
    assert(gameMessageTarget(Stack({{"TakeNItems", {"", "x"}, {0, 0, 2}}, {"ApplayNewSkill"}})) == "items");
}
