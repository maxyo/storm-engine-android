#define SDL_MAIN_HANDLED
#include "sdl_input.hpp"
#ifdef __ANDROID__
#include <platform/android_worldmap.hpp>
#endif
#include <SDL2/SDL.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

static void pushKey(Uint32 type, SDL_Scancode code, bool repeat = false)
{
    SDL_Event e{};
    e.type = type;
    e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = code;
    e.key.repeat = repeat;
    assert(SDL_PushEvent(&e) == 1);
}
#ifdef __ANDROID__
static void pushMouse(Uint32 type, Uint8 button)
{
    SDL_Event e{};
    e.type = type;
    e.button.button = button;
    assert(SDL_PushEvent(&e) == 1);
}
static void pushBoundary(Uint32 type, Uint8 windowEvent = 0)
{
    SDL_Event e{};
    e.type = type;
    e.window.event = windowEvent;
    assert(SDL_PushEvent(&e) == 1);
}
static void checkFrameButtons(storm::SDLInput &input)
{
    // The real adapter receives a press/release between updates, on the UI thread.
    std::thread ui([&] {
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
        pushKey(SDL_KEYUP, SDL_SCANCODE_E);
        pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
        pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT);
    });
    ui.join();
    input.PumpEvents();
    assert(input.KeyboardKeyState('E'));
    assert(input.KeyboardSDLKeyState(SDL_SCANCODE_E));
    assert(input.MouseKeyState(storm::MouseKey::Right));
    // Reads do not consume the edge: every control sharing a key sees one frame.
    assert(input.KeyboardKeyState('E'));
    input.PumpEvents();
    assert(!input.KeyboardKeyState('E'));
    assert(!input.MouseKeyState(storm::MouseKey::Right));

    // Independent held keys/buttons survive unrelated short presses.
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_W);
    pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
    input.PumpEvents();
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
    pushKey(SDL_KEYUP, SDL_SCANCODE_E);
    input.PumpEvents();
    assert(input.KeyboardKeyState('W') && input.KeyboardKeyState('E'));
    assert(input.MouseKeyState(storm::MouseKey::Left));
    input.PumpEvents();
    assert(input.KeyboardKeyState('W') && !input.KeyboardKeyState('E'));
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_W, true);
    pushKey(SDL_KEYUP, SDL_SCANCODE_W);
    pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT);
    input.PumpEvents();
    assert(!input.KeyboardKeyState('W'));
    assert(!input.MouseKeyState(storm::MouseKey::Left));

    // Cancel after releases discards an unconsumed tap, but permits a new press.
    int callbacks = 0;
    const auto subscription = input.Subscribe([&](const storm::InputEvent &) { ++callbacks; });
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
    pushKey(SDL_KEYUP, SDL_SCANCODE_E);
    pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
    pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT);
    storm::SDLInput::CancelPendingInput();
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_A);
    input.PumpEvents();
    assert(!input.KeyboardKeyState('E'));
    assert(!input.MouseKeyState(storm::MouseKey::Right));
    assert(input.KeyboardKeyState('A'));
    assert(callbacks == 1); // Only the fresh A press reaches the consumer.
    input.Unsubscribe(subscription);
    storm::SDLInput::CancelPendingInput();
    input.PumpEvents();
    assert(!input.KeyboardKeyState('A'));
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_A, true); // OS repeat cannot resurrect cancelled hold.
    input.PumpEvents();
    assert(!input.KeyboardKeyState('A'));

    for (bool app : {false, true})
    {
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
        pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
        pushBoundary(app ? SDL_APP_WILLENTERBACKGROUND : SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_LOST);
        input.PumpEvents();
        assert(!input.KeyboardKeyState('E'));
        assert(!input.MouseKeyState(storm::MouseKey::Right));
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_W); // Events while backgrounded are ignored.
        input.PumpEvents();
        assert(!input.KeyboardKeyState('W'));
        pushBoundary(app ? SDL_APP_DIDENTERFOREGROUND : SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_GAINED);
        input.PumpEvents();
        assert(!input.KeyboardKeyState('E') && !input.KeyboardKeyState('W'));
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
        pushKey(SDL_KEYUP, SDL_SCANCODE_E);
        input.PumpEvents();
        assert(input.KeyboardKeyState('E'));
        input.PumpEvents();
        assert(!input.KeyboardKeyState('E'));
    }

    // Window focus does not resume input while the app is still backgrounded.
    pushBoundary(SDL_APP_WILLENTERBACKGROUND);
    pushBoundary(SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_LOST);
    pushBoundary(SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_GAINED);
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
    input.PumpEvents();
    assert(!input.KeyboardKeyState('E'));
    pushBoundary(SDL_APP_DIDENTERFOREGROUND);
    input.PumpEvents();
    assert(!input.KeyboardKeyState('E'));
}
#endif
static void pushMotion(int dx, int dy)
{
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.xrel = dx; e.motion.yrel = dy;
    assert(SDL_PushEvent(&e) == 1);
}
#ifdef __ANDROID__
static void checkCancellationDuringPump(storm::SDLInput &input)
{
    for (int boundary = 0; boundary < 3; ++boundary)
    {
        bool cancelled = false;
        int staleKeyCallbacks = 0;
        const auto subscription = input.Subscribe([&](const storm::InputEvent &event) {
            if (event.type == storm::InputEvent::KeyboardKeyDown &&
                std::get<storm::KeyboardKey>(event.data) == 'E') ++staleKeyCallbacks;
            if (event.type != storm::InputEvent::MouseMove || cancelled) return;
            assert(input.KeyboardKeyState('W'));
            assert(input.MouseKeyState(storm::MouseKey::Left));
            // A real producer cancels while PumpEvents owns a detached batch.
            // Joining establishes that the boundary completed before the reads.
            std::thread producer([&] {
                if (boundary == 0) storm::SDLInput::CancelPendingInput();
                else if (boundary == 1) pushBoundary(SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_LOST);
                else pushBoundary(SDL_APP_WILLENTERBACKGROUND);
                pushKey(SDL_KEYDOWN, SDL_SCANCODE_A); // Belongs to the next batch.
            });
            producer.join();
            cancelled = true;
            assert(!input.KeyboardKeyState('W'));
            assert(!input.KeyboardSDLKeyState(SDL_SCANCODE_W));
            assert(!input.MouseKeyState(storm::MouseKey::Left));
        });
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_W);
        pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
        pushMotion(1, 0);
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
        pushKey(SDL_KEYUP, SDL_SCANCODE_E);
        pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
        pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT);
        input.PumpEvents();
        assert(cancelled && staleKeyCallbacks == 0);
        assert(!input.KeyboardKeyState('W') && !input.KeyboardKeyState('E'));
        assert(!input.KeyboardKeyState('A'));
        assert(!input.MouseKeyState(storm::MouseKey::Left));
        assert(!input.MouseKeyState(storm::MouseKey::Right));
        input.Unsubscribe(subscription);
        input.PumpEvents();
        assert(input.KeyboardKeyState('A') == (boundary == 0));
        if (boundary == 1) pushBoundary(SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_GAINED);
        if (boundary == 2) pushBoundary(SDL_APP_DIDENTERFOREGROUND);
        storm::SDLInput::CancelPendingInput();
        input.PumpEvents();
    }

    // A completed cancellation also invalidates an already published frame,
    // without requiring another PumpEvents call or touching its button arrays.
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
    pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
    input.PumpEvents();
    assert(input.KeyboardKeyState('E') && input.MouseKeyState(storm::MouseKey::Right));
    std::thread producer([] { storm::SDLInput::CancelPendingInput(); });
    producer.join();
    assert(!input.KeyboardKeyState('E'));
    assert(!input.KeyboardSDLKeyState(SDL_SCANCODE_E));
    assert(!input.MouseKeyState(storm::MouseKey::Right));
    input.PumpEvents();
}
#endif

