// The game's .tx texture files decoded on the CPU (bimg), for a frame owner without the engine's
// texture module: the capture tool's picture source. The game itself takes pictures from the
// renderer (RenderBackend::AcquireGamePicture).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace storm::ui
{

// A decoded game picture: premultiplied RGBA8, top row first.
struct Picture
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Top mip of a .tx file (raw formats and BC1-3); false with a reason for anything else.
bool DecodeTx(const uint8_t *data, size_t size, Picture &out, std::string *error = nullptr);

} // namespace storm::ui
