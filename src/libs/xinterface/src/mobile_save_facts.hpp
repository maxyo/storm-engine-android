// What the menus show of saves and of the game in progress, read from the engine and the
// script (the pure parsing is in mobile_save_list.hpp): the facts of each record of the save
// list, the identity of the game in progress (AndroidSaveIdentity script handler), the pause
// menu's line and the record «Продолжить» loads.
#pragma once
#include "mobile_save_list.hpp"

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace storm::mobile
{
// Returns the description stored in a save file ("" when it has none).
using SaveDescriptionReader = std::function<std::string(const std::string &path)>;

// "<hero>@<location>@<play time>@<HH:MM  DD.MM.YYYY>" of the game in progress, from the app
// script's handler (mobile-ui/PROGRAM/interface/android_save_identity.c); nothing without a
// game or handler (an error line, once).
std::optional<std::array<std::string, 4>> CurrentSaveIdentity();

// The context of row `index` of the save list (SaveListCapability::collect): the record's
// facts (SaveFacts), or for the new record the hero, location and play time of the game in
// progress. Descriptions are read once per file time.
std::map<std::string, std::string> SaveRowContext(const SaveListSnapshot &state, size_t index, const SaveDescriptionReader &read);

// The pause menu's line «<hero> · <location> · <date>» (V2-Menu-Game): the game in progress
// as the ANDROID_GAME text, its parts as context (hero, location, gameDate, gameTime). Asked
// once per opening of the menu.
std::optional<MobileWidget> GameIdentityWidget(uint64_t opening);

// Every profile's records (SAVE\\<profile>\\<file>, the game's profile folders without
// "netsave"), newest first: the rows of the save list when loading.
std::vector<SaveRecord> SaveRecordsOfAllProfiles();

// The record «Продолжить» loads on the title screen (V2-Menu-Title): the newest record of
// any profile, as the ANDROID_CONTINUE text «<hero> · <location>» (its file name when the
// description cannot be read) with the record's facts as context; nothing without records.
// Scanned once per opening of the screen.
std::optional<MobileWidget> ContinueWidget(uint64_t opening, const SaveDescriptionReader &read);
} // namespace storm::mobile
