#pragma once

#include <filesystem>
#include <stdexcept>
#include <SDL2/SDL.h>
#ifdef __ANDROID__
#include <SDL2/SDL_system.h>
#include <jni.h>
#include <string>
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

// The launcher may select an existing shared-storage folder; user data stays private.
inline StoragePaths storagePaths(const char *internal, const char *external,
                                 const std::filesystem::path &selectedGame = {})
{
    if (!internal || !*internal)
        throw std::runtime_error("Android internal storage is unavailable");
    const std::filesystem::path privateRoot(internal);
    const std::filesystem::path dataRoot(external && *external ? external : internal);
    if (!privateRoot.is_absolute() || !dataRoot.is_absolute())
        throw std::runtime_error("Android storage roots must be absolute");
    if (!selectedGame.empty() && !selectedGame.is_absolute())
        throw std::runtime_error("Selected game directory must be absolute");
    return {selectedGame.empty() ? dataRoot / "game" : selectedGame, privateRoot / "storm"};
}

#ifdef __ANDROID__
inline StoragePaths storagePaths()
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
        throw std::runtime_error("Android game activity is unavailable");
    const auto type = env->GetObjectClass(activity);
    const auto method = type ? env->GetMethodID(type, "getGameDataDirectory", "()Ljava/lang/String;") : nullptr;
    const auto value = method ? static_cast<jstring>(env->CallObjectMethod(activity, method)) : nullptr;
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    if (env->ExceptionCheck() || !value)
    {
        env->ExceptionClear();
        if (value) env->DeleteLocalRef(value);
        throw std::runtime_error("Game folder unavailable. Check file access in the launcher or import a ZIP.");
    }
    // JNI UTF chars use modified UTF-8; use UTF-16 to preserve non-BMP folder names too.
    const auto *chars = env->GetStringChars(value, nullptr);
    if (!chars)
    {
        env->ExceptionClear();
        env->DeleteLocalRef(value);
        throw std::runtime_error("Cannot read selected game directory");
    }
    const std::u16string path(reinterpret_cast<const char16_t *>(chars), env->GetStringLength(value));
    env->ReleaseStringChars(value, chars);
    env->DeleteLocalRef(value);
    if (path.empty()) throw std::runtime_error("No game directory selected");
    return storagePaths(SDL_AndroidGetInternalStoragePath(), SDL_AndroidGetExternalStoragePath(),
                        std::filesystem::path(path));
}

inline void prepareGameDirectory()
{
    const auto paths = storagePaths();

    if (!std::filesystem::is_regular_file(paths.game / "engine.ini"))
        throw std::runtime_error("Place your legally obtained game files, including engine.ini, in " + paths.game.string());
    std::filesystem::current_path(paths.game);
}
#endif
} // namespace storm::android
