// Device side of the Effekseer system (storm/render/effekseer.hpp): called by the bgfx frame owner
// (src/shim) and by the effekseer tests, which run bgfx without the shim. Main thread only.
#pragma once

#include <cstdint>

namespace storm::render::effekseer
{

struct DeviceInfo
{
    bool flip_y = false;            // render targets have a bottom-left origin (bgfx caps)
    bool homogeneous_depth = false; // clip-space z is -1..1 (bgfx caps)
};

// After bgfx::init. Nothing is created until the first effect is loaded.
void AttachDevice(const DeviceInfo &info);
// Before bgfx::shutdown: releases the manager, the renderer and every loaded effect.
void DetachDevice();
// Submits every live instance to `view` (already bound to the target the effects belong to).
void DrawToView(uint16_t view, const float view_matrix[16], const float projection[16]);

} // namespace storm::render::effekseer
