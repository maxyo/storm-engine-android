#include "sdl_window.hpp"

#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#endif
#include <algorithm>

namespace storm
{
SDLWindow::SDLWindow(int width, int height, int preferred_display, bool fullscreen, bool bordered)
    : fullscreen_(fullscreen)
{
    uint32_t flags = (fullscreen ? SDL_WINDOW_FULLSCREEN : 0) | SDL_WINDOW_HIDDEN;
#if !defined(_WIN32) && !defined(STORM_MESA_NINE) // DXVK-Native
    flags |= SDL_WINDOW_VULKAN;
#endif
    window_ = std::unique_ptr<SDL_Window, std::function<void(SDL_Window *)>>(
        SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED_DISPLAY(preferred_display),
                         SDL_WINDOWPOS_CENTERED_DISPLAY(preferred_display), width, height, flags),
        [](SDL_Window *w) { SDL_DestroyWindow(w); });

    sdlID_ = SDL_GetWindowID(window_.get());
    SDL_SetWindowBordered(window_.get(), bordered ? SDL_TRUE : SDL_FALSE);
    SDL_AddEventWatch(&SDLEventHandler, this);
}

SDLWindow::~SDLWindow()
{
    SDL_DelEventWatch(&SDLEventHandler, this);
}

void SDLWindow::PumpEvents()
{
#ifdef __ANDROID__
    std::array<SDL_WindowEvent, pendingWindowCapacity> events;
    size_t count;
    {
        std::lock_guard lock(pendingWindowMutex_);
        count = pendingWindowCount_;
        std::copy_n(pendingWindowEvents_.begin(), count, events.begin());
        pendingWindowCount_ = 0;
    }
    // Do not hold the producer lock while invoking core/application callbacks.
    // Events arriving now stay in the next batch even if main flushes SDL's queue.
    for (size_t i = 0; i < count; ++i) ProcessEvent(events[i]);
#endif
}

void SDLWindow::Show()
{
    SDL_ShowWindow(window_.get());
}

void SDLWindow::Hide()
{
    SDL_HideWindow(window_.get());
}

void SDLWindow::Focus()
{
    SDL_RaiseWindow(window_.get());
}

int SDLWindow::Width() const
{
    int w, h;
    SDL_GetWindowSize(window_.get(), &w, &h);
    return w;
}

int SDLWindow::Height() const
{
    int w, h;
    SDL_GetWindowSize(window_.get(), &w, &h);
    return h;
}

WindowSize SDLWindow::GetWindowSize() const
{
    int w, h;
    SDL_GetWindowSize(window_.get(), &w, &h);
    return {w, h};
}

bool SDLWindow::Fullscreen() const
{
    return fullscreen_;
}

std::string SDLWindow::Title() const
{
    return std::string(SDL_GetWindowTitle(window_.get()));
}

void SDLWindow::SetFullscreen(bool fullscreen)
{
    fullscreen_ = fullscreen;
    SDL_SetWindowFullscreen(window_.get(), fullscreen_ ? SDL_WINDOW_FULLSCREEN : 0);
}

void SDLWindow::Resize(int width, int height)
{
    SDL_SetWindowSize(window_.get(), width, height);
}

void SDLWindow::WarpMouseInWindow(int x, int y)
{
    SDL_WarpMouseInWindow(window_.get(), x, y);
}

void SDLWindow::SetTitle(const std::string &title)
{
    SDL_SetWindowTitle(window_.get(), title.c_str());
}

void SDLWindow::SetGamma(const uint16_t (&red)[256], const uint16_t (&green)[256], const uint16_t (&blue)[256])
{
    SDL_SetWindowGammaRamp(window_.get(), red, green, blue);
}

int SDLWindow::Subscribe(const EventHandler &handler)
{
    int id = 1;
    if (!handlers_.empty())
        id = (--handlers_.end())->first + 1;
    handlers_[id] = handler;
    return id;
}

void SDLWindow::Unsubscribe(int id)
{
    auto it = handlers_.find(id);
    if (it != handlers_.end())
        handlers_.erase(it);
}

// TODO: X/Wayland/MacOS
void *SDLWindow::OSHandle()
{
    if (!window_)
        return nullptr;

#ifdef _WIN32
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(window_.get(), &info);
    return info.info.win.window;
#else
    // dxvk-native uses HWND as SDL2 window handle, so this is allowed
    return window_.get();
#endif
}

SDL_Window *SDLWindow::SDLHandle() const
{
    return window_.get();
}

void SDLWindow::ProcessEvent(const SDL_WindowEvent &evt) const
{
    Event winEvent;
    switch (evt.event)
    {
    case SDL_WINDOWEVENT_FOCUS_GAINED:
        winEvent = FocusGained;
        break;

    case SDL_WINDOWEVENT_FOCUS_LOST:
        winEvent = FocusLost;
        break;

    case SDL_WINDOWEVENT_CLOSE:
        winEvent = Closed;
        break;

    default:
        return;
    }

    for (auto handler : handlers_)
        handler.second(winEvent);
}

std::shared_ptr<OSWindow> OSWindow::Create(int width, int height, int preferred_display, bool fullscreen, bool bordered)
{
    return std::make_shared<SDLWindow>(width, height, preferred_display, fullscreen, bordered);
}

int SDLWindow::SDLEventHandler(void *userdata, SDL_Event *evt)
{
    auto w = static_cast<SDLWindow *>(userdata);

    if ((evt->type != SDL_WINDOWEVENT) || (evt->window.windowID != w->sdlID_))
        return 0;

#ifdef __ANDROID__
    const auto kind = evt->window.event;
    if (kind != SDL_WINDOWEVENT_FOCUS_GAINED && kind != SDL_WINDOWEVENT_FOCUS_LOST &&
        kind != SDL_WINDOWEVENT_CLOSE) return 0;
    std::lock_guard lock(w->pendingWindowMutex_);
    auto &events = w->pendingWindowEvents_;
    auto &count = w->pendingWindowCount_;
    // Close is latched until delivery; a producer flood cannot evict it.
    if (kind == SDL_WINDOWEVENT_CLOSE && std::any_of(events.begin(), events.begin() + count,
        [](const auto &event) { return event.event == SDL_WINDOWEVENT_CLOSE; })) return 0;
    if (count == pendingWindowCapacity)
    {
        // At most one close is stored, so an old focus event always exists.
        // Keep the latest focus transition and preserve ordering of retained events.
        const auto oldestFocus = std::find_if(events.begin(), events.begin() + count,
            [](const auto &event) { return event.event != SDL_WINDOWEVENT_CLOSE; });
        std::move(oldestFocus + 1, events.begin() + count, oldestFocus);
        --count;
    }
    events[count++] = evt->window;
#else
    w->ProcessEvent(evt->window);
#endif

    return 0;
}
} // namespace storm