#ifdef __ANDROID__
static void checkMapModalPhysicalInput(storm::SDLInput &input)
{
    const int device=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);
    assert(device==0); // isolated virtual-device fixture, never an external controller
    char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device),guid,sizeof(guid));
    const auto mapping=std::string(guid)+",Modal fixture,a:b0,leftx:a0,platform:Linux,";
    assert(SDL_GameControllerAddMapping(mapping.c_str())>=0);
    SDL_Joystick *joystick=SDL_JoystickOpen(device);assert(joystick);
    SDL_PumpEvents();input.PumpEvents();
    assert(SDL_JoystickSetVirtualAxis(joystick,0,12345)==0);assert(SDL_JoystickSetVirtualButton(joystick,0,1)==0);
    SDL_JoystickUpdate();input.PumpEvents();
    assert(input.ControllerAxisValue(storm::ControllerAxis::LeftX)==12345);
    assert(input.ControllerButtonState(storm::ControllerButton::A));
    auto &modal=storm::android::worldMapHud.modal;
    assert(modal.request({1,1,1,1},17,7,1,SDL_GetTicks()));
    int callbacks=0;const int subscription=input.Subscribe([&](const storm::InputEvent &){++callbacks;});
    pushKey(SDL_KEYDOWN,SDL_SCANCODE_W);pushMouse(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT);pushMotion(17,29);
    assert(SDL_JoystickSetVirtualAxis(joystick,0,23456)==0);SDL_JoystickUpdate();input.PumpEvents();
    assert(SDL_JoystickGetAxis(joystick,0)==23456); // actual SDL hardware state still exists beneath the gate
    assert(!input.KeyboardKeyState('W')&&!input.MouseKeyState(storm::MouseKey::Left));
    assert(input.ControllerAxisValue(storm::ControllerAxis::LeftX)==0&&!input.ControllerButtonState(storm::ControllerButton::A));
    assert(callbacks==0);
    assert(modal.operate(1,1,1,17,1,0,SDL_GetTicks()));input.PumpEvents();assert(callbacks==0); // modal events were not buffered for resume
    assert(!input.KeyboardKeyState('W'));
    input.Unsubscribe(subscription);
    SDL_JoystickClose(joystick);assert(SDL_JoystickDetachVirtual(device)==0);SDL_PumpEvents();input.PumpEvents();
}
#endif
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
#ifdef __ANDROID__
        assert(input.KeyboardKeyState('W')); // Short press remains active for this frame.
