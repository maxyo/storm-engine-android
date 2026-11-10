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

Json Subject(const Admission &admitted, Bindings &bindings, std::function<void(const Widget &)> select)
{
    std::vector<int> filled;
    std::vector<std::string> names;
    for (const Widget *post : admitted.All("ANDROID_POSTS"))
        if (!Field(post, "character").empty())
        {
            filled.push_back(std::atoi(Field(post, "slot").c_str()));
            names.push_back(post->label);
        }
    if (filled.empty())
        return nullptr;
    const Widget *current = nullptr;
    for (const Widget *row : admitted.All("CHARACTERS_SCROLL"))
        if (row->value == "1")
            current = row;
    const int slot = current ? current->index : 0;
    const auto at = std::find(filled.begin(), filled.end(), slot);
    // A free post selected in the game's list shows the captain (character_all.c SetButtonsState).
    const int position = at == filled.end() ? 0 : static_cast<int>(at - filled.begin());
    std::vector<const Widget *> rows = admitted.All("CHARACTERS_SCROLL");
    const auto choose = [rows, filled, select](int index) {
        if (index < 0 || index >= static_cast<int>(filled.size()))
            return;
        for (const Widget *row : rows)
            if (row->index == filled[index] && row->Offers("select"))
                select(*row);
    };
    bindings.On("header", "subject-prev", [choose, position](const Event &) { choose(position - 1); });
    bindings.On("header", "subject-next", [choose, position](const Event &) { choose(position + 1); });
    bindings.Ignore("header", "subject-list");
    return Json{{"label", names[position]},
                {"sub", std::to_string(position + 1) + " " + bindings.Text("common.of") + " " + std::to_string(filled.size())},
                {"prev", position > 0},
                {"next", position + 1 < static_cast<int>(filled.size())},
                {"list", false}};
}

} // namespace storm::ui::screens::hero
