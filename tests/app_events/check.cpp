#define SDL_MAIN_HANDLED
#include "app_events.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using storm::AppEvent;
using storm::AppEvents;
using storm::AppEventType;

static void push(Uint32 type)
{
    SDL_Event event{};
    event.type = type;
    assert(SDL_PushEvent(&event) == 1);
}

static void pushWindow(Uint8 kind, int a = 0, int b = 0)
{
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT;
    event.window.windowID = 7;
    event.window.event = kind;
    event.window.data1 = a;
    event.window.data2 = b;
    assert(SDL_PushEvent(&event) == 1);
}

int main()
{
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    auto &events = AppEvents::instance();
    events.install();
    events.install(); // idempotent: one watch, no duplicate deliveries
    const auto game = std::this_thread::get_id();

    struct Seen
    {
        AppEventType type;
        bool immediate;
        int width, height;
    };
    std::vector<Seen> normal, early;
    const int normalId = events.subscribe([&](const AppEvent &e) {
        assert(std::this_thread::get_id() == game);
        assert(!e.immediate);
        normal.push_back({e.type, e.immediate, e.width, e.height});
    });
    const int earlyId = events.subscribe(
        [&](const AppEvent &e) {
            assert(std::this_thread::get_id() == game);
            early.push_back({e.type, e.immediate, e.width, e.height});
        },
        true);
    const auto types = [](const std::vector<Seen> &seen) {
        std::vector<AppEventType> result;
        for (const auto &s : seen)
            result.push_back(s.type);
        return result;
    };

    // Android pause: SDL raises the pair on the game thread and then blocks it.
    // Early subscribers must have run before SDL_PushEvent returns; normal ones wait.
    push(SDL_APP_WILLENTERBACKGROUND);
    push(SDL_APP_DIDENTERBACKGROUND);
    assert((types(early) == std::vector{AppEventType::WillEnterBackground, AppEventType::DidEnterBackground}));
    assert(early[0].immediate && early[1].immediate && normal.empty());
    push(SDL_APP_WILLENTERFOREGROUND);
    push(SDL_APP_DIDENTERFOREGROUND);
    SDL_FlushEvents(0, SDL_LASTEVENT); // the main loop flushes SDL's queue; ours is independent
    events.dispatch();
    assert((types(normal) == std::vector{AppEventType::WillEnterBackground, AppEventType::DidEnterBackground,
                                         AppEventType::WillEnterForeground, AppEventType::DidEnterForeground}));
    assert(early.size() == 4); // not delivered a second time by dispatch()
    events.dispatch();
    assert(normal.size() == 4 && early.size() == 4);
    normal.clear();
    early.clear();

    // Events raised on another thread (Java UI thread on Android): nothing runs
    // there; both kinds of subscriber get them from dispatch(), early first.
    std::thread java([&] {
        pushWindow(SDL_WINDOWEVENT_FOCUS_LOST);
        for (int i = 1; i <= 50; ++i)
            pushWindow(SDL_WINDOWEVENT_SIZE_CHANGED, 100 + i, 200 + i);
        push(SDL_APP_LOWMEMORY);
        push(SDL_APP_LOWMEMORY);
        pushWindow(SDL_WINDOWEVENT_MINIMIZED);
        pushWindow(SDL_WINDOWEVENT_RESTORED);
        pushWindow(SDL_WINDOWEVENT_FOCUS_GAINED);
        pushWindow(SDL_WINDOWEVENT_EXPOSED); // not an application event
        push(SDL_KEYDOWN);
    });
    java.join();
    assert(normal.empty() && early.empty());
    events.dispatch();
    const std::vector expected{AppEventType::WindowFocusLost,  AppEventType::WindowResized,
                               AppEventType::LowMemory,        AppEventType::WindowMinimized,
                               AppEventType::WindowRestored,   AppEventType::WindowFocusGained};
    assert(types(normal) == expected && types(early) == expected);
    assert(normal[1].width == 150 && normal[1].height == 250); // only the last size of the burst
    assert(!early[0].immediate);
    normal.clear();
    early.clear();

    // Display orientation.
    SDL_Event display{};
    display.type = SDL_DISPLAYEVENT;
    display.display.event = SDL_DISPLAYEVENT_ORIENTATION;
    display.display.display = 0;
    display.display.data1 = SDL_ORIENTATION_LANDSCAPE_FLIPPED;
    AppEvent translated;
    assert(AppEvents::translate(display, translated) &&
           translated.type == AppEventType::DisplayOrientationChanged &&
           translated.orientation == SDL_ORIENTATION_LANDSCAPE_FLIPPED);
    SDL_Event key{};
    key.type = SDL_KEYDOWN;
    assert(!AppEvents::translate(key, translated));

    // Flood from a foreign thread: bounded, lifecycle events survive, order kept.
    std::thread flood([&] {
        push(SDL_APP_TERMINATING);
        for (int i = 0; i < 5000; ++i)
            pushWindow(i % 2 ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST);
        push(SDL_APP_LOWMEMORY);
    });
    flood.join();
    SDL_FlushEvents(0, SDL_LASTEVENT);
    events.dispatch();
    assert(normal.size() <= 64);
    assert(normal.front().type == AppEventType::Terminating && normal.back().type == AppEventType::LowMemory);
    assert(normal[normal.size() - 2].type == AppEventType::WindowFocusGained); // latest focus state retained
    normal.clear();
    early.clear();

    // Handlers may subscribe, unsubscribe and post while being called.
    int extraCalls = 0, extraId = 0;
    const int reentrant = events.subscribe([&](const AppEvent &e) {
        if (e.type == AppEventType::WindowMinimized)
        {
            extraId = events.subscribe([&](const AppEvent &) { ++extraCalls; });
            AppEvent follow;
            follow.type = AppEventType::WindowRestored;
            events.post(follow);
        }
    });
    pushWindow(SDL_WINDOWEVENT_MINIMIZED);
    events.dispatch();
    assert(extraCalls == 0); // subscribed during this batch: starts with the next one
    events.dispatch();
    assert(extraCalls == 1 && normal.back().type == AppEventType::WindowRestored);
    events.unsubscribe(reentrant);
    events.unsubscribe(extraId);

    // Unsubscribed handlers and an uninstalled hub receive nothing.
    events.unsubscribe(normalId);
    events.unsubscribe(earlyId);
    normal.clear();
    early.clear();
    push(SDL_APP_LOWMEMORY);
    events.dispatch();
    assert(normal.empty() && early.empty());
    const int late = events.subscribe([&](const AppEvent &e) { normal.push_back({e.type, false, 0, 0}); });
    events.uninstall();
    push(SDL_APP_LOWMEMORY);
    events.dispatch();
    assert(normal.empty());
    events.unsubscribe(late);
    SDL_Quit();
    std::puts("app events ok");
}
