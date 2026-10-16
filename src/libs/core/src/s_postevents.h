#pragma once

#include <memory>
#include <vector>

#include "s_eventmsg.h"

class POSTEVENTS_LIST
{
    std::vector<std::unique_ptr<S_EVENTMSG>> events_;

  public:
    POSTEVENTS_LIST() = default;

    ~POSTEVENTS_LIST()
    {
        Release();
    };

    void Release()
    {
        // Keep the original forward destruction order and release the storage.
        for (auto &event : events_)
            event.reset();
        decltype(events_){}.swap(events_);
    };

    void Add(S_EVENTMSG *pClass)
    {
        events_.push_back(std::unique_ptr<S_EVENTMSG>(pClass));
    };

    void Del(uint32_t _n)
    {
        if (_n >= events_.size())
            return;
        events_.erase(events_.begin() + _n);
    }

    S_EVENTMSG *Read(uint32_t _n)
    {
        if (_n >= events_.size())
            return nullptr;
        return events_[_n].get();
    };

    uint32_t GetClassesNum()
    {
        return static_cast<uint32_t>(events_.size());
    }

    void InvalidateAll()
    {
        for (const auto &event : events_)
            event->Invalidate();
    };

    void RemoveInvalidated()
    {
        for (uint32_t n = 0; n < events_.size(); n++)
        {
            if (events_[n]->bInvalide)
            {
                Del(n);
                n = 0;
            }
        }
    }
};
