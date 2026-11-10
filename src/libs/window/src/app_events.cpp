#include "app_events.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace storm
{
namespace
{
constexpr size_t kQueueCapacity = 64;

bool isLifecycle(AppEventType type)
{
    switch (type)
    {
    case AppEventType::WillEnterBackground:
    case AppEventType::DidEnterBackground:
    case AppEventType::WillEnterForeground:
    case AppEventType::DidEnterForeground:
    case AppEventType::LowMemory:
    case AppEventType::Terminating:
        return true;
    default:
        return false;
    }
}
} // namespace

const char *toString(AppEventType type)
{
    switch (type)
    {
    case AppEventType::WindowResized: return "WindowResized";
    case AppEventType::WindowFocusGained: return "WindowFocusGained";
    case AppEventType::WindowFocusLost: return "WindowFocusLost";
    case AppEventType::WindowMinimized: return "WindowMinimized";
    case AppEventType::WindowRestored: return "WindowRestored";
    case AppEventType::DisplayOrientationChanged: return "DisplayOrientationChanged";
    case AppEventType::WillEnterBackground: return "WillEnterBackground";
    case AppEventType::DidEnterBackground: return "DidEnterBackground";
    case AppEventType::WillEnterForeground: return "WillEnterForeground";
    case AppEventType::DidEnterForeground: return "DidEnterForeground";
    case AppEventType::LowMemory: return "LowMemory";
    case AppEventType::Terminating: return "Terminating";
    }
    return "Unknown";
}

struct AppEvents::State
{
    struct Subscriber
    {
        Handler handler;
        bool early;
    };
    struct Queued
    {
        AppEvent event;
        bool earlyDelivered;
    };

    std::mutex mutex; // guards everything below; never held while a handler runs
    std::map<int, Subscriber> subscribers;
    std::vector<Queued> queue;
    std::thread::id gameThread;
    int nextId = 1;
    bool installed = false;

    std::vector<Handler> handlers(bool early)
    {
        std::vector<Handler> result;
        std::lock_guard lock(mutex);
        for (const auto &[id, subscriber] : subscribers)
            if (subscriber.early == early)
                result.push_back(subscriber.handler);
        return result;
    }

    static int SDLCALL watch(void *userdata, SDL_Event *source)
    {
        AppEvent event;
        if (source && translate(*source, event))
            static_cast<AppEvents *>(userdata)->post(event);
        return 0;
    }
};

AppEvents::AppEvents() : state_(new State)
{
}

AppEvents::~AppEvents()
{
    // Process exit: SDL may already be gone, so the watch is not removed here.
    delete state_;
}

AppEvents &AppEvents::instance()
{
    static AppEvents events;
    return events;
}

void AppEvents::install()
{
    {
        std::lock_guard lock(state_->mutex);
        state_->gameThread = std::this_thread::get_id();
        if (state_->installed)
            return;
        state_->installed = true;
    }
    SDL_AddEventWatch(&State::watch, this);
}

void AppEvents::uninstall()
{
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->installed)
            return;
        state_->installed = false;
        state_->queue.clear();
    }
    SDL_DelEventWatch(&State::watch, this);
}

int AppEvents::subscribe(Handler handler, bool early)
{
    std::lock_guard lock(state_->mutex);
    const int id = state_->nextId++;
    state_->subscribers[id] = {std::move(handler), early};
    return id;
}

void AppEvents::unsubscribe(int id)
{
    std::lock_guard lock(state_->mutex);
    state_->subscribers.erase(id);
}

void AppEvents::post(const AppEvent &source)
{
    AppEvent event = source;
    event.immediate = false;
    bool onGameThread;
    {
        std::lock_guard lock(state_->mutex);
        onGameThread = state_->gameThread == std::this_thread::get_id();
    }

    // Early subscribers first, without the lock, so they may post or (un)subscribe.
    if (onGameThread)
    {
        AppEvent early = event;
        early.immediate = true;
        for (const auto &handler : state_->handlers(true))
            handler(early);
    }

    std::lock_guard lock(state_->mutex);
    auto &queue = state_->queue;
    if (event.type == AppEventType::WindowResized)
    {
        // Only the final size matters; a rotation produces a burst.
        queue.erase(std::remove_if(queue.begin(), queue.end(),
                                   [&](const State::Queued &queued) {
                                       return queued.event.type == AppEventType::WindowResized &&
                                              queued.event.windowId == event.windowId;
                                   }),
                    queue.end());
    }
    else if (event.type == AppEventType::LowMemory &&
             std::any_of(queue.begin(), queue.end(), [](const State::Queued &queued) {
                 return queued.event.type == AppEventType::LowMemory && !queued.earlyDelivered;
             }))
    {
        return;
    }
    if (queue.size() >= kQueueCapacity)
    {
        const auto victim = std::find_if(queue.begin(), queue.end(), [](const State::Queued &queued) {
            return !isLifecycle(queued.event.type);
        });
        if (victim != queue.end())
            queue.erase(victim);
        else if (!isLifecycle(event.type))
            return; // full of lifecycle events: keep them, drop the newcomer
    }
    queue.push_back({event, onGameThread});
}

void AppEvents::dispatch()
{
    std::vector<State::Queued> batch;
    {
        std::lock_guard lock(state_->mutex);
        batch.swap(state_->queue);
    }
    if (batch.empty())
        return;
    const auto early = state_->handlers(true);
    const auto normal = state_->handlers(false);
    for (const auto &queued : batch)
    {
        if (!queued.earlyDelivered)
            for (const auto &handler : early)
                handler(queued.event);
        for (const auto &handler : normal)
            handler(queued.event);
    }
}

bool AppEvents::translate(const SDL_Event &source, AppEvent &event)
{
    event = {};
    switch (source.type)
    {
    case SDL_WINDOWEVENT:
        event.windowId = source.window.windowID;
        switch (source.window.event)
        {
        case SDL_WINDOWEVENT_SIZE_CHANGED: // also follows RESIZED; covers API- and user-initiated changes
            event.type = AppEventType::WindowResized;
            event.width = source.window.data1;
            event.height = source.window.data2;
            return true;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            event.type = AppEventType::WindowFocusGained;
            return true;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            event.type = AppEventType::WindowFocusLost;
            return true;
        case SDL_WINDOWEVENT_MINIMIZED:
            event.type = AppEventType::WindowMinimized;
            return true;
        case SDL_WINDOWEVENT_RESTORED:
            event.type = AppEventType::WindowRestored;
            return true;
        default:
            return false;
        }
    case SDL_DISPLAYEVENT:
        if (source.display.event != SDL_DISPLAYEVENT_ORIENTATION)
            return false;
        event.type = AppEventType::DisplayOrientationChanged;
        event.display = static_cast<int>(source.display.display);
        event.orientation = source.display.data1;
        return true;
    case SDL_APP_WILLENTERBACKGROUND:
        event.type = AppEventType::WillEnterBackground;
        return true;
    case SDL_APP_DIDENTERBACKGROUND:
        event.type = AppEventType::DidEnterBackground;
        return true;
    case SDL_APP_WILLENTERFOREGROUND:
        event.type = AppEventType::WillEnterForeground;
        return true;
    case SDL_APP_DIDENTERFOREGROUND:
        event.type = AppEventType::DidEnterForeground;
        return true;
    case SDL_APP_LOWMEMORY:
        event.type = AppEventType::LowMemory;
        return true;
    case SDL_APP_TERMINATING:
        event.type = AppEventType::Terminating;
        return true;
    default:
        return false;
    }
}
} // namespace storm
