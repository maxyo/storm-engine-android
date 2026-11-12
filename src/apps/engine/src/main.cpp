#include <chrono>
#include <string>
#include <thread>

#include <SDL2/SDL.h>
// AddressSanitizer must see every C++ allocation: an ASan build keeps the default new/delete.
#if defined(__SANITIZE_ADDRESS__)
#define STORM_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define STORM_ASAN 1
#endif
#endif
#if !defined(__ANDROID__) && !defined(STORM_ASAN)
#include <mimalloc-new-delete.h>
#endif
#include <mimalloc.h>
#include <spdlog/spdlog.h>

#include "core_private.h"
#include "lifecycle_diagnostics_service.hpp"
#include "logging.hpp"
#include "os_window.hpp"
#include "app_events.hpp"
#include "v_sound_service.h"
#include "fs.h"
#include "platform/host_hooks.hpp"
#include "profiler.hpp"
#ifdef STORM_TRACY
#include <common/TracyVersion.hpp>
#endif
#include "watermark.hpp"
#ifdef __ANDROID__
#include <android/log.h>
#include "platform/android_ui.hpp"
#include "shared/interface/messages.h"
#include "platform/android_camera_zoom.hpp"
#include "platform/android_camera_turn.hpp"
#include "platform/android_held_controls.hpp"
#include "platform/script_patches.hpp"
#include "platform/android_shortcuts.hpp"
#include "android_gameplay_state.hpp"
#include "android_ship_abilities.hpp"
#include "android_time_scale.hpp"
#include "android_combat_state.hpp"
#include "android_sea_orders_state.hpp"
#include "android_land_commands_state.hpp"
#include "android_worldmap_state.hpp"
#include <sstream>
#include <charconv>
#endif

#ifdef __ANDROID__
extern "C" JNIEXPORT jboolean JNICALL
Java_org_stormengine_port_GameActivity_nativeWorldMapModal(JNIEnv *env,jobject caller,jlong revision,jlong activity,
    jlong gameplay,jlong token,jlong proof,jint operation)
{
    auto current=static_cast<jobject>(SDL_AndroidGetActivity());
    const bool same=current&&env->IsSameObject(current,caller);
    if(current)env->DeleteLocalRef(current);
    if(!same||revision<=0||activity<=0||gameplay<=0||token<=0||proof<0){
        storm::android::traceWorldMapModal("invalid-caller-or-identity","token="+std::to_string(token));return JNI_FALSE;
    }
    const bool accepted=storm::android::worldMapHud.modal.operate(revision,activity,gameplay,token,proof,operation,SDL_GetTicks());
    storm::android::traceWorldMapModal(accepted?"accepted":"rejected",
        "op="+std::to_string(operation)+" requested="+std::to_string(token)+"/"+std::to_string(proof)+" "+storm::android::worldMapHud.modal.debug());
    return accepted?JNI_TRUE:JNI_FALSE;
}

// GameActivity's bounded exit wait polls this from the UI thread; see host_hooks.hpp.
extern "C" JNIEXPORT jboolean JNICALL Java_org_stormengine_port_GameActivity_nativeSaveInProgress(JNIEnv *, jclass)
{
    return storm::platform::saveInProgress() ? JNI_TRUE : JNI_FALSE;
}
#endif

namespace
{

CorePrivate *core_private;

constexpr char defaultLoggerName[] = "system";
bool isRunning = false;
bool bActive = true;
bool bSoundInBackground = false;

storm::diag::LifecycleDiagnosticsService lifecycleDiagnostics;

// Single owner of "may sound play right now". Window focus, the Android
// activity lifecycle and Android audio focus all used to call
// SetActiveWithFade independently and could re-enable each other's pause.
struct AudioGate
{
    bool windowFocused = true; // OSWindow focus
    bool foreground = true;    // AppEvents lifecycle (Android pause/resume)
    bool audioFocus = true;    // Android AudioManager focus, reported by the activity
    float duck = 1.0f;         // transient "may duck" focus loss
    bool applied = true;
    float appliedDuck = 1.0f;

