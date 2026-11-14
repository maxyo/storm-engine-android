// Shared parts of the hero section screens, see hero.h.
#include "hero.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace storm::ui::screens::hero
{

int Number(const std::string &text)
{
    std::string digits;
    bool minus = false;
    for (const char c : text)
    {
        if (c >= '0' && c <= '9')
            digits += c;
        else if (digits.empty() && c == '-')
            minus = true;
        else if (!digits.empty() && c != ' ')
            break;
    }
    const int value = digits.empty() ? 0 : std::atoi(digits.c_str());
    return minus ? -value : value;
}

std::string Trim(std::string text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// Lower case for the game's upper-case words ("ОТЛИЧНОЕ"): ASCII, Latin-1 and Cyrillic.
std::string LowerText(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size();)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80)
        {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && i + 1 < text.size())
        {
            unsigned code = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            if ((code >= 0x410 && code <= 0x42F) || (code >= 0xC0 && code <= 0xDE && code != 0xD7))
                code += 0x20;
            else if (code >= 0x400 && code <= 0x40F)
                code += 0x50;
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
            i += 2;
            continue;
        }
        out += text[i++];
    }
    return out;
}

std::string Field(const Widget *fact, const char *key)
{
    if (!fact)
        return {};
    const auto it = fact->context.find(key);
    return it == fact->context.end() ? std::string() : it->second;
}

std::string Decimal(const std::string &text, const std::string &language)
{
    if (language == "english")
        return text;
    std::string out = text;
    for (size_t i = 1; i + 1 < out.size(); ++i)
        if (out[i] == '.' && out[i - 1] >= '0' && out[i - 1] <= '9' && out[i + 1] >= '0' && out[i + 1] <= '9')
            out[i] = ',';
    return out;
}

Json Portrait(const std::string &face)
{
    return face.empty() ? Json(nullptr) : Json{{"kind", "portrait"}, {"texture", "INTERFACES/PORTRAITS/128/face_" + face}};
}

std::string Thousands(const std::string &digits)
{
    const int value = Number(digits);
    std::string raw = std::to_string(std::abs(value)), out;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        if (i && (raw.size() - i) % 3 == 0)
            out += "\xC2\xA0";
        out += raw[i];
    }
    return (value < 0 ? "-" : "") + out;
}

void CheckAdapter(const Admission &admitted, Bindings &bindings, const char *fact, uint64_t &opening)
{
    if (opening == admitted.model->opening)
        return;
    opening = admitted.model->opening;
    if (!admitted.One(fact))
        bindings.Error(admitted.model->screen + ": no " + fact + " from the interface adapter (android_hero.c not loaded?)");
}

Json Subject(const Admission &admitted, Bindings &bindings, SubjectSwitch &subjects, std::function<void(const Widget &)> select)
{
    std::vector<int> filled;
    std::vector<SubjectSwitch::Choice> choices;
    for (const Widget *post : admitted.All("ANDROID_POSTS"))
        if (!Field(post, "character").empty())
        {
            filled.push_back(std::atoi(Field(post, "slot").c_str()));
            choices.push_back({post->label, LowerText(Field(post, "role")), Portrait(Field(post, "face")), "hero"});
        }
    const Widget *current = nullptr;
    for (const Widget *row : admitted.All("CHARACTERS_SCROLL"))
        if (row->value == "1")
            current = row;
    const int slot = current ? current->index : 0;
    const auto at = std::find(filled.begin(), filled.end(), slot);
    // A free post selected in the game's list shows the captain (character_all.c SetButtonsState).
    const size_t position = at == filled.end() ? 0 : static_cast<size_t>(at - filled.begin());
    std::vector<const Widget *> rows = admitted.All("CHARACTERS_SCROLL");
    return subjects.View(bindings, choices, position, bindings.Text("hero.subjects"), [rows, filled, select](size_t index) {
        for (const Widget *row : rows)
            if (row->index == filled[index] && row->Offers("select"))
                select(*row);
    });
}

std::string AfterColon(const std::string &text)
{
    const size_t colon = text.find(':');
    return Trim(colon == std::string::npos ? text : text.substr(colon + 1));
}

Json CrewQuality(const Admission &admitted, Bindings &bindings, const char *table, const char *morale_text, const Widget *fact,
                 const std::string &prefix)
{
    const auto value = [&](const char *field) { return std::clamp(Number(Field(fact, (prefix + field).c_str())), 0, 100); };
    Json groups = Json::array();
    const std::pair<const char *, const char *> fields[] = {{"Sailors", "sailors"}, {"Cannoners", "cannoners"}, {"Soldiers", "soldiers"}};
    for (const Widget *row : admitted.All(table))
        for (const auto &[id, field] : fields)
            if (row->row_id == id)
                groups.push_back(Json{{"label", Trim(row->CellText(1))}, {"value", value(field)}, {"word", LowerText(Trim(row->CellText(2)))}});
    return Json{{"groups", groups},
                {"moraleLabel", bindings.Text("ship.morale")},
                {"morale", value("morale")},
                {"moraleWord", LowerText(AfterColon(OneLine(admitted.Label(morale_text))))}};
}

} // namespace storm::ui::screens::hero
