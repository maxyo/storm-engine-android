#pragma once

#include <filesystem>
#include <stdexcept>
#ifdef __ANDROID__
#include "platform/android_runtime.hpp"
#endif

#ifdef _WIN32 // SHGetKnownFolderPath
#include <ShlObj.h>
#else
#include <SDL2/SDL.h>
#endif

/* Filesystem proxy */
namespace fs
{
using namespace std::filesystem;

inline path GetStashPath()
{
    static path path;
    if (path.empty())
    {
#ifdef _WIN32 // SHGetKnownFolderPath
        wchar_t *str = nullptr;
        SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_SIMPLE_IDLIST, nullptr, &str);
        path = str;
        path = path / "My Games" / "Sea Dogs";
        CoTaskMemFree(str);
#elif defined(__ANDROID__)
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

inline path GetScreenshotsPath()
{
    return GetStashPath() / "Screenshots";
}

constexpr char ENGINE_INI_FILE_NAME[] = "engine.ini";
} // namespace fs
