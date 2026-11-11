#pragma once

#include <filesystem>
#include <stdexcept>
#ifdef __ANDROID__
#include "platform/android_runtime.hpp"
#endif

#include <SDL2/SDL.h>

/* Filesystem proxy */
namespace fs
{
using namespace std::filesystem;

inline path GetStashPath()
{
    static path path;
    if (path.empty())
    {
#if defined(__ANDROID__)
        path = storm::android::storagePaths().user;
#else
        char *pref_path = nullptr;
        pref_path = SDL_GetPrefPath("Akella", "Sea Dogs");
        if (!pref_path)
            throw std::runtime_error(SDL_GetError());
        path = pref_path;
        SDL_free(pref_path);
#endif
    }
    return path;
}

inline path GetLogsPath()
{
    return GetStashPath() / "Logs";
}

inline path GetSaveDataPath()
{
    return GetStashPath() / "SaveData";
}

constexpr char ENGINE_INI_FILE_NAME[] = "engine.ini";
} // namespace fs
