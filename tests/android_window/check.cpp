#define SDL_MAIN_HANDLED
#include "sdl_window.hpp"
#include <algorithm>
#include <cassert>
#include <thread>
#include <vector>

int main()
{
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    {
        storm::SDLWindow window(32, 32, 0, false, false);
        assert(window.SDLHandle());
        const auto mainThread = std::this_thread::get_id();
        const auto id = SDL_GetWindowID(window.SDLHandle());
        std::vector<storm::OSWindow::Event> received;
        auto push = [id](Uint8 kind, Uint32 windowId = 0) {
            SDL_Event event{};
            event.type = SDL_WINDOWEVENT;
            event.window.windowID = windowId ? windowId : id;
            event.window.event = kind;
            assert(SDL_PushEvent(&event) == 1);
        };
        window.Subscribe([&](const auto &event) {
            assert(std::this_thread::get_id() == mainThread);
            received.push_back(event);
        });
        window.PumpEvents();
        received.clear();
        // Reproduce the production race: SDL/Java posts after window's pump,
        // before main's global flush. The next pump must still restore focus.
        std::thread producer([&] { push(SDL_WINDOWEVENT_FOCUS_GAINED); });
        producer.join();
        assert(received.empty()); // no engine callbacks on producer thread
        SDL_FlushEvents(0, SDL_LASTEVENT);
        window.PumpEvents();
        assert((received == std::vector{storm::OSWindow::FocusGained}));
        window.PumpEvents();
        assert(received.size() == 1); // exactly once
        received.clear();
        push(SDL_WINDOWEVENT_FOCUS_LOST, id + 1);
        push(SDL_WINDOWEVENT_SIZE_CHANGED);
        SDL_FlushEvents(0, SDL_LASTEVENT);
        window.PumpEvents();
        assert(received.empty());
        // Preserve ordinary loss/gain order, including events queued in a callback.
        bool enqueueFromCallback = true;
        const auto subscription = window.Subscribe([&](const auto &event) {
            if (enqueueFromCallback && event == storm::OSWindow::FocusLost)
            {
                enqueueFromCallback = false;
                std::thread duringDispatch([&] { push(SDL_WINDOWEVENT_FOCUS_GAINED); });
                duringDispatch.join(); // deadlocks if PumpEvents holds producer mutex
            }
        });
        push(SDL_WINDOWEVENT_FOCUS_LOST);
        window.PumpEvents();
        assert((received == std::vector{storm::OSWindow::FocusLost}));
        SDL_FlushEvents(0, SDL_LASTEVENT);
        window.PumpEvents();
        assert((received == std::vector{storm::OSWindow::FocusLost, storm::OSWindow::FocusGained}));
        window.Unsubscribe(subscription);
        received.clear();
        // Overflow keeps close and final focus without unbounded allocations.
        std::thread flood([&] {
            push(SDL_WINDOWEVENT_CLOSE);
            for (int i = 0; i < 2000; ++i)
            {
                push(i % 2 ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST);
                push(SDL_WINDOWEVENT_CLOSE);
            }
        });
        flood.join();
        SDL_FlushEvents(0, SDL_LASTEVENT);
        window.PumpEvents();
        assert(received.size() <= 32);
        assert(std::count(received.begin(), received.end(), storm::OSWindow::Closed) == 1);
        assert(received.back() == storm::OSWindow::FocusGained);
        received.clear();
        window.PumpEvents();
        assert(received.empty());
    }
    // Destructor unregisters the watch before its mailbox is destroyed.
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    SDL_PushEvent(&event);
    SDL_Quit();
}
