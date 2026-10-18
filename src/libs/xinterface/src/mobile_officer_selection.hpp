#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace storm::mobile
{
// Bind each published choice to its whole list and profile, not a reusable slot.
struct OfficerListIdentity
{
    uint64_t revision = 0;
    std::string profile;
    std::vector<std::string> rows;
    bool operator==(const OfficerListIdentity &) const = default;
};
class OfficerSelection
{
    OfficerListIdentity published_;
    uint64_t serial_ = 0;
public:
    void publish(const OfficerListIdentity &state)
    {
        if (!(state == published_)) { published_ = state; ++serial_; }
    }
    std::string rowId(int index) const { return std::to_string(serial_) + ":" + std::to_string(index); }
    bool accepts(const OfficerListIdentity &state, int index, const std::string &action) const
    {
        return !state.profile.empty() && state.revision != 0 && state == published_ && index >= 0 &&
            static_cast<size_t>(index) < state.rows.size() &&
            action == "select@" + std::to_string(state.revision) + ":" + rowId(index);
    }
};
}
