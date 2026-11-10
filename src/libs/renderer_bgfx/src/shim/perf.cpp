#include "perf.hpp"

#include <algorithm>
#include <cstdio>

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

void perf_frame_presented(double backend_ms)
{
    ++totals.frames;
    totals.backend_ms += backend_ms;
    totals.transient_vb_frame_max = std::max(totals.transient_vb_frame_max, totals.transient_vb_bytes - frame_start_vb);
    totals.transient_ib_frame_max = std::max(totals.transient_ib_frame_max, totals.transient_ib_bytes - frame_start_ib);
    frame_start_vb = totals.transient_vb_bytes;
    frame_start_ib = totals.transient_ib_bytes;
    const bgfx::Stats *stats = bgfx::getStats();
    if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin)
    {
        totals.gpu_ms += 1000.0 * static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) /
                         static_cast<double>(stats->gpuTimerFreq);
        ++totals.gpu_frames;
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
    d.uniform_commit_bytes -= start.uniform_commit_bytes;
    d.texture_binds -= start.texture_binds;
    d.texture_changes -= start.texture_changes;
    d.transient_vb_bytes -= start.transient_vb_bytes;
    d.transient_ib_bytes -= start.transient_ib_bytes;
    d.fan_draws -= start.fan_draws;
    d.buffer_uploads -= start.buffer_uploads;
    d.buffer_upload_bytes -= start.buffer_upload_bytes;
    d.texture_uploads -= start.texture_uploads;
    d.texture_upload_bytes -= start.texture_upload_bytes;
    d.shim_ms -= start.shim_ms;
    d.backend_ms -= start.backend_ms;
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
    char text[768];
    snprintf(text, sizeof(text),
             "draws=%.1f skipped=%.1f views=%.1f programs=%.1f states=%.1f uniforms=%.1f uniform_kb=%.1f "
             "uniform_draws=%.1f commit_kb=%.1f tex_binds=%.1f tex_changes=%.1f tvb_kb=%.1f tib_kb=%.1f fans=%.1f buf_uploads=%.2f "
             "buf_kb=%.1f tex_uploads=%.2f tex_kb=%.1f shim_ms=%.3f backend_ms=%.3f gpu_ms=%.3f tex_mb=%.1f rt_mb=%.1f "
             "tex_shadow_mb=%.1f buf_shadow_mb=%.1f buf_gpu_mb=%.1f tvb_max_kb=%.0f tib_max_kb=%.0f",
             mean(draws), mean(skipped_draws), mean(views), mean(program_changes), mean(state_changes),
             mean(uniform_updates), kb(uniform_bytes), mean(uniform_draws), kb(uniform_commit_bytes), mean(texture_binds), mean(texture_changes),
             kb(transient_vb_bytes), kb(transient_ib_bytes), mean(fan_draws), mean(buffer_uploads),
             kb(buffer_upload_bytes), mean(texture_uploads), kb(texture_upload_bytes), shim_ms / n, backend_ms / n,
             gpu_frames ? gpu_ms / static_cast<double>(gpu_frames) : 0.0, texture_memory / 1048576.0,
             render_target_memory / 1048576.0, texture_shadow_bytes / 1048576.0, buffer_shadow_bytes / 1048576.0,
             buffer_gpu_bytes / 1048576.0, transient_vb_frame_max / 1024.0, transient_ib_frame_max / 1024.0);
    return text;
}
} // namespace storm::render