    void apply()
    {
        if (!core_private || !core_private->initialized())
            return;
        const bool active = foreground && audioFocus && (windowFocused || bSoundInBackground);
        if (active == applied && duck == appliedDuck)
            return;
        auto *soundService = static_cast<VSoundService *>(core.GetService("SoundService"));
        if (!soundService)
            return;
        if (duck != appliedDuck)
        {
            soundService->SetDucking(duck);
            appliedDuck = duck;
        }
        if (active != applied)
        {
            soundService->SetActiveWithFade(active);
            applied = active;
        }
    }
} audioGate;

void RunFrame()
{
    if (!core_private->Run())
    {
        isRunning = false;
    }

    lifecycleDiagnostics.notifyAfterRun();
}

// Last line of defence for exceptions thrown out of a frame (about 490 `throw`
// sites are reachable from the frame loop): log, flush, tell the player, and
// let main() shut the engine down in order. Android already caught
// std::exception here; this also covers other platforms and non-std throws.
bool fatalError = false;

void ReportFatalError(const char *what)
{
    fatalError = true;
    isRunning = false;
    spdlog::critical("Unhandled exception in the frame loop: {}", what);
    if (auto logger = spdlog::default_logger())
        logger->flush();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Engine stopped: %s", what);
    // SDL's Android bridge shows a Java dialog and waits for dismissal
    // before SDL_main returns and the game Activity is finished.
    // SDL's Android default TextView colors can be dark on a dark
    // Activity theme, so explicitly style this fatal error message.
    const SDL_MessageBoxColorScheme colors{{
        {24, 24, 24}, {255, 255, 255}, {128, 128, 128}, {64, 64, 64}, {96, 96, 96}}};
    const SDL_MessageBoxButtonData button{
        SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "OK"};
    const SDL_MessageBoxData dialog{SDL_MESSAGEBOX_ERROR, nullptr, "Game stopped", what, 1, &button, &colors};
    SDL_ShowMessageBox(&dialog, nullptr);
}

#ifdef __ANDROID__
// Logs what the script patch layer did during the resource scan (which runs
// before logging is ready) and, once per distinct set of unapplied fixes, tells
// the player which fixes their game files did not allow.
void AnnounceScriptPatches()
{
    const auto &report = storm::script_patches::lastReport();
    for (const auto &line : report.info)
        spdlog::info("{}", line);
    for (const auto &line : report.errors)
        spdlog::error("{}", line);
    const auto key = report.noticeKey();
    if (key.empty())
        return;
    const auto stamp = std::filesystem::path(SDL_AndroidGetInternalStoragePath()) / "script-patches/notice-shown";
    std::string shown;
    if (std::ifstream file(stamp); file)
        std::getline(file, shown);
    if (shown == key)
        return;
    bool russian = false;
    if (SDL_Locale *locales = SDL_GetPreferredLocales())
    {
        russian = locales[0].language && std::string_view(locales[0].language) == "ru";
        SDL_free(locales);
    }
    const auto text = storm::script_patches::noticeText(report, russian);
    const SDL_MessageBoxButtonData button{
        SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "OK"};
    const SDL_MessageBoxData dialog{SDL_MESSAGEBOX_WARNING, nullptr,
                                    russian ? "Исправления не применены" : "Fixes not applied", text.c_str(), 1,
                                    &button, nullptr};
    if (SDL_ShowMessageBox(&dialog, nullptr) != 0)
    {
        spdlog::error("Cannot show the script patch notice: {}", SDL_GetError());
        return; // shown again next start
    }
    std::ofstream file(stamp, std::ios::trunc);
    file << key << '\n';
    if (!file)
        spdlog::error("Cannot record the script patch notice in {}", stamp.string());
}
#endif

void mimalloc_fun(const char *msg, void *arg)
{
    static std::filesystem::path mimalloc_log_path;
    if (mimalloc_log_path.empty())
    {
        mimalloc_log_path = fs::GetLogsPath() / "mimalloc.log";
        std::error_code ec;
        remove(mimalloc_log_path, ec);
    }

    FILE *mimalloc_log =
        fopen(mimalloc_log_path.c_str(), "a+b");
    if (mimalloc_log != nullptr)
    {
        fputs(msg, mimalloc_log);
        fclose(mimalloc_log);
    }
}

} // namespace

// Engine-side reactions to application events. Other subsystems subscribe to
// storm::AppEvents themselves; see src/libs/window/README.md.
void SubscribeEngineToAppEvents()
{
    auto &events = storm::AppEvents::instance();
    // Early: pause audio and tell scripts inside the SDL call that announces the
    // pause, before anything else can run.
    events.subscribe(
        [](const storm::AppEvent &event) {
            if (event.type == storm::AppEventType::WillEnterBackground)
            {
                audioGate.foreground = false;
                audioGate.apply();
                if (event.immediate && core_private && core_private->initialized())
                {
                    // Hook for scripts (the app's background autosave). Runs between frames on
                    // the game thread. This handler runs before the renderer's (early handlers are
                    // called in subscription order), so the scripts, and the save thumbnail, still
                    // see a live surface; the renderer stops presenting right after. The Android
                    // UI thread waits in surfaceDestroyed only for a running bgfx frame.
                    const auto started = std::chrono::steady_clock::now();
                    core_private->Event("AppWillEnterBackground");
                    spdlog::info("AppWillEnterBackground handlers took {} ms",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started).count());
                }
            }
            if (event.type == storm::AppEventType::WillEnterBackground ||
                event.type == storm::AppEventType::Terminating)
            {
                // A backgrounded process can be killed without notice: do not lose buffered log lines.
                spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });
            }
        },
        true);
    events.subscribe([](const storm::AppEvent &event) {
        const bool ready = core_private && core_private->initialized();
        switch (event.type)
        {
        case storm::AppEventType::DidEnterForeground:
            audioGate.foreground = true;
            audioGate.apply();
            if (ready)
                core_private->Event("AppDidEnterForeground");
            break;
        case storm::AppEventType::LowMemory:
            if (ready)
            {
                uint64_t released = 0;
                if (auto *soundService = static_cast<VSoundService *>(core.GetService("SoundService")))
                    released = soundService->TrimCaches();
                spdlog::warn("Low memory: released {} KiB of cached audio", released / 1024);
                core_private->Event("AppLowMemory");
            }
            break;
        case storm::AppEventType::WindowResized:
            spdlog::info("Window resized to {}x{}", event.width, event.height);
            if (ready)
                core_private->Event("AppWindowResized", "ll", event.width, event.height);
            break;
        default:
            break;
        }
    });
}

void HandleWindowEvent(const storm::OSWindow::Event &event)
{
    if (event == storm::OSWindow::Closed)
    {
        isRunning = false;
        if (core_private->initialized())
        {
            core_private->Event("DestroyWindow");
        }
    }
    else if (event == storm::OSWindow::FocusGained)
    {
        bActive = true;
        audioGate.windowFocused = true;
        if (core_private->initialized())
        {
            core_private->AppState(bActive);
            audioGate.apply();
        }
    }
    else if (event == storm::OSWindow::FocusLost)
    {
        bActive = false;
        audioGate.windowFocused = false;
#ifdef __ANDROID__
        // AppState/audio fading can block this thread beyond the opening grace.
        // Observe the real SDL focus event before those calls, at its actual time.
        const auto observedAt=SDL_GetTicks();
        if(storm::android::worldMapHud.modal.ownsInput(observedAt)){
            storm::android::worldMapHud.modal.hold(false,observedAt);
            storm::android::traceWorldMapModal("focus-lost-observed",storm::android::worldMapHud.modal.debug());
        }
#endif
        if (core_private->initialized())
        {
            core_private->AppState(bActive);
            audioGate.apply();
        }
    }
}

