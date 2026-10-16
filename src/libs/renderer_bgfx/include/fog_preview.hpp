#pragma once
#include "fog_snapshot.hpp"
#include <cstdint>
#include <string>

namespace storm::render {
struct FogPreviewOptions {
    std::string snapshotPath, shaderDirectory;
    uint32_t width=1280,height=720;
    bool vsync=true, fogEnabled=true;
    uint32_t frameLimit=0; // Development automation only; 0 waits for quit.
};
// Owns its SDL window and bgfx context. Call after SDL video initialization and
// before creating any game renderer. Never shares a window with DXVK.
int runFogPreview(const FogPreviewOptions& options);
}
