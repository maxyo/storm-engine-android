#define SDL_MAIN_HANDLED
#include "sdl_input.hpp"
#include <SDL2/SDL.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

static void pushKey(Uint32 type, SDL_Scancode code)
{
    SDL_Event e{};
    e.type = type;
    e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = code;
    assert(SDL_PushEvent(&e) == 1);
}
static void pushMotion(int dx, int dy)
{
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.xrel = dx; e.motion.yrel = dy;
    assert(SDL_PushEvent(&e) == 1);
}
int main()
{
    SDL_SetMainReady();
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    SDL_Window *window = SDL_CreateWindow("input adapter integration", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN);
    assert(window);
    SDL_RaiseWindow(window);
    SDL_PumpEvents();
    {
        storm::SDLInput input;
        assert(input.SetRelativeMouseMode(true));
        assert(SDL_GetRelativeMouseMode() == SDL_TRUE);
        std::vector<storm::InputEvent> received;
        const auto consumer = std::this_thread::get_id();
        int sub = input.Subscribe([&](const storm::InputEvent &e) {
            assert(std::this_thread::get_id() == consumer);
            received.push_back(e);
        });
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_W);
        pushMotion(18, -9);
        pushKey(SDL_KEYUP, SDL_SCANCODE_W);
#ifdef __ANDROID__
        // Compile the actual Android deferral branch against real host SDL.
        // This checks thread handoff semantics, not Android/JNI runtime itself.
        assert(received.empty());
#endif
        input.PumpEvents();
        assert(received.size() == 3);
        assert(received[0].type == storm::InputEvent::KeyboardKeyDown);
        assert(std::get<storm::KeyboardKey>(received[0].data) == 'W');
        assert(received[1].type == storm::InputEvent::MouseMove);
        auto delta = std::get<storm::MousePos>(received[1].data);
        assert(delta.x == 18 && delta.y == -9);
        assert(received[2].type == storm::InputEvent::KeyboardKeyUp);
        assert(std::get<storm::KeyboardKey>(received[2].data) == 'W');
        received.clear();
        input.PumpEvents();
        assert(received.empty()); // A delta must not replay next frame.
        assert(!input.KeyboardKeyState('W')); // PushEvent does not fake physical state.
#ifdef __ANDROID__
        std::thread ui([&] {
            pushMotion(123, 45); // Unconsumed camera motion before focus loss.
            pushKey(SDL_KEYDOWN, SDL_SCANCODE_A);
            pushKey(SDL_KEYUP, SDL_SCANCODE_A); // onPause's release must survive.
        });
        ui.join();
        assert(received.empty());
#endif
        assert(input.SetRelativeMouseMode(false));
        assert(SDL_GetRelativeMouseMode() == SDL_FALSE);
#ifdef __ANDROID__
        input.PumpEvents();
        assert(received.size() == 2);
        assert(received[0].type == storm::InputEvent::KeyboardKeyDown);
        assert(received[1].type == storm::InputEvent::KeyboardKeyUp);
        received.clear();
        pushMotion(999, -999); // Motion arriving while the app is paused.
        pushKey(SDL_KEYUP, SDL_SCANCODE_LSHIFT);
#endif
        assert(input.SetRelativeMouseMode(true));
        assert(SDL_GetRelativeMouseMode() == SDL_TRUE);
#ifdef __ANDROID__
        input.PumpEvents();
        assert(received.size() == 1 && received[0].type == storm::InputEvent::KeyboardKeyUp);
        received.clear();
#endif
        pushMotion(-6, 4); // New relative touch/physical input after resume.
        input.PumpEvents();
        assert(received.size() == 1);
        delta = std::get<storm::MousePos>(received[0].data);
        assert(delta.x == -6 && delta.y == 4);
        input.Unsubscribe(sub);
    }
    assert(SDL_GetRelativeMouseMode() == SDL_FALSE); // Service teardown releases capture.
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "PASS: SDL input events, relative mode, focus/release and teardown\n";
    return 0;
}