#endif
        received.clear();
        input.PumpEvents();
        assert(received.empty()); // A delta must not replay next frame.
        assert(!input.KeyboardKeyState('W')); // No press replay in the next frame.
#ifdef __ANDROID__
        std::thread ui([&] {
            pushMotion(123, 45); // Unconsumed camera motion before focus loss.
            pushKey(SDL_KEYDOWN, SDL_SCANCODE_A);
            pushKey(SDL_KEYUP, SDL_SCANCODE_A);
        });
        ui.join();
        assert(received.empty());
#endif
        assert(input.SetRelativeMouseMode(false));
        assert(SDL_GetRelativeMouseMode() == SDL_FALSE);
#ifdef __ANDROID__
        input.PumpEvents();
        assert(received.empty()); // Focus loss must not turn the cancelled tap into activation.
        assert(!input.KeyboardKeyState('A'));
        received.clear();
        pushMotion(999, -999); // Motion arriving while the app is paused.
        pushKey(SDL_KEYUP, SDL_SCANCODE_LSHIFT);
#endif
        assert(input.SetRelativeMouseMode(true));
        assert(SDL_GetRelativeMouseMode() == SDL_TRUE);
#ifdef __ANDROID__
        input.PumpEvents();
        assert(received.empty());
        received.clear();
#endif
        pushMotion(-6, 4); // New relative touch/physical input after resume.
        input.PumpEvents();
        assert(received.size() == 1);
        delta = std::get<storm::MousePos>(received[0].data);
        assert(delta.x == -6 && delta.y == 4);
        input.Unsubscribe(sub);
#ifdef __ANDROID__
        checkFrameButtons(input);
        checkCancellationDuringPump(input);
        checkMapModalPhysicalInput(input);
#endif
    }
    assert(SDL_GetRelativeMouseMode() == SDL_FALSE); // Service teardown releases capture.
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "PASS: SDL input events, relative mode, focus/release and teardown\n";
    return 0;
}
