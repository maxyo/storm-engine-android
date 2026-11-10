#pragma once

// What the Caribbean Legend localisation natives (DLGO, DLG_Convert, StringFromKey, ... in
// core/src/natives_dlg.cpp) need from the string service, which lives in xinterface: the
// current language directory and the common strings (common.ini). STRSERVICE::Init installs
// them. Without them the natives report an error and expand nothing.

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace storm::dlg
{
struct TextHooks
{
    // Directory of the current language under resource/ini/texts ("ENGLISH", "russian", ...).
    std::function<std::string()> language_directory;
    // A common string (common.ini) by key; nullopt when missing.
    std::function<std::optional<std::string>(std::string_view key)> common_string;
};

void setTextHooks(TextHooks hooks);
} // namespace storm::dlg
