#pragma once
#include "android_combat_input.hpp"
#include <chrono>
#include <cmath>

namespace storm::android
{
inline uint32_t movementTick()
{
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct MovementPermission
{
    bool forward = false, backward = false, left = false, right = false, run = false;
};
struct MovementAxes
{
    float x = 0, y = 0, magnitude = 0;
    bool runAllowed = false;
};
inline MovementAxes movementAxes(int x, int y, const MovementPermission &permission, bool wasMoving = false)
{
    if ((x < 0 && !permission.left) || (x > 0 && !permission.right)) x = 0;
    if ((y < 0 && !permission.backward) || (y > 0 && !permission.forward)) y = 0;
    const float length = std::hypot(float(x), float(y));
    // Radial hysteresis prevents a resting thumb from repeatedly starting and
    // stopping. Normalize direction only; radius selects a gait, never speed.
    if (length <= (wasMoving ? 120.0f : 180.0f)) return {0, 0, 0, permission.run};
    return {x / length, y / length, std::min(length, 1000.0f) / 1000.0f, permission.run};
}
inline bool movementOwnerCurrent(std::string_view owner, const GameplaySnapshot &current)
{
    return !owner.empty() && current.inputOwner == owner &&
        (current.mode == "explore" || current.mode == "melee");
}
inline bool ordinaryMovementAllowed(bool requested, bool moving, bool locked, bool priority,
                                    bool jumping, bool swimming, bool fighting, bool queuedFight, bool locomotion)
{
    return requested && moving && !locked && !priority && !jumping && !swimming &&
        !fighting && !queuedFight && locomotion;
}
struct MovementPace { bool run; float speed; };
// Two native gait speeds. Enter run at 80% travel and return to walk below
// 65%; permission changes always take effect immediately. Direction stays analog.
inline MovementPace movementPace(float magnitude, float walkSpeed, float runSpeed, bool canRun,
                                  bool wasRunning = false)
{
    if (magnitude <= 0.0f) return {false, 0.0f};
    walkSpeed = std::fabs(walkSpeed); runSpeed = std::max(walkSpeed, std::fabs(runSpeed));
    const bool run = canRun && magnitude >= (wasRunning ? .65f : .80f);
    return {run, run ? runSpeed : walkSpeed};
}
struct MovementDirection { float x, z; };
inline MovementDirection movementDirection(const MovementAxes &axes, float cameraX, float cameraZ,
                                            float fallbackYaw)
{
    const float length = std::hypot(cameraX, cameraZ);
    if (length > .001f) { cameraX /= length; cameraZ /= length; }
    else { cameraX = std::sin(fallbackYaw); cameraZ = std::cos(fallbackYaw); }
    return {cameraZ * axes.x + cameraX * axes.y, -cameraX * axes.x + cameraZ * axes.y};
}
// One latest-value slot, owned by the engine thread. No entity/script pointers
// cross JNI. The atomic cancellation epoch is the existing JNI hard-cancel fence.
class MovementInput
{
    inline static std::atomic<uint64_t> nextGeneration_{0};
    std::string owner_;
    uint64_t generation_ = 0, sequence_ = 0, cancel_ = 0;
    uint32_t tick_ = 0;
    int x_ = 0, y_ = 0;
    bool active_ = false, moving_ = false;
    template<class T> static bool number(std::string_view value, T &out)
    {
        const auto parsed = std::from_chars(value.data(), value.data()+value.size(), out);
        return !value.empty() && parsed.ec == std::errc() && parsed.ptr == value.data()+value.size();
    }
    void synchronizeCancel()
    {
        const auto epoch = combatCancelEpoch.load(std::memory_order_acquire);
        if (epoch != cancel_) { cancel_ = epoch; resetActivity(); }
    }
public:
    // nullopt means a hard context change; individual false bits mean a locked
    // or unmapped real native control and suppress only that requested direction.
    std::function<std::optional<MovementPermission>(std::string_view)> guard;
    void resetActivity()
    {
        generation_ = nextGeneration_.fetch_add(1, std::memory_order_relaxed) + 1;
        sequence_ = 0; active_ = moving_ = false; x_ = y_ = 0;
    }
    void updateOwner(std::string owner)
    {
        synchronizeCancel();
        if (owner != owner_) { owner_ = std::move(owner); resetActivity(); }
    }
    uint64_t generation() { synchronizeCancel(); return generation_; }
    const std::string &owner() const { return owner_; }
    bool offer(std::string_view input, uint32_t tick,
               uint64_t enqueueEpoch = combatCancelEpoch.load(std::memory_order_acquire))
    {
        synchronizeCancel();
        if (active_ && tick - tick_ > 350) resetActivity();
        if (enqueueEpoch != cancel_ || input.size() > 256 || owner_.empty()) return false;
        std::array<std::string_view,7> fields;
        for (size_t i=0; i<fields.size(); ++i)
        {
            const auto tab = input.find('\t');
            if (i+1 == fields.size()) { if (tab != std::string_view::npos) return false; fields[i] = input; }
            else { if (tab == std::string_view::npos) return false; fields[i] = input.substr(0,tab); input.remove_prefix(tab+1); }
        }
        uint64_t generation = 0, sequence = 0;
        int x = 0, y = 0;
        if (fields[0] != "1" || fields[1] != owner_ || !number(fields[2],generation) ||
            generation != generation_ || !generation || !number(fields[3],sequence) || sequence <= sequence_ ||
            (fields[4] != "0" && fields[4] != "1") || !number(fields[5],x) || !number(fields[6],y) ||
            x < -1000 || x > 1000 || y < -1000 || y > 1000 || (fields[4] == "0" && (x || y))) return false;
        if (!guard || !guard(owner_)) { updateOwner({}); return false; }
        sequence_ = sequence; tick_ = tick; x_ = x; y_ = y; active_ = fields[4] == "1";
        if (!active_) moving_ = false;
        return true;
    }
    std::optional<MovementAxes> read(uint32_t tick)
    {
        synchronizeCancel();
        if (!active_) return std::nullopt;
        // Expiry rotates the token: a delayed heartbeat cannot restart movement.
        if (tick - tick_ > 350) { resetActivity(); return std::nullopt; }
        const auto permission = guard ? guard(owner_) : std::nullopt;
        if (!permission) { updateOwner({}); return std::nullopt; }
        const auto axes = movementAxes(x_, y_, *permission, moving_);
        moving_ = axes.magnitude > 0.0f;
        return axes;
    }
};
inline MovementInput movementInput;
} // namespace storm::android
