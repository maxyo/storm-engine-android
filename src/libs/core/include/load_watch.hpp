#pragma once
// Watchdog of a load the player asked for (an explicit, logged guard, not a fallback).
//
// The game's load runs in steps that can each stop without a word: save_load.c ProcessLoad
// closes the interface and posts evntLoad; seadogs.c LoadGame returns at once when the
// record has no description (MSG_INTERFACE_GET_SAVE_DATA gives ""); LoadEngineState is
// refused by CoreImpl::InitiateStateLoading for a damaged record. Each of those left the
// player in front of the bare menu scene or an unchanged game, with no message.
//
// Whoever accepts the player's load (xinterface: the confirmed load of a save list record)
// calls Requested(); CoreImpl::ProcessStateLoading calls Loaded() once the record's state is
// applied. Every known refusal (the description guard, InitiateStateLoading,
// ProcessStateLoading) calls Refused() with its reason. The core frame loop calls Poll() with
// the frame's real time: it returns the failure once — at the first refusal, or when the load
// has not started within kTimeoutSeconds of running frames (time the app spends suspended
// does not count). The caller then logs it, brings the menu back and tells the player.
#include <optional>
#include <string>

namespace storm
{
class LoadWatch
{
  public:
    // Measured from the confirmed load to InitiateStateLoading: ~2-3 s on the arm64-on-x86
    // emulator at 1-2 menu frames per second, well under a second on a phone; the state is
    // then applied inside one frame (counted as kMaxFrameSeconds at most). The deadline is
    // generous so that only a load that will never start trips it.
    static constexpr double kTimeoutSeconds = 20.0;
    // One frame never counts for more than this: a frame that spans a suspension of the app
    // must not trip the deadline.
    static constexpr double kMaxFrameSeconds = 1.0;

    struct Failure
    {
        std::string record; // the game's path of the record ("SAVE\<profile>\<file>")
        std::string reason;
    };

    void Requested(std::string record)
    {
        record_ = std::move(record);
        armed_ = true;
        waited_ = 0.0;
        refusal_.reset();
    }
    // The core applied a record's state: the watched load (or any load) is done.
    void Loaded()
    {
        armed_ = false;
        refusal_.reset();
    }
    // A step of the load refused the record. Only a watched load fails through it: a refusal
    // the player did not ask for (a save list reading descriptions) is not a failed load.
    void Refused(const std::string &record, std::string reason)
    {
        if (armed_ && !refusal_ && Same(record, record_))
            refusal_ = std::move(reason);
    }
    std::optional<Failure> Poll(double frameSeconds)
    {
        if (!armed_)
            return std::nullopt;
        waited_ += frameSeconds < 0.0 ? 0.0 : frameSeconds > kMaxFrameSeconds ? kMaxFrameSeconds : frameSeconds;
        if (!refusal_ && waited_ < kTimeoutSeconds)
            return std::nullopt;
        armed_ = false;
        Failure failure{record_, refusal_ ? *refusal_ : "the load did not start within " + std::to_string(static_cast<int>(kTimeoutSeconds)) + " s"};
        refusal_.reset();
        return failure;
    }
    bool Armed() const
    {
        return armed_;
    }
    double Waited() const
    {
        return waited_;
    }

    // The scripts write the same record as "SAVE\p\f" or "SAVE/p/f", in any letter case of
    // the save root.
    static bool Same(const std::string &a, const std::string &b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            char x = a[i] == '/' ? '\\' : a[i], y = b[i] == '/' ? '\\' : b[i];
            if (x >= 'A' && x <= 'Z')
                x = static_cast<char>(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z')
                y = static_cast<char>(y - 'A' + 'a');
            if (x != y)
                return false;
        }
        return true;
    }

  private:
    std::string record_;
    bool armed_ = false;
    double waited_ = 0.0;
    std::optional<std::string> refusal_;
};

// The one watch of the game thread.
inline LoadWatch &loadWatch()
{
    static LoadWatch watch;
    return watch;
}
} // namespace storm
