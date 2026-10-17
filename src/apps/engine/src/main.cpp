#ifdef STORM_ENABLE_BGFX
#include "fog_preview.hpp"
#endif
#include <thread>

#include <SDL2/SDL.h>
#ifndef __ANDROID__
#include <mimalloc-new-delete.h>
#endif
#include <mimalloc.h>
#include <spdlog/spdlog.h>

#include "core_private.h"
#include "lifecycle_diagnostics_service.hpp"
#include "logging.hpp"
#include "os_window.hpp"
#include "steam_api.hpp"
#include "v_sound_service.h"
#include "fs.h"
#include "watermark.hpp"
#ifdef __ANDROID__
#include "platform/android_ui.hpp"
#include "platform/android_shortcuts.hpp"
#include "android_gameplay_state.hpp"
#include "android_sea_orders_state.hpp"
#include <sstream>
#include <charconv>
#endif

namespace
{

CorePrivate *core_private;

constexpr char defaultLoggerName[] = "system";
bool isRunning = false;
bool bActive = true;
bool bSoundInBackground = false;

storm::diag::LifecycleDiagnosticsService lifecycleDiagnostics;

void RunFrame()
{
    if (!core_private->Run())
    {
        isRunning = false;
    }

    lifecycleDiagnostics.notifyAfterRun();
}

#ifdef _WIN32
void RunFrameWithOverflowCheck()
{
    __try
    {
        RunFrame();
    }
    __except ([](unsigned code, struct _EXCEPTION_POINTERS *ep) {
        return code == EXCEPTION_STACK_OVERFLOW;
    }(GetExceptionCode(), GetExceptionInformation()))
    {
        _resetstkoflw();
        throw std::runtime_error("Stack overflow");
    }
}
#else
#define RunFrameWithOverflowCheck RunFrame
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
#ifdef _MSC_VER
        _wfopen(mimalloc_log_path.c_str(), L"a+b");
#else
        fopen(mimalloc_log_path.c_str(), "a+b");
#endif
    if (mimalloc_log != nullptr)
    {
        fputs(msg, mimalloc_log);
        fclose(mimalloc_log);
    }
}

} // namespace

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
        if (core_private->initialized())
        {
            core_private->AppState(bActive);
            if (const auto soundService = static_cast<VSoundService *>(core.GetService("SoundService"));
                soundService && !bSoundInBackground)
            {
                soundService->SetActiveWithFade(true);
            }
        }
    }
    else if (event == storm::OSWindow::FocusLost)
    {
        bActive = false;
        if (core_private->initialized())
        {
            core_private->AppState(bActive);
            if (const auto soundService = static_cast<VSoundService *>(core.GetService("SoundService"));
                soundService && !bSoundInBackground)
            {
                soundService->SetActiveWithFade(false);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    // Prevent multiple instances
#ifdef _WIN32 // CreateEventA
    if (!CreateEventA(nullptr, false, false, "Global\\FBBD2286-A9F1-4303-B60C-743C3D7AA7BE") ||
        GetLastError() == ERROR_ALREADY_EXISTS)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Another instance is already running!", nullptr);
        return EXIT_SUCCESS;
    }
#endif
#ifdef __ANDROID__
    try
    {
        storm::android::prepareGameDirectory();
        // Build the resource index only after JNI and the game directory are ready.
        fio->ScanResourcePaths();
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

    SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

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
    spdlog::info("mimalloc-redirect status: {}", mi_is_redirected());

    // Init core
    core_private = static_cast<CorePrivate *>(&core);
    core_private->Init();

    // Read config
    auto ini = fio->OpenIniFile(fs::ENGINE_INI_FILE_NAME);

    uint32_t dwMaxFPS = 0;
    bool bSteam = false;
    int width = 1024, height = 768;
    int preferred_display = 0;
    bool fullscreen = false;
    bool show_borders = false;
    bool run_in_background = false;

    if (ini)
    {
        dwMaxFPS = static_cast<uint32_t>(ini->GetInt(nullptr, "max_fps", 0));
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
        bSteam = ini->GetInt(nullptr, "Steam", 1) != 0;
    }

#ifdef STORM_ENABLE_BGFX
    // This mode branches before OSWindow/InitBase: DXVK never owns this surface.
    char fogPreview[1024]{}, fogCapture[1024]{}, fogShaders[1024]{};
    if (ini) {
        ini->ReadString(nullptr, "bgfx_fog_preview_file", fogPreview, sizeof(fogPreview), "");
        ini->ReadString(nullptr, "bgfx_fog_capture_file", fogCapture, sizeof(fogCapture), "");
        ini->ReadString(nullptr, "bgfx_shader_directory", fogShaders, sizeof(fogShaders), "");
    }
    if (fogPreview[0]) {
        storm::render::FogPreviewOptions options;
        options.snapshotPath = fogPreview;
        options.shaderDirectory = fogShaders;
        options.width = std::max(320, width);
        options.height = std::max(180, height);
        options.vsync = ini->GetInt(nullptr, "vsync", 1) != 0;
        options.fogEnabled = ini->GetInt(nullptr, "bgfx_preview_fog", 1) != 0;
#ifdef __ANDROID__
        options.width = std::max(320, storm::android::graphicsSetting("screen_x", options.width));
        options.height = std::max(180, storm::android::graphicsSetting("screen_y", options.height));
        options.vsync = storm::android::graphicsSetting("vsync", options.vsync) != 0;
#endif
        core_private->CleanUp();
        core_private->ReleaseBase();
        const int result = storm::render::runFogPreview(options);
        SDL_Quit();
        return result;
    }
    storm::render::configureFogCapture(fogCapture);
#endif

    // initialize SteamApi through evaluating its singleton
    try
    {
        steamapi::SteamApi::getInstance(!bSteam);
    }
    catch (const std::exception &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }

    std::shared_ptr<storm::OSWindow> window =
        storm::OSWindow::Create(width, height, preferred_display, fullscreen, show_borders);
    window->SetTitle("Sea Dogs");
    window->Subscribe(HandleWindowEvent);
    window->Show();
    core_private->SetWindow(window);

    // Init core
    core_private->InitBase();

    // Message loop
    auto dwOldTime = SDL_GetTicks();

#ifdef __ANDROID__
    storm::android::Lifecycle androidLifecycle;
    storm::android::GameplayShortcuts gameplayShortcuts;
    storm::android::GameplayState gameplayState;
    gameplayState.update({});
    storm::android::publishGameplayState(gameplayState.json());
    storm::android::shipTelemetry.update(gameplayState.revision(), {});
    storm::android::publishShipTelemetry();
    unsigned shortcutLogBudget = 64;
#endif
    isRunning = true;
    while (isRunning)
    {
#ifdef __ANDROID__
        for (const auto &command : storm::android::pollMobileActions())
        {
            if (command.starts_with("orders:"))
            {
                storm::android::seaOrders.offer(std::string_view(command).substr(7), SDL_GetTicks());
            }
            else if (command.starts_with("shiphud:"))
            {
                storm::android::shipTelemetry.acknowledge(std::string_view(command).substr(8));
            }
            else if (command.starts_with("sails:"))
            {
                storm::android::shipTelemetry.offerSails(std::string_view(command).substr(6), SDL_GetTicks());
            }
            else if (command.starts_with("gameplay:"))
            {
                gameplayState.offer(std::string_view(command).substr(9), SDL_GetTicks());
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
            else if (command == "back" && !storm::android::mobileScreen.empty())
                core_private->Event("exitCancel");
            else if (command.starts_with("context:") && storm::android::mobileScreen.empty())
            {
                if (command.substr(8) == storm::android::contextAction)
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
#ifdef __ANDROID__
        const bool wasRunning = androidLifecycle.canRun();
        SDL_Event appEvent;
        while (SDL_PeepEvents(&appEvent, 1, SDL_GETEVENT, SDL_APP_TERMINATING,
                              SDL_APP_DIDENTERFOREGROUND) > 0)
        {
            androidLifecycle.consume(appEvent.type);
            if (appEvent.type == SDL_APP_WILLENTERBACKGROUND || appEvent.type == SDL_APP_DIDENTERBACKGROUND ||
                appEvent.type == SDL_APP_TERMINATING)
                storm::android::closeSeaOrders();
        }
        if (SDL_HasEvent(SDL_QUIT))
            androidLifecycle.consume(SDL_QUIT);
        if (wasRunning != androidLifecycle.canRun() && core_private->initialized())
            if (auto* audio = static_cast<VSoundService*>(core.GetService("SoundService")))
                audio->SetActiveWithFade(androidLifecycle.canRun());
        if (!androidLifecycle.canRun() || !storm::android::mobileScreen.empty())
            gameplayShortcuts.clear();
        if (!androidLifecycle.canRun() || !storm::android::mobileScreen.empty() || storm::android::loadingState != 0)
        {
            storm::android::closeSeaOrders();
            storm::android::publishSeaOrders();
            gameplayState.update({});
            storm::android::publishGameplayState(gameplayState.json());
            storm::android::shipTelemetry.update(gameplayState.revision(), {});
            storm::android::publishShipTelemetry();
        }
        if (androidLifecycle.terminating())
            isRunning = false;
#endif
        SDL_FlushEvents(0, SDL_LASTEVENT);
        if (!isRunning)
            break;

        if ((bActive || run_in_background)
#ifdef __ANDROID__
            && androidLifecycle.canRun()
#endif
        )
        {
            if (dwMaxFPS)
            {
                const auto dwMS = 1000u / dwMaxFPS;
                const auto dwNewTime = SDL_GetTicks();
                if (dwNewTime - dwOldTime < dwMS)
                    continue;
                dwOldTime = dwNewTime;
            }

            RunFrameWithOverflowCheck();
#ifdef STORM_ENABLE_BGFX
            static bool fogCaptureKeyDown = false;
            const bool fogCaptureKey = core.Controls && core.Controls->GetDebugAsyncKeyState(VK_F11) != 0;
            if (fogCaptureKey && !fogCaptureKeyDown) storm::render::requestFogCapture();
            fogCaptureKeyDown = fogCaptureKey;
#endif
#ifdef __ANDROID__
            const bool gameplayReady = isRunning && bActive && androidLifecycle.canRun() &&
                core_private->initialized() && core.Controls && storm::android::loadingState == 0 &&
                storm::android::mobileScreen.empty();
            storm::android::refreshSeaOrders(storm::android::seaOrdersReady(gameplayReady));
            // Detect Activity replacement before accepting any queued action.
            storm::android::publishSeaOrders();
            if (const auto request = storm::android::seaOrders.take(SDL_GetTicks()))
            {
                storm::android::executeSeaOrder(*request);
                storm::android::refreshSeaOrders(storm::android::seaOrdersReady(gameplayReady));
                storm::android::publishSeaOrders();
            }
            gameplayState.update(storm::android::readGameplaySnapshot(gameplayReady));
            storm::android::publishGameplayState(gameplayState.json());
            storm::android::shipTelemetry.update(gameplayState.revision(), storm::android::readShipTelemetry(gameplayState));
            // Activity recreation revokes pending targets/ready ACKs before dispatch.
            storm::android::publishShipTelemetry();
            if (const auto detent = storm::android::shipTelemetry.takeSails(SDL_GetTicks()))
                if (storm::android::applyShipSailTarget(gameplayState, *detent))
                {
                    storm::android::shipTelemetry.update(gameplayState.revision(), storm::android::readShipTelemetry(gameplayState));
                    storm::android::publishShipTelemetry();
                }
            if (const auto action = gameplayState.take(SDL_GetTicks()))
                core_private->Event("Control Activation", "s", action->c_str());
            // Execute commands at a completed game-frame boundary, after control
            // polling and outside Android dialog callbacks. No timed key pulse
            // or held bit can be lost between two slow native frames.
            if (!storm::android::mobileScreen.empty()) gameplayShortcuts.clear();
            const bool shortcutReady = isRunning && bActive && androidLifecycle.canRun() &&
                core_private->initialized() && core.Controls && storm::android::loadingState == 0 &&
                storm::android::mobileScreen.empty();
            if (const auto shortcut = gameplayShortcuts.take(SDL_GetTicks(), shortcutReady))
            {
                const auto *control = storm::android::shortcutControl(*shortcut);
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
                    spdlog::info("Android shortcut Control Activation: {} dialogRun={} InterfaceStates.Launched={} resume.enable={}",
                                 control, dialogRun, launched, resume);
                }
                // Use exactly the ordinary control event. Script guards (dialogRun,
                // resume.enable, fight mode, etc.) still decide whether to act.
                core_private->Event("Control Activation", "s", control);
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
    storm::android::closeSeaOrders();
    storm::android::publishSeaOrders();
    gameplayState.update({});
    storm::android::publishGameplayState(gameplayState.json());
    storm::android::shipTelemetry.update(gameplayState.revision(), {});
    storm::android::publishShipTelemetry();
#endif
    core_private->Event("ExitApplication");
    core_private->CleanUp();
    core_private->ReleaseBase();
#ifdef _WIN32 // FIX_LINUX Cursor
    ClipCursor(nullptr);
#elif _DEBUG
    mi_option_set(mi_option_verbose, 0); // disable statistics writing in Linux
#endif
    SDL_Quit();

    return EXIT_SUCCESS;
}