int main(int argc, char *argv[])
{
#ifdef __ANDROID__
    // The engine runs once per process: its globals and statics (services, script state, ids of
    // renderer objects) hold one run. The app ends the game process when this function returns
    // (GameActivity.onDestroy), so the next start is a fresh process; entering it again in the
    // same process would run on the previous run's state.
    static bool entered = false;
    if (entered)
        __android_log_assert(nullptr, "StormEngine",
                             "SDL_main entered a second time in one process: the engine runs once per process");
    entered = true;
    try
    {
        storm::android::prepareGameDirectory();
        // Build the resource index only after JNI and the game directory are ready.
        fio->ScanResourcePaths();
    }
    catch (const storm::script_patches::LayerError &error)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Damaged script fixes", error.what(), nullptr);
        return EXIT_FAILURE;
    }
    catch (const std::exception &error)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Game data required", error.what(), nullptr);
        return EXIT_FAILURE;
    }
#endif
    mi_register_output(mimalloc_fun, nullptr);
    mi_option_set(mi_option_show_errors, 1);
    mi_option_set(mi_option_show_stats, 0);
    mi_option_set(mi_option_eager_commit, 1);
    mi_option_set(mi_option_eager_region_commit, 1);
    mi_option_set(mi_option_large_os_pages, 1);
    mi_option_set(mi_option_page_reset, 0);
    mi_option_set(mi_option_segment_reset, 0);
    mi_option_set(mi_option_reserve_huge_os_pages, 1);
    mi_option_set(mi_option_segment_cache, 16);
#ifdef _DEBUG
    mi_option_set(mi_option_verbose, 4);
#endif

#ifdef __ANDROID__
    // The manifest pins the activities to sensorLandscape, but SDL recomputes the
    // requested orientation from the window it creates; without this hint a
    // window that is not wider than tall (or a resizable one) unlocks portrait.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

    // Window/lifecycle event hub; must be installed on the game thread.
    storm::AppEvents::instance().install();
    SubscribeEngineToAppEvents();

    // Init diagnostics
    const auto lifecycleDiagnosticsGuard =
#ifdef STORM_ENABLE_CRASH_REPORTS
        lifecycleDiagnostics.initialize(true);
#else
        lifecycleDiagnostics.initialize(false);
#endif
    if (!lifecycleDiagnosticsGuard)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Warning", "Unable to initialize lifecycle service!", nullptr);
    }
    else
    {
        lifecycleDiagnostics.setCrashInfoCollector([]() { core_private->collectCrashInfo(); });
    }

    // Init stash
    create_directories(fs::GetSaveDataPath());

    // Init logging
    spdlog::set_default_logger(storm::logging::getOrCreateLogger(defaultLoggerName));
    spdlog::info("Logging system initialized. Running on {}", STORM_BUILD_WATERMARK_STRING);
#ifdef STORM_TRACY
    // Development build (cmake/tracy.cmake). The application's release packaging refuses a library with this line.
    STORM_PROFILE_THREAD("game");
    spdlog::info("storm-profiler: Tracy {}.{}.{} client, on demand, listening on 127.0.0.1:8086 (device: adb forward "
                 "tcp:8086 tcp:8086; tools/android/README.md)",
                 tracy::Version::Major, tracy::Version::Minor, tracy::Version::Patch);
#endif
    spdlog::info("mimalloc-redirect status: {}", mi_is_redirected());
#ifdef __ANDROID__
    AnnounceScriptPatches();
    storm::android::readStartAtMainMenu();
#endif

    // Init core
    core_private = static_cast<CorePrivate *>(&core);
    core_private->Init();

    // Read config
    auto ini = fio->OpenIniFile(fs::ENGINE_INI_FILE_NAME);

    int width = 1024, height = 768;
    int preferred_display = 0;
    bool fullscreen = false;
    bool show_borders = false;
    bool run_in_background = false;

    if (ini)
    {
        auto bDebugWindow = ini->GetInt(nullptr, "DebugWindow", 0) == 1;
        auto bAcceleration = ini->GetInt(nullptr, "Acceleration", 0) == 1;
        if (ini->GetInt(nullptr, "logs", 1) == 0) // disable logging
        {
            spdlog::set_level(spdlog::level::off);
        }
        width = ini->GetInt(nullptr, "screen_x", 1024);
        height = ini->GetInt(nullptr, "screen_y", 768);
        preferred_display = ini->GetInt(nullptr, "display", 0);
        fullscreen = ini->GetInt(nullptr, "full_screen", false);
        show_borders = ini->GetInt(nullptr, "window_borders", false);
        run_in_background = ini->GetInt(nullptr, "run_in_background", false);
        if (run_in_background) {
            bSoundInBackground = ini->GetInt(nullptr, "sound_in_background", true);
        }
        else {
            bSoundInBackground = false;
        }
    }

    std::shared_ptr<storm::OSWindow> window =
        storm::OSWindow::Create(width, height, preferred_display, fullscreen, show_borders);
    window->SetTitle("Sea Dogs");
    window->Subscribe(HandleWindowEvent);
    window->Show();
    core_private->SetWindow(window);

    // Init core
    core_private->InitBase();

    // Message loop. Frames are paced by the renderer's present (Swappy on Android, vsync elsewhere).

