#pragma once

#include <filesystem>
#include <stdexcept>
#include <SDL2/SDL.h>
#ifdef __ANDROID__
#include <SDL2/SDL_system.h>
#endif

namespace storm::android
{
// Only mutate on the game thread while consuming SDL's event queue.
class Lifecycle
{
public:
    void consume(unsigned type)
    {
        switch (type)
        {
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_DIDENTERBACKGROUND: background_ = true; break;
        case SDL_APP_DIDENTERFOREGROUND: background_ = false; break;
        case SDL_APP_TERMINATING:
        case SDL_QUIT: terminating_ = true; break;
        default: break;
        }
    }
    bool canRun() const { return !background_ && !terminating_; }
    bool terminating() const { return terminating_; }
private:
    bool background_ = false;
    bool terminating_ = false;
};

struct StoragePaths
{
    std::filesystem::path game;
    std::filesystem::path user;
};

// App-specific directories; no shared-storage permission or hardcoded /sdcard.
inline StoragePaths storagePaths(const char *internal, const char *external)
{
    if (!internal || !*internal)
        throw std::runtime_error("Android internal storage is unavailable");
    const std::filesystem::path privateRoot(internal);
    const std::filesystem::path dataRoot(external && *external ? external : internal);
    if (!privateRoot.is_absolute() || !dataRoot.is_absolute())
        throw std::runtime_error("Android storage roots must be absolute");
    return {dataRoot / "game", privateRoot / "storm"};
}

#ifdef __ANDROID__
inline StoragePaths storagePaths()
{
    return storagePaths(SDL_AndroidGetInternalStoragePath(), SDL_AndroidGetExternalStoragePath());
}

inline void prepareGameDirectory()
{
    const auto paths = storagePaths();
    std::filesystem::create_directories(paths.game);
    if (!std::filesystem::is_regular_file(paths.game / "engine.ini"))
        throw std::runtime_error("Place your legally obtained game files, including engine.ini, in " + paths.game.string());
    std::filesystem::current_path(paths.game);
}
#endif
} // namespace storm::android
