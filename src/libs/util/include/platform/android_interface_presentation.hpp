#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace storm::android
{
// Game-thread owned. A versioned Java callback opts in to hiding the legacy
// interface immediately, before its asynchronous mobile replacement is ready.
class InterfacePresentation
{
public:
    void begin(uint64_t revision, bool active, bool negotiated)
    {
        revision_ = revision;
        active_ = active;
        negotiated_ = negotiated;
        nativeVisible_ = false;
    }

    bool negotiated() const { return negotiated_; }
    bool suppressed() const { return active_ && negotiated_ && !nativeVisible_; }

    bool offer(std::string_view action)
    {
        if (!active_ || !negotiated_) return false;
        const auto prefix = std::to_string(revision_) + ":";
        if (action.substr(0, prefix.size()) != prefix) return false;
        const auto mode = action.substr(prefix.size());
        if (mode == "mobile") nativeVisible_ = false;
        else if (mode == "original" || mode == "fallback") nativeVisible_ = true;
        else return false;
        return true;
    }

private:
    uint64_t revision_ = 0;
    bool active_ = false;
    bool negotiated_ = false;
    bool nativeVisible_ = false;
};

// Suppress only GPU submissions made inside XINTERFACE::Realize. Its nodes
// must still run Draw: several update animation, selection and script state.
// The scope never covers the world renderer or the scene postprocess.
inline thread_local bool nativeInterfaceDrawSuppressed = false;
inline bool NativeInterfaceDrawSuppressed() { return nativeInterfaceDrawSuppressed; }

class NativeInterfaceDrawScope
{
public:
    explicit NativeInterfaceDrawScope(bool suppress) : previous_(nativeInterfaceDrawSuppressed)
    {
        nativeInterfaceDrawSuppressed = previous_ || suppress;
    }
    ~NativeInterfaceDrawScope() { nativeInterfaceDrawSuppressed = previous_; }
    NativeInterfaceDrawScope(const NativeInterfaceDrawScope &) = delete;
    NativeInterfaceDrawScope &operator=(const NativeInterfaceDrawScope &) = delete;

private:
    bool previous_;
};
}