#ifdef __ANDROID__
    storm::android::Lifecycle androidLifecycle;
    storm::android::GameplayShortcuts gameplayShortcuts;
    storm::android::GameplayState gameplayState;
    // Why the game is not ready for gameplay this loop (GameplaySnapshot::blocked).
    const auto notReady = [&]() -> const char * {
        if (!androidLifecycle.canRun() || !bActive) return "paused";
        if (storm::android::loadingState == 1) return "loading"; // -1: nothing loaded yet (starting)
        if (!storm::android::mobileScreen.empty()) return "interface";
        return "starting";
    };
    storm::android::movementInput.updateOwner({});
    {
        storm::android::GameplaySnapshot starting;
        starting.blocked = "starting";
        gameplayState.update(std::move(starting));
    }
    storm::android::combatInput.guard = [&]() {
        return storm::android::combatInput.player() == std::to_string(core.GetEntityId("Player")) &&
            storm::android::combatContextCurrent(gameplayState, isRunning && bActive && androidLifecycle.canRun() &&
            core_private->initialized() && core.Controls && storm::android::loadingState == 0 && storm::android::mobileScreen.empty());
    };
    storm::android::movementInput.guard = [&](std::string_view owner) -> std::optional<storm::android::MovementPermission> {
        const bool ready = isRunning && bActive && androidLifecycle.canRun() && core_private->initialized() &&
            core.Controls && storm::android::loadingState == 0 && storm::android::mobileScreen.empty();
        const auto current = storm::android::readGameplaySnapshot(ready);
        if (!storm::android::movementOwnerCurrent(owner, current)) return std::nullopt;
        storm::android::MovementPermission permissions;
        unsigned processed = 0;
        const auto count = core.Controls->GetControlsNum();
        for (int32_t i=0; i<count; ++i)
        {
            USER_CONTROL descriptor{};
            if (!core.Controls->GetControlDesc(i, descriptor) || !descriptor.name) continue;
            const std::string_view name = descriptor.name;
            unsigned bit = 0;
            bool *allowed = nullptr;
            if (name == "ChrForward") { bit = 1; allowed = &permissions.forward; }
            else if (name == "ChrBackward") { bit = 2; allowed = &permissions.backward; }
            else if (name == "ChrStrafeLeft") { bit = 4; allowed = &permissions.left; }
            else if (name == "ChrStrafeRight") { bit = 8; allowed = &permissions.right; }
            else if (name == "ChrRun") { bit = 16; allowed = &permissions.run; }
            if (!bit || (processed & bit)) continue;
            processed |= bit;
            *allowed = !descriptor.bLocked && !descriptor.bSystemLocked &&
                (descriptor.control_type == UCT_ControlTree || descriptor.system_code != UNASSIGNED_CONTROL) &&
                std::find(current.capabilities.begin(), current.capabilities.end(), name) != current.capabilities.end();
            // Locked and unmapped names are resolved too. No index cache: the
            // next request observes remapping and current global/control locks.
            if (processed == 31) break;
        }
        return permissions;
    };
    // A held HUD control acts only while the gameplay that binds it is live.
    storm::android::heldControls.guard = [&](std::string_view control) {
        return isRunning && bActive && androidLifecycle.canRun() && core_private->initialized() && core.Controls &&
            storm::android::loadingState == 0 && storm::android::mobileScreen.empty() && gameplayState.allows(control);
    };
    storm::android::updateCombatState(gameplayState, core_private);
    storm::android::publishCombatState();
    storm::android::publishGameplayState(gameplayState.json());
    storm::android::worldMapHud.update({},SDL_GetTicks());
    storm::android::publishWorldMapHud();
    storm::android::shipTelemetry.update(gameplayState.revision(), {});
    storm::android::publishShipTelemetry();
    storm::android::publishSeaQuickAction();
    unsigned shortcutLogBudget = 64;
