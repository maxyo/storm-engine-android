// The grass blades' 8-byte vertices (Grass::UpdateWindow, shaders/ports/vs_port_grass.sc), without engine
// dependencies so tests/grass_pack (synthetic maps) and tools/game_checks/grass_pack (every map of a game) check them.
//
// The buffer holds a window of blocks around the camera's block. A vertex keeps the blade's place in that window in
// 1/32 blocks (15 bits each way, the corner in the sign bits), so a window spans at most 1023 blocks whatever the map
// size; the base height in kHeightSteps steps over the map's height range with the highest LOD that still draws the
// blade; frame, height, width and angle in 4 bits each.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace grass_pack
{

constexpr int32_t kBlockSteps = 32;      // GRASS_BLK_SIZE: places in a block, each way
constexpr float kStep = 1.0f / 32.0f;    // GRASS_STEP with GRASS_BLK_DST 1
constexpr int32_t kHeightSteps = 16383;  // 14 bits; 2 bits for the LOD
constexpr int32_t kWindowMargin = 8;     // blocks the camera moves before the window is rebuilt
constexpr int32_t kMaxWindowBlocks = 32767 / kBlockSteps; // 1023: the places fit 15 bits
constexpr int32_t kMaxReach = (kMaxWindowBlocks - 1) / 2 - kWindowMargin;

#pragma pack(push, 1)
struct Vertex
{
    int16_t x, z;   // place in the window in 1/32 blocks; the corner's offset in the sign bit
    int16_t height; // base height step * 4 + the highest LOD that still draws the blade, less 32768
    int16_t data;   // frame, height, width, angle: 4 bits each
};
#pragma pack(pop)
static_assert(sizeof(Vertex) == 8);

struct Window
{
    int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0; // blocks [x0, x1) x [z0, z1)
    bool empty() const { return x0 >= x1 || z0 >= z1; }
    bool contains(const Window &other) const
    {
        return other.x0 >= x0 && other.x1 <= x1 && other.z0 >= z0 && other.z1 <= z1;
    }
    int32_t width() const { return x1 - x0; }
};

// The blocks within `reach` (+ `margin`) of the camera's block, clipped to the map.
inline Window around(int32_t camx, int32_t camz, int32_t reach, int32_t margin, int32_t miniX, int32_t miniZ)
{
    return {std::max(0, camx - reach - margin), std::max(0, camz - reach - margin),
            std::min(miniX, camx + reach + margin + 1), std::min(miniZ, camz + reach + margin + 1)};
}

// The step in its block of a blade Grass::LoadData placed at (step * kStep + corner).
inline int32_t step_in_block(float position, float corner)
{
    return static_cast<int32_t>(lroundf((position - corner) / kStep));
}

inline int32_t height_step(float y, float low, float step)
{
    return std::clamp(static_cast<int32_t>(lroundf((y - low) / step)), 0, kHeightSteps);
}

// GRSMapElementEx::frame/h/w/ang hold 17 * the 0..15 of the .grs file.
inline int16_t pack_data(uint8_t frame, uint8_t h, uint8_t w, uint8_t ang)
{
    return static_cast<int16_t>(static_cast<uint16_t>(frame / 17 | (h / 17) << 4 | (w / 17) << 8 | (ang / 17) << 12));
}

// Corner c (bit 0: x, bit 1: z) of the blade at step (sx, sz) of block (mx, mz).
inline Vertex pack(const Window &window, int32_t mx, int32_t mz, int32_t sx, int32_t sz, int32_t heightStep,
                   int32_t maxLod, int16_t data, int32_t c)
{
    const int32_t xq = (mx - window.x0) * kBlockSteps + sx, zq = (mz - window.z0) * kBlockSteps + sz;
    return {static_cast<int16_t>(c & 1 ? xq - 32768 : xq), static_cast<int16_t>(c & 2 ? zq - 32768 : zq),
            static_cast<int16_t>(heightStep * 4 + maxLod - 32768), data};
}

} // namespace grass_pack
