#ifdef STORM_SCENE_SWEEP
#include "scene_sweep.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "compiler.h"
#include "core.h"
#include "s_import_func.h"
#include "script_libriary.h"
#include "v_s_stack.h"
#include "vma.hpp"

// Protocol with tools/scene_sweep/run.py, all inside $STORM_SCENE_SWEEP_DIR:
//   scenes.txt  one scene per line, read by SceneSweepNext()
//   events.log  "<ms since start> <text>" lines appended by SceneSweepEvent()/SceneSweepCapture()
//   STORM_SCENE_SWEEP_SETTLE_MS  rendering time per scene before its capture (default 300)
//   ack         created by the runner once it has taken the screenshot and read the logs
namespace
{

using Clock = std::chrono::steady_clock;

std::filesystem::path directory;
std::ifstream scenes;
std::FILE *events = nullptr;
const Clock::time_point started = Clock::now();
Clock::time_point windowStart = started;
uint32_t windowFrames = 0;
uint32_t lastDrawCalls = 0;
std::function<std::string()> rendererWindow;
std::vector<std::string> variants; // STORM_SCENE_SWEEP_VARIANTS split at ';'
int currentVariant = -1;
storm::scene_sweep::VariantHook variantHook = nullptr;

void writeEvent(const std::string &text)
{
    // The runner attributes log lines to scenes by reading the logs at each event.
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });
    std::fflush(stdout);
    std::fflush(stderr);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    std::fprintf(events, "%lld %s\n", static_cast<long long>(ms), text.c_str());
    std::fflush(events);
}

uint32_t next(VS_STACK *stack)
{
    auto *result = static_cast<VDATA *>(stack->Push());
    if (!result)
        return IFUNCRESULT_FAILED;
    std::string scene;
    std::getline(scenes, scene);
    result->Set(scene);
    return IFUNCRESULT_OK;
}

uint32_t event(VS_STACK *stack)
{
    auto *text = static_cast<VDATA *>(stack->Pop());
    if (!text)
        return IFUNCRESULT_FAILED;
    writeEvent(text->GetString());
    windowStart = Clock::now();
    windowFrames = 0;
    if (rendererWindow)
        rendererWindow();
    return IFUNCRESULT_OK;
}

// Reports the frames rendered since the previous event and keeps the last frame on
// screen until the runner acknowledges that it has captured it.
uint32_t capture(VS_STACK *)
{
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - windowStart).count();
    writeEvent("capture frames=" + std::to_string(windowFrames) + " frame_ms=" +
               std::to_string(windowFrames ? ms / windowFrames : 0.0) + " draws=" + std::to_string(lastDrawCalls) +
               (currentVariant >= 0 ? " variant=" + std::to_string(currentVariant) : std::string()) +
               (rendererWindow ? " renderer: " + rendererWindow() : std::string()));
    const auto ack = directory / "ack";
    const auto deadline = Clock::now() + std::chrono::seconds(60);
    std::error_code error;
    while (!std::filesystem::remove(ack, error))
    {
        if (Clock::now() > deadline)
        {
            spdlog::critical("Scene sweep: the runner did not acknowledge a capture within 60 s");
            spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });
            std::_Exit(EXIT_FAILURE);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    windowStart = Clock::now();
    windowFrames = 0;
    return IFUNCRESULT_OK;
}

// Milliseconds a scene renders after its fader is gone before the capture (run.py --settle).
uint32_t variantCount(VS_STACK *stack)
{
    auto *result = static_cast<VDATA *>(stack->Push());
    if (!result)
        return IFUNCRESULT_FAILED;
    result->Set(static_cast<int32_t>(variants.size()));
    return IFUNCRESULT_OK;
}

uint32_t variant(VS_STACK *stack)
{
    auto *index = static_cast<VDATA *>(stack->Pop());
    if (!index)
        return IFUNCRESULT_FAILED;
    int32_t value = 0;
    index->Get(value);
    if (value < 0 || static_cast<size_t>(value) >= variants.size() || !variantHook)
    {
        spdlog::critical("Scene sweep: variant {} of {} cannot be applied (renderer hook set: {})", value,
                         variants.size(), variantHook != nullptr);
        spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });
        std::_Exit(EXIT_FAILURE);
    }
    currentVariant = value;
    variantHook(variants[value].c_str());
    return IFUNCRESULT_OK;
}

uint32_t settleMs(VS_STACK *stack)
{
    static const int value = [] {
        const char *text = std::getenv("STORM_SCENE_SWEEP_SETTLE_MS");
        return text && *text ? std::atoi(text) : 300;
    }();
    auto *result = static_cast<VDATA *>(stack->Push());
    if (!result)
        return IFUNCRESULT_FAILED;
    result->Set(static_cast<int32_t>(value));
    return IFUNCRESULT_OK;
}

void registerFunction(const char *name, const char *returns, uint32_t arguments, SIMPORTFUNC address)
{
    IFUNCINFO info;
    info.pFuncName = name;
    info.pReturnValueName = returns;
    info.nArguments = arguments;
    info.pFuncAddress = address;
    core.SetScriptFunction(&info);
}

// Loaded by `#libriary "SceneSweepLibrary"` in tools/scene_sweep/scene_sweep.c.
class SceneSweepLibrary : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override
    {
        scenes.open(directory / "scenes.txt");
        events = std::fopen((directory / "events.log").c_str(), "a");
        if (!scenes || !events)
        {
            spdlog::critical("Scene sweep: cannot open scenes.txt or events.log in '{}'", directory.string());
            spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });
            std::_Exit(EXIT_FAILURE);
        }
        registerFunction("SceneSweepNext", "string", 0, next);
        registerFunction("SceneSweepEvent", "void", 1, event);
        registerFunction("SceneSweepCapture", "void", 0, capture);
        registerFunction("SceneSweepSettleMs", "int", 0, settleMs);
        registerFunction("SceneSweepVariants", "int", 0, variantCount);
        registerFunction("SceneSweepVariant", "void", 1, variant);
        if (const char *list = std::getenv("STORM_SCENE_SWEEP_VARIANTS"); list && *list)
        {
            const std::string text = list;
            for (size_t start = 0, end; start <= text.size(); start = end + 1)
            {
                end = text.find(';', start);
                if (end == std::string::npos)
                    end = text.size();
                if (end > start)
                    variants.push_back(text.substr(start, end - start));
            }
        }
        return true;
    }
};

CREATE_SCRIPTLIBRIARY(SceneSweepLibrary)

} // namespace

void storm::scene_sweep::framePresented(uint32_t drawCalls)
{
    ++windowFrames;
    lastDrawCalls = drawCalls;
}

void storm::scene_sweep::setVariantHook(VariantHook hook)
{
    variantHook = hook;
}

void storm::scene_sweep::setRendererWindow(std::function<std::string()> window)
{
    rendererWindow = std::move(window);
}

void COMPILER::LoadSceneSweep()
{
    const char *path = std::getenv("STORM_SCENE_SWEEP_DIR");
    if (!path || !*path)
        return;
    directory = path;
    constexpr const char *segment = "scene_sweep.c";
    if (!BC_LoadSegment(segment))
    {
        spdlog::critical("Scene sweep: script segment '{}' did not load, see error.log", segment);
        spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });
        std::_Exit(EXIT_FAILURE);
    }
}
#endif
