// Renderer performance counters of the bgfx shim.
//
// The counters are always on: plain increments on the game thread, totals since start-up.
// A reader keeps the totals of the start of its window and subtracts (Since()), so several
// readers never disturb each other. The engine logs a window every few seconds when engine.ini
// has `render_metrics=1` (Renderer, "RenderMetrics:" line); the scene sweep reports one window
// per captured scene; tests/renderer_bgfx checks the counts of a fixed synthetic frame so that
// draw/state regressions show up in host CI.
#pragma once

#include <cstdint>
#include <string>

namespace storm::render
{

struct PerfCounters
{
    uint64_t frames = 0;           // presented frames
    // Work submitted to bgfx by the shim.
    uint64_t draws = 0;            // bgfx::submit of game draws
    uint64_t skipped_draws = 0;    // draws refused (see the TRAP log for the reason)
    uint64_t views = 0;            // bgfx views opened by the game (render target / viewport / clear passes)
    uint64_t program_changes = 0;  // draws whose program differs from the previous draw's
    uint64_t state_changes = 0;    // draws whose blend/depth/cull/primitive/stencil state differs
    uint64_t uniform_updates = 0;  // bgfx::setUniform calls (values that really changed)
    uint64_t uniform_bytes = 0;
    uint64_t uniform_draws = 0;    // draws with at least one uniform update (the GL backend then
                                   // re-uploads every uniform of the program)
    uint64_t uniform_commit_bytes = 0; // bytes the GL backend re-uploads: all uniforms of the program
                                       // for every draw with a uniform update or a program change
    uint64_t texture_binds = 0;    // bgfx::setTexture calls
    uint64_t texture_changes = 0;  // binds of a different texture or sampler state than the unit had
    uint64_t transient_vb_bytes = 0;
    uint64_t transient_ib_bytes = 0;
    uint64_t fan_draws = 0;        // triangle fans converted to lists
    uint64_t buffer_uploads = 0;   // vertex/index buffers created or updated from their shadow copy
    uint64_t buffer_upload_bytes = 0;
    uint64_t buffer_creates = 0;   // bgfx buffer handles created for them (a rewrite updates in place: none)
    uint64_t texture_uploads = 0;  // texture levels created or updated from the CPU
    uint64_t texture_upload_bytes = 0;
    // Time, milliseconds.
    double shim_ms = 0;     // game thread inside the shim's draw path (D3D9 -> bgfx translation)
    double backend_ms = 0;  // bgfx::frame() of presented frames: the GL backend, driver and swap
    double gpu_ms = 0;      // GPU time of the frames bgfx measured with timer queries (0 when unsupported)
    uint64_t gpu_frames = 0; // frames that had a GPU time
    // Current values (not totals) as of the last presented frame.
    uint64_t texture_memory = 0;       // bytes of textures (bgfx::Stats::textureMemoryUsed)
    uint64_t render_target_memory = 0; // bytes of render targets (bgfx::Stats::rtMemoryUsed)
    // Current CPU/GPU memory held by the shim (bytes, live values).
    uint64_t texture_shadow_bytes = 0; // CPU copies of texture levels (D3D formats)
    uint64_t buffer_shadow_bytes = 0;  // CPU copies of vertex/index buffers
    uint64_t buffer_gpu_bytes = 0;     // bgfx static vertex/index buffer snapshots
    uint64_t transient_vb_frame_max = 0; // largest transient vertex bytes of one frame since start-up
    uint64_t transient_ib_frame_max = 0; // (sizes BGFX_CONFIG_MAX_TRANSIENT_*_BUFFER_SIZE)

    // The totals of this window minus those of `start`; current values are this window's.
    PerfCounters Since(const PerfCounters &start) const;
    // Per-frame means of a window as space-separated key=value pairs (empty without frames):
    // draws skipped views programs states uniforms uniform_kb uniform_draws commit_kb tex_binds tex_changes
    // tvb_kb tib_kb fans buf_uploads buf_kb tex_uploads tex_kb shim_ms backend_ms gpu_ms tex_mb rt_mb
    // tex_shadow_mb buf_shadow_mb buf_gpu_mb tvb_max_kb tib_max_kb.
    std::string Summary() const;
};

// Game thread. Totals since start-up.
const PerfCounters &PerfTotals();

} // namespace storm::render