#endif
    int exitCode = EXIT_SUCCESS;
    storm::platform::FrameWorkClock frameWork;
    isRunning = true;
    while (isRunning)
    {
        STORM_PROFILE_ZONE("main loop");
        if (const auto work = frameWork.lap(storm::platform::FrameWorkClock::Clock::now()))
            storm::platform::reportFrameWork(std::chrono::duration_cast<std::chrono::nanoseconds>(*work));
#ifdef __ANDROID__
        storm::android::worldMapHud.clock(SDL_GetTicks());
        const auto combatDrainEpoch = storm::android::combatCancelEpoch.load(std::memory_order_acquire);
        // The UI host first: it applies the touches the platform sent, and what they produce
        // is read in this iteration, together with the activity's own queue.
        {
            STORM_PROFILE_ZONE("UI host: input");
            storm::android::ui::tick();
        }
        // A pinch acts only in the frame it came for (android_camera_zoom.hpp).
        if (storm::android::cameraZoom.beginFrame())
            __android_log_print(ANDROID_LOG_WARN, "StormInput", "camera pinch dropped: no camera with a zoom took it (gameplay %s)",
                                gameplayState.json().c_str());
        auto mobileCommands = storm::android::ui::takeCommands();
        for (auto &command : storm::android::pollMobileActions())
            mobileCommands.push_back(std::move(command));
        for (const auto &command : mobileCommands)
        {
            if(storm::android::worldMapHud.modal.ownsInput(SDL_GetTicks()) &&
                (command.starts_with("gameplay:") || command.starts_with("shortcut:") || command.starts_with("context:") ||
                 command.starts_with("move:") || command.starts_with("combat:") || command.starts_with("sails:") ||
                 command.starts_with("hold:") || command.starts_with("seaquick:") || command.starts_with("camera:")))continue;
            if (command.starts_with("combat:"))
            {
                storm::android::combatInput.offer(std::string_view(command).substr(7), SDL_GetTicks(), combatDrainEpoch);
            }
            else if (command.starts_with("move:"))
            {
                storm::android::movementInput.offer(std::string_view(command).substr(5), combatDrainEpoch);
            }
            else if (command.starts_with("landcommands:"))
            {
                storm::android::landCommands.offer(std::string_view(command).substr(13), SDL_GetTicks());
            }
            else if (command.starts_with("spyglass:"))
            {
                storm::android::spyglass.acknowledge(std::string_view(command).substr(9));
            }
            else if (command.starts_with("camera:turn\t"))
            {
                if (!storm::android::cameraTurn.offer(std::string_view(command).substr(7)))
                    __android_log_print(ANDROID_LOG_ERROR, "StormInput", "invalid camera command '%s'", command.c_str());
            }
            else if (command.starts_with("camera:"))
            {
                if (!storm::android::cameraZoom.offer(std::string_view(command).substr(7)))
                    __android_log_print(ANDROID_LOG_ERROR, "StormInput", "invalid camera command '%s'", command.c_str());
            }
            else if (command.starts_with("hold:"))
            {
                storm::android::heldControls.offer(std::string_view(command).substr(5), combatDrainEpoch);
            }
            else if (command.starts_with("seaquick:"))
            {
                const bool accepted = storm::android::seaQuickAction.offer(std::string_view(command).substr(9), SDL_GetTicks());
                if (command.find("\tactivate\t") != std::string::npos)
                    storm::android::traceContextAction("sea-quick-mailbox", accepted ? "accepted" : "rejected", command);
            }
            else if (command.starts_with("orders:"))
            {
                storm::android::seaOrders.offer(std::string_view(command).substr(7), SDL_GetTicks());
            }
            else if (command.starts_with("worldmap:"))
            {
                const bool accepted=storm::android::worldMapHud.offer(std::string_view(command).substr(9),SDL_GetTicks());
                if(accepted && command.find("\tprepare\t")!=std::string::npos){
                    while(gameplayState.take(SDL_GetTicks())){} // discard actions already across the Java mailbox
                    gameplayShortcuts.clear();storm::android::requestedContextAction.clear();
                }
                static unsigned mapTraceBudget=160;
                if(mapTraceBudget && (command.find("\tprepare\t")!=std::string::npos || !accepted)){
                    --mapTraceBudget;
                    storm::android::traceContextAction("worldmap-mailbox",accepted?"accepted":"rejected",command);
                }
            }
            else if (command.starts_with("navigator:"))
            {
                const bool accepted=storm::android::mobileScreen.empty() &&
                    storm::android::seaNavigator.offer(std::string_view(command).substr(10),
                        storm::android::shipTelemetry.revision(), storm::android::shipTelemetry.hudRevision(),
                        storm::android::shipTelemetry.frame().navigatorGeneration);
                static std::string lastNavigatorPrepared,lastNavigatorVisible,lastNavigatorOther;
                auto &lastNavigatorRequest=command.find("\tprepare\t")!=std::string::npos?lastNavigatorPrepared:
                    command.find("\tvisible\t")!=std::string::npos?lastNavigatorVisible:lastNavigatorOther;
                const auto detail=std::string(accepted?"accepted ":"rejected ")+storm::android::seaNavigator.result()+" "+command+" current="+
                    std::to_string(storm::android::shipTelemetry.revision())+"/"+std::to_string(storm::android::shipTelemetry.hudRevision())+"/"+
                    std::to_string(storm::android::shipTelemetry.frame().navigatorGeneration);
                if(detail!=lastNavigatorRequest){lastNavigatorRequest=detail;storm::android::traceContextAction("navigator-mailbox","receive",detail);}
            }
            else if (command.starts_with("hudowner:"))
            {
                // The portable UI presents this HUD: its unanswered revisions keep native drawing off.
                if (command == "hudowner:lost") {
                    if (const auto line = storm::android::hudPresenters.lost(); !line.empty())
                        __android_log_print(ANDROID_LOG_ERROR, "StormInput", "%s", line.c_str());
                }
                else if (!storm::android::hudPresenters.declare(std::string_view(command).substr(9)))
                    __android_log_print(ANDROID_LOG_ERROR, "StormInput", "unknown HUD presenter declaration '%s'", command.c_str());
            }
            else if (command.starts_with("shiphud:"))
            {
                storm::android::shipTelemetry.acknowledge(std::string_view(command).substr(8));
            }
            else if (command == "graphics:reload")
            {
                storm::android::graphicsSettingsChanged = true;
            }
            else if (command.starts_with("interface:"))
            {
                storm::android::interfacePresentation.offer(std::string_view(command).substr(10));
            }
            else if (command.starts_with("sails:"))
            {
                storm::android::shipTelemetry.offerSails(std::string_view(command).substr(6), SDL_GetTicks());
            }
            else if (command.starts_with("gameplay:"))
            {
                gameplayState.offer(std::string_view(command).substr(9), SDL_GetTicks());
            }
            else if ((command == "back" || command == "shortcut:mainmenu") && storm::android::fullscreenVideo())
            {
                // Back during a full-screen game video is the game's Cancel: the scripts'
                // video-break handlers decide whether this video may be skipped (as the
                // player's tap does). The interface behind it (often a stale one: the main menu
                // leaves without a release) gets nothing.
                core_private->Event("Control Activation", "s", "ICancel");
                core_private->Event("Control Deactivation", "s", "ICancel");
            }
            else if (command.starts_with("widget:game_over\tEND\t"))
            {
                // The death screen's choice: end the game's death picture now. The game's own
                // end of it (interface.c StartPictureAsVideo → PictureAsVideoBreak) goes on to
                // its title screen, as after the picture's time.
                if (storm::android::gameOverShown)
                    core_private->Event("PictureAsVideoBreak");
                else
                    spdlog::warn("death screen: end of the death picture asked while none is shown");
            }
            else if (command.starts_with("shortcut:"))
            {
                if (storm::android::mobileScreen.empty())
                    gameplayShortcuts.offer(std::string_view(command).substr(9), SDL_GetTicks());
            }
            else if (command.starts_with("main:") && storm::android::mobileScreen == "mainmenu")
            {
                const auto action = command.substr(5);
                if (action == "New" || action == "Load" || action == "Options" || action == "Credits" || action == "Exit")
                    core_private->Event("backgroundcommand", "s", action.c_str());
            }
            else if (command.starts_with("audiofocus:"))
            {
                // Reported by the activity's AudioManager focus listener.
                const auto state = std::string_view(command).substr(11);
                if (state == "gain") { audioGate.audioFocus = true; audioGate.duck = 1.0f; }
                else if (state == "duck") { audioGate.audioFocus = true; audioGate.duck = 0.25f; }
                else if (state == "loss") { audioGate.audioFocus = false; audioGate.duck = 1.0f; }
            }
            else if (command == "back" && !storm::android::mobileScreen.empty())
                core_private->Event("exitCancel");
            else if (command == "uisound:press")
            {
                // A press started on the touch UI's game interface: the scripts' interface click,
                // once per gesture (the widget actions that follow are silent, mobile_action_frame.hpp).
                if (!storm::android::mobileScreen.empty())
                    core_private->Event(ISOUND_EVENT, "l", 1);
            }
            else if (command.starts_with("context:"))
            {
                const bool accepted = storm::android::mobileScreen.empty() && command.substr(8) == storm::android::contextAction;
                storm::android::traceContextAction("receive", command.substr(8),
                    std::string("accepted=") + (accepted ? "1" : "0") + " current=" + storm::android::contextAction);
                if (accepted)
                    storm::android::requestedContextAction = command.substr(8);
            }
            else if (command.starts_with("dialogue:"))
            {
                if (storm::android::dialogueActions.size() < 8)
                    storm::android::dialogueActions.push_back(command.substr(9));
            }
            else if (command.starts_with("widget:"))
            {
                std::istringstream stream(command.substr(7));
                std::string screen, node, indexText, value;
                if (std::getline(stream, screen, '\t') && std::getline(stream, node, '\t') &&
                    std::getline(stream, indexText, '\t') && std::getline(stream, value) &&
                    screen == storm::android::mobileScreen)
                {
                    int index;
                    const auto parsed = std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
                    if (parsed.ec == std::errc() && parsed.ptr == indexText.data() + indexText.size())
                        storm::android::uiWidgetActions.push_back({screen, node, value, index});
                }
            }
            else if (command.starts_with("touch:"))
            {
                std::istringstream stream(command.substr(6));
                std::string screen;
                float x, y; int phase;
                if (stream >> screen >> x >> y >> phase && screen == storm::android::mobileScreen &&
                    x >= 0 && x <= 1 && y >= 0 && y <= 1 && phase >= 0 && phase <= 3)
                    storm::android::uiTouches.push_back({x, y, phase});
            }
        }
#endif
        SDL_PumpEvents();
        window->PumpEvents();
        // Queued window/lifecycle events, delivered between frames on this thread.
        storm::AppEvents::instance().dispatch();
        audioGate.apply();
#ifdef __ANDROID__
        SDL_Event appEvent;
        while (SDL_PeepEvents(&appEvent, 1, SDL_GETEVENT, SDL_APP_TERMINATING,
                              SDL_APP_DIDENTERFOREGROUND) > 0)
        {
            androidLifecycle.consume(appEvent.type);
            if (appEvent.type == SDL_APP_WILLENTERBACKGROUND || appEvent.type == SDL_APP_DIDENTERBACKGROUND ||
                appEvent.type == SDL_APP_TERMINATING)
            {
                storm::android::cancelAndroidCombatInput();
                storm::android::closeSeaOrders();
                storm::android::closeLandCommands();
                storm::android::publishLandCommands();
                storm::android::seaQuickLifecycleReady = false;
            }
        }
        if (SDL_HasEvent(SDL_QUIT))
            androidLifecycle.consume(SDL_QUIT);
        // Audio pause/resume for the lifecycle goes through AppEvents and audioGate.
        if (!androidLifecycle.canRun() || !storm::android::mobileScreen.empty())
            gameplayShortcuts.clear();
        if (!androidLifecycle.canRun() || !storm::android::mobileScreen.empty() || storm::android::loadingState != 0)
        {
            storm::android::closeSeaOrders();
            storm::android::closeLandCommands();
            storm::android::publishLandCommands();
            storm::android::publishSeaOrders();
            storm::android::movementInput.updateOwner({});
            storm::android::GameplaySnapshot blocked;
            blocked.blocked = notReady();
            gameplayState.update(std::move(blocked));
            storm::android::heldControls.update(gameplayState.revision());
            storm::android::updateCombatState(gameplayState, core_private);
            storm::android::publishCombatState();
            storm::android::publishGameplayState(gameplayState.json());
            storm::android::worldMapHud.update({},SDL_GetTicks());
            storm::android::publishWorldMapHud();
            storm::android::shipTelemetry.update(gameplayState.revision(), {});
            storm::android::publishShipTelemetry();
            storm::android::seaQuickLifecycleReady = false;
            storm::android::seaQuickAction.update({});
            storm::android::publishSeaQuickAction();
        }
        if (androidLifecycle.terminating())
            isRunning = false;
        if(storm::android::worldMapHud.modal.ownsInput(SDL_GetTicks()))storm::android::publishWorldMapHud();
        storm::android::worldMapHud.resume(bActive,SDL_GetTicks());
        const bool mapModalHold=storm::android::worldMapHud.modal.hold(bActive,SDL_GetTicks());
#else
        // SDL turns SIGINT/SIGTERM into SDL_QUIT: the host shuts down in order, like closing the window.
        if (SDL_HasEvent(SDL_QUIT))
        {
            spdlog::info("Quit requested (SIGINT/SIGTERM)");
            isRunning = false;
        }
#endif
        SDL_FlushEvents(0, SDL_LASTEVENT);
        if (!isRunning)
            break;

        if ((bActive || run_in_background)
#ifdef __ANDROID__
            && androidLifecycle.canRun() && !mapModalHold
#endif
        )
        {
            frameWork.frame();

#ifdef __ANDROID__
            // Activity changes revoke queued intents before any native Player
            // or Control Activation handler can consume the next frame.
            storm::android::publishCombatState();
            // HUD input reaches the script at the top of the frame, before RunFrame, so that
            // what it opens or changes is drawn and published (xinterface Realize) in this same
            // frame. Taken after RunFrame, it waited for the next frame to show anything.
            if (const auto action = gameplayState.take(SDL_GetTicks()))
            {
                STORM_PROFILE_ZONE("HUD command: gameplay action");
                STORM_PROFILE_ZONE_TEXT(action->data(), action->size());
                // A battle ability goes through the command list, as the native menu launches it.
                if (action->starts_with("BI_"))
                    storm::android::launchShipAbility(*action);
                else if (const int step = storm::android::timeScaleStep(*action); step >= 0)
                    storm::android::applyTimeScaleStep(step);
                else
                    core_private->Event("Control Activation", "s", action->c_str());
            }
            // Commands run at a game-frame boundary, outside Android dialog callbacks: no
            // timed key pulse or held bit can be lost between two slow native frames.
            if (!storm::android::mobileScreen.empty()) gameplayShortcuts.clear();
            const bool shortcutReady = isRunning && bActive && androidLifecycle.canRun() &&
                core_private->initialized() && core.Controls && storm::android::loadingState == 0 &&
                storm::android::mobileScreen.empty();
            if (const auto shortcut = gameplayShortcuts.take(SDL_GetTicks(), shortcutReady))
            {
                STORM_PROFILE_ZONE("HUD command: shortcut");
                const auto *control = storm::android::shortcutControl(*shortcut);
                const auto *screen = storm::android::shortcutScreen(*shortcut);
                const std::string_view shortcutName = control ? control : screen;
                STORM_PROFILE_ZONE_TEXT(shortcutName.data(), shortcutName.size());
                if (shortcutLogBudget > 0)
                {
                    --shortcutLogBudget;
                    std::string dialogRun = "<missing>", launched = "<missing>", resume = "<missing>";
                    try
                    {
                        if (auto *value = static_cast<VDATA *>(core.GetScriptVariable("dialogRun")))
                            dialogRun = std::to_string(value->GetInt());
                        auto *state = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates"));
                        auto *attributes = state ? state->GetAClass() : nullptr;
                        const auto read = [attributes](const char *path) -> std::string {
                            auto *attribute = attributes ? attributes->FindAClass(attributes, path) : nullptr;
                            return attribute && attribute->HasValue() ? attribute->GetValue().substr(0, 32) : "<missing>";
                        };
                        launched = read("Launched");
                        resume = read("buttons.resume.enable");
                    }
                    catch (...) { /* Diagnostics must never alter command delivery or guards. */ }
                    spdlog::info("Android shortcut {}: {} dialogRun={} InterfaceStates.Launched={} resume.enable={}",
                                 control ? "Control Activation" : "AndroidOpenScreen", control ? control : screen, dialogRun, launched, resume);
                }
                // Use exactly the ordinary control event, or the app adapter's screen opening
                // (android_open_screen.c). Script guards (dialogRun, resume.enable, fight mode,
                // etc.) still decide whether to act.
                if (control)
                    core_private->Event("Control Activation", "s", control);
                else
                    core_private->Event("AndroidOpenScreen", "s", screen);
            }
#endif
            // Exceptions escaping a frame are reported and end the session in order
            // instead of reaching std::terminate; see ReportFatalError.
            try
            {
#ifdef __ANDROID__
                storm::android::worldMapHud.beginDraw(SDL_GetTicks());
#endif
                STORM_PROFILE_ZONE("frame: script, entities, render");
                RunFrame();
            }
            catch (const std::exception &error)
            {
                ReportFatalError(error.what());
            }
            catch (...)
            {
                ReportFatalError("unknown error");
            }
            if (fatalError)
            {
                exitCode = EXIT_FAILURE;
                break;
            }
#ifdef __ANDROID__
            const bool gameplayReady = isRunning && bActive && androidLifecycle.canRun() &&
                core_private->initialized() && core.Controls && storm::android::loadingState == 0 &&
                storm::android::mobileScreen.empty();
            {
                STORM_PROFILE_ZONE("HUD adapter: sea orders");
                storm::android::refreshSeaOrders(storm::android::seaOrdersReady(gameplayReady));
                // Detect Activity replacement before accepting any queued action.
                storm::android::publishSeaOrders();
                if (const auto request = storm::android::seaOrders.take(SDL_GetTicks()))
                {
                    storm::android::executeSeaOrder(*request);
                    storm::android::refreshSeaOrders(storm::android::seaOrdersReady(gameplayReady));
                    storm::android::publishSeaOrders();
                }
            }
            {
                STORM_PROFILE_ZONE("HUD adapter: land commands");
                storm::android::refreshLandCommands(storm::android::landCommandsReady(gameplayReady));
                storm::android::publishLandCommands();
                if (const auto request = storm::android::landCommands.take(SDL_GetTicks())) {
                    storm::android::executeLandCommand(*request);
                    storm::android::refreshLandCommands(storm::android::landCommandsReady(gameplayReady));
                    storm::android::publishLandCommands();
                }
            }
            STORM_PROFILE_ZONE("HUD adapter: gameplay, combat, map, ship");
            auto gameplaySnapshot = storm::android::readGameplaySnapshot(gameplayReady);
            if (!gameplayReady)
                gameplaySnapshot.blocked = notReady(); // the same reason as the blocked branch above: one state per loop
            storm::android::movementInput.updateOwner(gameplaySnapshot.inputOwner);
            gameplaySnapshot.analogMovement = !gameplaySnapshot.inputOwner.empty();
            gameplaySnapshot.movementGeneration = gameplaySnapshot.analogMovement ? storm::android::movementInput.generation() : 0;
            gameplayState.update(std::move(gameplaySnapshot));
            storm::android::heldControls.update(gameplayState.revision());
            storm::android::seaQuickLifecycleReady = gameplayReady;
            storm::android::publishSeaQuickAction();
            storm::android::updateCombatState(gameplayState, core_private);
            storm::android::publishCombatState();
            storm::android::publishGameplayState(gameplayState.json());
            storm::android::worldMapHud.update(storm::android::readWorldMapFrame(gameplayState,gameplayReady),SDL_GetTicks());
            storm::android::worldMapHud.finishDraw(SDL_GetTicks());
            if(storm::android::worldMapHud.modal.receipt(SDL_GetTicks()).token){
                storm::android::traceWorldMapModal("presented",storm::android::worldMapHud.modal.debug());
            }
            storm::android::publishWorldMapHud(storm::android::worldMapHud.modal.receipt(SDL_GetTicks()).token!=0);
            {
                // A presenting UI answers each revision within one of its frames; one that stays
                // silent is withdrawn loudly and the native HUD parts come back (android_hud_presenter.hpp).
                using namespace storm::android;
                const auto &telemetry = shipTelemetry;
                const bool seaWaiting = gameplayReady && (seaQuickAction.awaiting() || telemetry.awaiting() ||
                    (telemetry.frame().valid && seaNavigator.pending(telemetry.revision(), telemetry.hudRevision(), telemetry.frame().navigatorGeneration)));
                for (const auto &line : {hudPresenters.watch("sea", seaWaiting, SDL_GetTicks()),
                                         hudPresenters.watch("worldmap", gameplayReady && worldMapHud.awaiting(), SDL_GetTicks()),
                                         hudPresenters.watch("spyglass", gameplayReady && spyglass.awaiting(), SDL_GetTicks())})
                    if (line) __android_log_print(ANDROID_LOG_ERROR, "StormInput", "%s", line->c_str());
            }
            if(storm::android::worldMapHud.modal.hold(bActive,SDL_GetTicks()))continue;
            if(const auto command=storm::android::worldMapHud.take(SDL_GetTicks()))
            {
                storm::android::executeWorldMapCommand(*command);
                storm::android::worldMapHud.update(storm::android::readWorldMapFrame(gameplayState,gameplayReady),SDL_GetTicks());
                storm::android::publishWorldMapHud();
            }
            storm::android::shipTelemetry.update(gameplayState.revision(), storm::android::readShipFrame(gameplayState, SDL_GetTicks(), storm::android::seaOrders.frame().active));
            // Activity recreation revokes pending targets/ready ACKs before dispatch.
            storm::android::publishShipTelemetry();
            if (const auto detent = storm::android::shipTelemetry.takeSails(SDL_GetTicks()))
                if (storm::android::applyShipSailTarget(gameplayState, *detent))
                {
                    storm::android::shipTelemetry.update(gameplayState.revision(), storm::android::readShipFrame(gameplayState, SDL_GetTicks(), storm::android::seaOrders.frame().active));
                    storm::android::publishShipTelemetry();
                }
            static uint32_t lastMobileState = 0;
            if (core_private->initialized() && SDL_GetTicks() - lastMobileState >= 200) {
                lastMobileState = SDL_GetTicks();
                bool cinematic = false;
                auto *index = static_cast<VDATA *>(core.GetScriptVariable("nMainCharacterIndex"));
                auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
                if (index && characters && index->GetInt() >= 0) {
                    if (auto *character = characters->GetAClass(index->GetInt())) {
                        auto *type = character->FindAClass(character, "chr_ai.type");
                        cinematic = type && type->HasValue() && std::string_view(type->GetValue()) == "actor";
                    }
                }
                storm::android::publishCinematic(cinematic || core.GetEntityId("CAviPlayer") != invalid_entity || core.GetEntityId("WMVideoPlay") != invalid_entity);
            }
#endif
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (core.Controls && core.Controls->GetDebugAsyncKeyState(VK_F1) && core.Controls->GetDebugAsyncKeyState(VK_SHIFT))
        {
            mi_stats_print_out(mimalloc_fun, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Release
#ifdef __ANDROID__
    storm::android::combatInput.clear();
    storm::android::combatInput.guard = {};
    storm::android::movementInput.guard = {};
    storm::android::heldControls.guard = {};
    storm::android::closeSeaOrders();
    storm::android::closeLandCommands();
    storm::android::publishLandCommands();
    storm::android::publishSeaOrders();
    storm::android::movementInput.updateOwner({});
    {
        storm::android::GameplaySnapshot stopping;
        stopping.blocked = "starting"; // the engine is going down: nothing of the game is usable
        gameplayState.update(std::move(stopping));
    }
    storm::android::publishGameplayState(gameplayState.json());
    storm::android::worldMapHud.update({},SDL_GetTicks());
    storm::android::publishWorldMapHud();
    storm::android::shipTelemetry.update(gameplayState.revision(), {});
    storm::android::publishShipTelemetry();
    storm::android::ui::shutdown(); // its textures and frame client go before the renderer
#endif
    storm::AppEvents::instance().uninstall();
    // After a fatal error the engine state is unknown: still try to shut down in
    // order (services flush files), but never let a second exception abort.
    try
    {
        if (exitCode == EXIT_SUCCESS)
            core_private->Event("ExitApplication");
        core_private->CleanUp();
        core_private->ReleaseBase();
    }
    catch (const std::exception &error)
    {
        spdlog::critical("Exception during shutdown: {}", error.what());
        exitCode = EXIT_FAILURE;
    }
    catch (...)
    {
        spdlog::critical("Unknown exception during shutdown");
        exitCode = EXIT_FAILURE;
    }
    if (auto logger = spdlog::default_logger())
        logger->flush();
#if _DEBUG
    mi_option_set(mi_option_verbose, 0); // disable statistics writing in Linux
#endif
    SDL_Quit();

    return exitCode;
}
