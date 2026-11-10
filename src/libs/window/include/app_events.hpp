#pragma once

// Application window and lifecycle events: one subscription point for every
// subsystem (renderer surface handling, UI layout, audio, autosave, caches).
//
//   #include "app_events.hpp"
//   const int id = storm::AppEvents::instance().subscribe([](const storm::AppEvent &e) {
//       if (e.type == storm::AppEventType::WindowResized) relayout(e.width, e.height);
//   });
//   ...
//   storm::AppEvents::instance().unsubscribe(id);
//
// Threading contract
// ------------------
// Every handler runs on the game thread (the thread that called install()).
// SDL raises some events on other threads (on Android: focus, resize, low
// memory and terminate come from the Java UI thread). Those are queued and
// delivered by dispatch(), which the main loop calls once per iteration right
// after SDL_PumpEvents(), outside of any frame.
//
// Normal vs early subscribers
// ---------------------------
// subscribe(handler)        delivered by dispatch(). Use this by default.
// subscribe(handler, true)  "early": if SDL raises the event on the game
//                           thread, the handler runs immediately, inside the
//                           SDL call that raised it; otherwise it runs at the
//                           start of the next dispatch(), before normal
//                           subscribers. Each event reaches a handler once.
//
// Android pause, precisely (SDL 2.0.18, SDL_androidevents.c): SDL raises
// WindowMinimized, WillEnterBackground and DidEnterBackground on the game
// thread from inside SDL_PumpEvents(), returns, and blocks that thread in a
// later SDL_PumpEvents() once DidEnterBackground has left SDL's queue. With
// the engine's main loop (pump, dispatch(), flush) a normal subscriber
// therefore still runs before the thread is suspended, one dispatch() later
// than an early one. Use an early subscriber when the work must be finished
// before control returns to SDL at all (releasing a surface/swapchain that the
// OS is about to destroy), and keep it quick: Android may kill a paused
// process. Early handlers must tolerate being called from inside an SDL
// function on the game thread.
//
// Event order on Android
// ----------------------
//   Home/recents:  WindowFocusLost*, WillEnterBackground, DidEnterBackground
//                  ... suspended ... WillEnterForeground, DidEnterForeground,
//                  WindowFocusGained*            (* from the Java thread, so
//                  their position relative to the lifecycle pair can vary)
//   Rotation / fold / multi-window: WindowResized (latest size only),
//                  DisplayOrientationChanged
//   onTrimMemory/onLowMemory: LowMemory (delivered on resume if suspended)
//   Task removed:  Terminating
//
// The queue is bounded: WindowResized keeps only the newest size, LowMemory is
// not duplicated, and when the queue is full the oldest non-lifecycle event is
// dropped. Lifecycle events are never dropped.

#include <functional>

union SDL_Event;

namespace storm
{
enum class AppEventType
{
    WindowResized,             // width/height: new drawable size in pixels
    WindowFocusGained,
    WindowFocusLost,
    WindowMinimized,
    WindowRestored,
    DisplayOrientationChanged, // orientation: SDL_DisplayOrientation, display: index
    WillEnterBackground,       // last chance to release surfaces / pause / save (early subscribers)
    DidEnterBackground,
    WillEnterForeground,
    DidEnterForeground,        // surfaces may be recreated, audio may resume
    LowMemory,                 // free caches
    Terminating,               // the OS is ending the process
};

struct AppEvent
{
    AppEventType type{};
    int width = 0;
    int height = 0;
    int orientation = 0;
    int display = 0;
    unsigned windowId = 0;
    // true while the event is being delivered inside the SDL call that raised
    // it (early subscribers only); false when delivered by dispatch().
    bool immediate = false;
};

const char *toString(AppEventType type);

class AppEvents
{
  public:
    using Handler = std::function<void(const AppEvent &)>;

    static AppEvents &instance();

    // Game thread, after SDL's event subsystem is initialized. Idempotent.
    void install();
    // Stops listening and drops queued events; subscriptions are kept.
    void uninstall();

    // Returns an id for unsubscribe(). Safe to call from handlers.
    int subscribe(Handler handler, bool early = false);
    void unsubscribe(int id);

    // Deliver queued events. Game thread only; call once per main-loop iteration.
    void dispatch();

    // Queue an event from any thread (platform bridges, tests). Early
    // subscribers are called immediately when this is the game thread.
    void post(const AppEvent &event);

    // SDL -> AppEvent mapping; false for events that are not application events.
    static bool translate(const SDL_Event &source, AppEvent &event);

  private:
    AppEvents();
    ~AppEvents();
    struct State;
    State *state_;
};
} // namespace storm
