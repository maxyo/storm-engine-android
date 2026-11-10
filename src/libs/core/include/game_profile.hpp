#pragma once

// Which game the engine runs, decided once at startup from the game files.
//
// Markers (game-relative paths, any case):
// - every supported game: engine.ini, program/, resource/;
// - Caribbean Legend: resource/texture_library.json (its texture library) and
//   resource/shaders/ (the shader sources of its own renderer); neither exists in TEHO.
//   Both are required: one without the other is a damaged or mixed installation;
// - To Each His Own: program/_lsc_on_off.h (the version header of the TEHO script base)
//   without the Caribbean Legend markers. CL keeps this header too, so CL is decided first.
// Anything else (another Storm game, a partial copy) is an error, never a default.

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace storm
{
enum class GameProfile
{
    TEHO,
    CaribbeanLegend,
};

constexpr std::string_view gameProfileName(const GameProfile profile)
{
    return profile == GameProfile::CaribbeanLegend ? "Caribbean Legend" : "To Each His Own";
}

// `exists` answers whether a game-relative path exists, matching case-insensitively.
// Throws std::runtime_error naming the missing or conflicting markers.
inline GameProfile detectGameProfile(const std::function<bool(const std::string &)> &exists)
{
    std::string missing;
    for (const char *required : {"engine.ini", "program", "resource"})
        if (!exists(required))
            missing += std::string(missing.empty() ? "" : ", ") + required;
    if (!missing.empty())
        throw std::runtime_error("Not a game directory: missing " + missing);

    constexpr const char *library = "resource/texture_library.json";
    constexpr const char *shaders = "resource/shaders";
    const bool hasLibrary = exists(library);
    const bool hasShaders = exists(shaders);
    if (hasLibrary && hasShaders)
        return GameProfile::CaribbeanLegend;
    if (hasLibrary != hasShaders)
        throw std::runtime_error(std::string("Incomplete Caribbean Legend installation: ") +
                                 (hasLibrary ? library : shaders) + " is present but " +
                                 (hasLibrary ? shaders : library) + " is missing");
    if (exists("program/_lsc_on_off.h"))
        return GameProfile::TEHO;
    throw std::runtime_error(std::string("Unknown game: neither the Caribbean Legend markers (") + library + ", " +
                             shaders + ") nor the To Each His Own marker (program/_lsc_on_off.h) were found");
}

// Detects the profile from the game directory through the file service, logs it and keeps
// it for gameProfile(). main() calls it once the resource index is built; throws as above.
GameProfile initGameProfile();

// The profile initGameProfile() detected; throws std::logic_error before that.
GameProfile gameProfile();
} // namespace storm
