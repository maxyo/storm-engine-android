#pragma once

#include "storm/render/perf_stats.hpp"

#include <bgfx/bgfx.h>

#include <chrono>

namespace storm::shim
{

// The totals of storm/render/perf_stats.hpp (game thread only).
render::PerfCounters &perf();

// Called after bgfx::frame() of a presented frame: frame count, bgfx statistics (backend time among them).
// `wait_ms`: how long the game thread waited for the render thread for this frame (0 without one).
void perf_frame_presented(double wait_ms);

// Counts a texture upload and passes the memory through.
inline const bgfx::Memory *counted_texture_upload(const bgfx::Memory *memory)
{
    ++perf().texture_uploads;
    perf().texture_upload_bytes += memory->size;
    return memory;
}

// Live memory accounting (perf_stats.hpp *_bytes): `field` += new_size - old_size.
inline void perf_resize(uint64_t render::PerfCounters::*field, size_t old_size, size_t new_size)
{
    perf().*field += new_size;
    perf().*field -= old_size;
}

// Adds the scope's wall time to `sink` (milliseconds).
class PerfScope
{
  public:
    explicit PerfScope(double &sink) : sink_(sink), start_(std::chrono::steady_clock::now()) {}
    ~PerfScope() { sink_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count(); }
    PerfScope(const PerfScope &) = delete;
    PerfScope &operator=(const PerfScope &) = delete;

  private:
    double &sink_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace storm::shim
