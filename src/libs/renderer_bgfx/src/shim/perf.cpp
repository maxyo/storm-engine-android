#include "perf.hpp"
#include "shim.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace bgfx::gl
{
extern std::atomic<uint64_t> g_stormUniformUploadCalls; // patches/bgfx-gl-uniform-commit.patch
extern std::atomic<uint64_t> g_stormUniformUploadBytes;
} // namespace bgfx::gl

namespace storm::shim
{

namespace
{
render::PerfCounters totals;
uint64_t frame_start_vb = 0, frame_start_ib = 0; // transient totals at the start of the frame
} // namespace

render::PerfCounters &perf()
{
    return totals;
}

void perf_frame_presented(double wait_ms)
{
    ++totals.frames;
    totals.wait_ms += wait_ms;
    totals.gl_uniform_calls = bgfx::gl::g_stormUniformUploadCalls.load(std::memory_order_relaxed);
    totals.gl_uniform_bytes = bgfx::gl::g_stormUniformUploadBytes.load(std::memory_order_relaxed);
    totals.transient_vb_frame_max = std::max(totals.transient_vb_frame_max, totals.transient_vb_bytes - frame_start_vb);
    totals.transient_ib_frame_max = std::max(totals.transient_ib_frame_max, totals.transient_ib_bytes - frame_start_ib);
    frame_start_vb = totals.transient_vb_bytes;
    frame_start_ib = totals.transient_ib_bytes;
    // bgfx's statistics are those of the last frame the backend has finished (with the render thread: an earlier
    // frame than the one just handed over).
    const bgfx::Stats *stats = bgfx::getStats();
    if (stats->cpuTimerFreq > 0 && stats->cpuTimeEnd > stats->cpuTimeBegin)
        totals.submit_ms += 1000.0 * static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) /
                            static_cast<double>(stats->cpuTimerFreq);
    if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin)
    {
        totals.gpu_ms += 1000.0 * static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) /
                         static_cast<double>(stats->gpuTimerFreq);
        ++totals.gpu_frames;
    }
    // The transient buffers' use of every client (shim draws, UI, effects) against the limits (the CMake
    // BGFX_CONFIG_MAX_TRANSIENT_*_BUFFER_SIZE): logged at each new peak 25% above the last one logged.
    static uint64_t logged_vb = 0, logged_ib = 0;
    const uint64_t used_vb = static_cast<uint64_t>(std::max<int32_t>(0, stats->transientVbUsed));
    const uint64_t used_ib = static_cast<uint64_t>(std::max<int32_t>(0, stats->transientIbUsed));
    if (used_vb > logged_vb + logged_vb / 4 || used_ib > logged_ib + logged_ib / 4)
    {
        logged_vb = std::max(logged_vb, used_vb);
        logged_ib = std::max(logged_ib, used_ib);
        const bgfx::Caps *caps = bgfx::getCaps();
        logf("transient buffers: frame peak vertices %.0f KB of %.0f KB, indices %.0f KB of %.0f KB", used_vb / 1024.0,
             caps->limits.maxTransientVbSize / 1024.0, used_ib / 1024.0, caps->limits.maxTransientIbSize / 1024.0);
    }
    totals.texture_memory = static_cast<uint64_t>(std::max<int64_t>(0, stats->textureMemoryUsed));
    totals.render_target_memory = static_cast<uint64_t>(std::max<int64_t>(0, stats->rtMemoryUsed));
}

} // namespace storm::shim

namespace storm::render
{
const PerfCounters &PerfTotals()
{
    return shim::totals;
}

PerfCounters PerfCounters::Since(const PerfCounters &start) const
{
    PerfCounters d = *this;
    d.frames -= start.frames;
    d.draws -= start.draws;
    d.skipped_draws -= start.skipped_draws;
    d.views -= start.views;
    d.program_changes -= start.program_changes;
    d.state_changes -= start.state_changes;
    d.uniform_updates -= start.uniform_updates;
    d.uniform_bytes -= start.uniform_bytes;
    d.uniform_draws -= start.uniform_draws;
    d.gl_uniform_calls -= start.gl_uniform_calls;
    d.gl_uniform_bytes -= start.gl_uniform_bytes;
    d.texture_binds -= start.texture_binds;
    d.texture_changes -= start.texture_changes;
    d.transient_vb_bytes -= start.transient_vb_bytes;
    d.transient_ib_bytes -= start.transient_ib_bytes;
    d.fan_draws -= start.fan_draws;
    d.buffer_uploads -= start.buffer_uploads;
    d.buffer_upload_bytes -= start.buffer_upload_bytes;
    d.buffer_creates -= start.buffer_creates;
    d.texture_uploads -= start.texture_uploads;
    d.texture_upload_bytes -= start.texture_upload_bytes;
    d.shim_ms -= start.shim_ms;
    d.submit_ms -= start.submit_ms;
    d.wait_ms -= start.wait_ms;
    d.gpu_ms -= start.gpu_ms;
    d.gpu_frames -= start.gpu_frames;
    return d;
}

std::string PerfCounters::Summary() const
{
    if (frames == 0)
        return {};
    const double n = static_cast<double>(frames);
    const auto mean = [n](uint64_t value) { return static_cast<double>(value) / n; };
    const auto kb = [n](uint64_t bytes) { return static_cast<double>(bytes) / 1024.0 / n; };
    char text[1024];
    snprintf(text, sizeof(text),
             "draws=%.1f skipped=%.1f views=%.1f programs=%.1f states=%.1f uniforms=%.1f uniform_kb=%.1f "
             "uniform_draws=%.1f gl_uniforms=%.1f gl_uniform_kb=%.1f tex_binds=%.1f tex_changes=%.1f tvb_kb=%.1f tib_kb=%.1f fans=%.1f buf_uploads=%.2f "
             "buf_kb=%.1f buf_creates=%.2f tex_uploads=%.2f tex_kb=%.1f shim_ms=%.3f submit_ms=%.3f wait_ms=%.3f gpu_ms=%.3f tex_mb=%.1f rt_mb=%.1f "
             "tex_shadow_mb=%.1f buf_shadow_mb=%.1f buf_gpu_mb=%.1f tvb_max_kb=%.0f tib_max_kb=%.0f",
             mean(draws), mean(skipped_draws), mean(views), mean(program_changes), mean(state_changes),
             mean(uniform_updates), kb(uniform_bytes), mean(uniform_draws), mean(gl_uniform_calls), kb(gl_uniform_bytes), mean(texture_binds), mean(texture_changes),
             kb(transient_vb_bytes), kb(transient_ib_bytes), mean(fan_draws), mean(buffer_uploads),
             kb(buffer_upload_bytes), mean(buffer_creates), mean(texture_uploads), kb(texture_upload_bytes), shim_ms / n, submit_ms / n, wait_ms / n,
             gpu_frames ? gpu_ms / static_cast<double>(gpu_frames) : 0.0, texture_memory / 1048576.0,
             render_target_memory / 1048576.0, texture_shadow_bytes / 1048576.0, buffer_shadow_bytes / 1048576.0,
             buffer_gpu_bytes / 1048576.0, transient_vb_frame_max / 1024.0, transient_ib_frame_max / 1024.0);
    return text;
}
} // namespace storm::render
