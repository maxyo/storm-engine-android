#include "platform/android_runtime.hpp"
#include <cassert>
int main()
{
    using storm::android::Lifecycle;
    Lifecycle state;
    assert(state.canRun());
    state.consume(SDL_APP_WILLENTERBACKGROUND);
    assert(!state.canRun());
    state.consume(SDL_APP_DIDENTERBACKGROUND);
    state.consume(SDL_WINDOWEVENT);
    state.consume(SDL_APP_WILLENTERFOREGROUND);
    assert(!state.canRun());
    state.consume(SDL_APP_DIDENTERFOREGROUND);
    assert(state.canRun());
    state.consume(SDL_APP_TERMINATING);
    state.consume(SDL_APP_DIDENTERFOREGROUND);
    assert(state.terminating() && !state.canRun());
    Lifecycle quit;
    quit.consume(SDL_QUIT);
    assert(quit.terminating());
    const auto paths = storm::android::storagePaths("/data/user/0/app/files", "/storage/emulated/0/Android/data/app/files");
    assert(paths.game == "/storage/emulated/0/Android/data/app/files/game");
    assert(paths.user == "/data/user/0/app/files/storm");
    assert(storm::android::storagePaths("/private", nullptr).game == "/private/game");
    assert(storm::android::storagePaths("/private", "").game == "/private/game");
    const auto direct = storm::android::storagePaths("/private", "/external", "/storage/emulated/0/TEHO");
    assert(direct.game == "/storage/emulated/0/TEHO");
    assert(direct.user == "/private/storm");
    bool relativeRejected = false;
    try { storm::android::storagePaths("/private", "/external", "relative/game"); }
    catch (const std::runtime_error &) { relativeRejected = true; }
    assert(relativeRejected);
    for (const char *invalid : {static_cast<const char *>(nullptr), "", "relative"})
    {
        bool rejected = false;
        try { storm::android::storagePaths(invalid, "/external"); }
        catch (const std::runtime_error &) { rejected = true; }
        assert(rejected);
    }
    return 0;
}
