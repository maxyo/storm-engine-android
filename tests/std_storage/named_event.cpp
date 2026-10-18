#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "s_eventmsg.h"

int main()
{
    // Exercise the constructor used by CoreImpl::PostEvent and FUNC_POSTEVENT,
    // including the real MESSAGE payload's destruction.
    auto message = std::make_unique<MESSAGE>();
    message->Reset("ls", int32_t{17}, std::string(2048, 'x'));
    const auto *payload = message.get();
    {
        S_EVENTMSG event("named_event_ownership", message.release(), 50);
        if (std::strcmp(event.pEventName, "named_event_ownership") != 0 || event.pMessageClass != payload)
            throw std::runtime_error("event name/payload ownership");
        if (event.ProcessTime(49) || !event.ProcessTime(1))
            throw std::runtime_error("event timer");
        event.Invalidate();
        if (!event.bInvalide)
            throw std::runtime_error("event invalidation");
    }
    S_EVENTMSG empty;
    std::cout << "PASS: named S_EVENTMSG allocation, payload ownership and destruction\n";
}
