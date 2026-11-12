// Shared parts of the hero section screens (hero_character.cpp, hero_items.cpp, …): reading
// the interface adapter's facts (android_hero.c, widgets ANDROID_<LIST>), game numbers and
// words, and the header's officer switch.
#pragma once

#include "controls.h"

#include <functional>

namespace storm::ui::screens::hero
{

// A field of an adapter fact row ("" when absent). Attribute names arrive in lower case.
std::string Field(const Widget *fact, const char *key);
// The first number in a game text ("Вес: 4.6 / 140" → 4, "-3" → -3), 0 without one.
int Number(const std::string &text);
std::string Trim(std::string text);
// Lower case for the game's upper-case words ("ОТЛИЧНОЕ"): ASCII, Latin-1 and Cyrillic.
std::string LowerText(const std::string &text);
// A game number with a decimal point in the interface's language ("2.4" → "2,4" except in
// English): every point between two digits.
std::string Decimal(const std::string &text, const std::string &language);
// "41440" → "41 440" (no-break space).
std::string Thousands(const std::string &digits);
// The row image of a game portrait by face, null without one.
Json Portrait(const std::string &face);
// The interface adapter's facts are part of these screens, but a closing interface may publish a
// last model without them (the script's subject is gone): a missing `fact` is an error only in
// the first model of an opening. `opening` keeps the opening already checked.
void CheckAdapter(const Admission &admitted, Bindings &bindings, const char *fact, uint64_t &opening);
// The subject switch over the captain and the officers on posts (CHARACTERS_SCROLL rows, the
// adapter's ANDROID_POSTS), with their list; `select` sends the selection of a row. Null without
// officers.
Json Subject(const Admission &admitted, Bindings &bindings, SubjectSwitch &subjects, std::function<void(const Widget &)> select);

} // namespace storm::ui::screens::hero
