// One grass map (.grs) against the grass vertices (src/libs/location/src/grass_pack.hpp, Grass::UpdateWindow): the
// file is read as Grass::LoadData reads it (a file it refuses has no grass in the game), every blade is packed in the
// window that puts its block farthest from the window's corner and decoded as vs_port_grass.sc decodes it: the
// position must come back exactly as LoadData computed it, the height within half a step, the data, the LOD and the
// corner unchanged; the window of the default visible distance must fit the 15-bit places. Used by
// tools/game_checks/grass_pack (every map of a game) and tests/grass_pack (synthetic maps).
#pragma once

#include "grass_pack.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace grass_maps
{

namespace fs = std::filesystem;
using namespace grass_pack;


#pragma pack(push, 1)
struct Header // grs.h GRSHeader
{
    uint32_t id, ver;
    int32_t miniX, miniZ;
    float startX, startZ;
    int32_t numElements;
};
struct MiniMapElement // grs.h GRSMiniMapElement
{
    int32_t start, num[4];
    float minHeight, maxHeight;
};
struct MapElement // grs.h GRSMapElement
{
    unsigned char x, z;
    unsigned char w : 4, h : 4;
    unsigned char frame : 4;
    unsigned char ang : 4;
    float y;
};
#pragma pack(pop)

inline constexpr uint32_t kGrassId = 0x73737247, kGrassVer = 0x30302E31; // grs.h GRASS_ID 'ssrG', GRASS_VER '00.1'

// Grass::UpdateWindow's reach, ceil(maxDist / (scale * GRASS_BLK_DST)) + 2, at its widest in the game: the island
// jungle seen from the sea (sea.c: 1000 m at the default jungle scale 10). A location's grass (50 m, scale 1) is 52.
inline constexpr int32_t kReach = 1000 / 10 + 2;

inline int failures = 0;

inline void fail(const fs::path &file, const char *what, long a = 0, long b = 0)
{
    if (failures++ < 40)
        std::printf("FAIL %s: %s (%ld, %ld)\n", file.string().c_str(), what, a, b);
}

struct Result
{
    long blades = 0, peak = 0;
};

inline bool check_map(const fs::path &file, Result &result)
{
    std::ifstream in(file, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Header hdr{};
    if (bytes.size() < sizeof(hdr))
        return false;
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    const size_t mini = static_cast<size_t>(hdr.miniX) * hdr.miniZ;
    // LoadData's own checks: a file it refuses has no grass in the game
    if (hdr.id != kGrassId || hdr.ver != kGrassVer || hdr.miniX <= 0 || hdr.miniZ <= 0 ||
        bytes.size() != sizeof(hdr) + mini * sizeof(MiniMapElement) + hdr.numElements * sizeof(MapElement))
        return false;
    std::vector<MiniMapElement> map(mini);
    std::memcpy(map.data(), bytes.data() + sizeof(hdr), mini * sizeof(MiniMapElement));
    // LoadData's last check (a block starts where the previous one ended)
    for (int64_t i = 0, blade = 0; i < static_cast<int64_t>(mini); blade += map[i].num[0], ++i)
        if (map[i].start != blade || map[i].num[0] < 0)
            return false;
    const auto *src = reinterpret_cast<const MapElement *>(bytes.data() + sizeof(hdr) + mini * sizeof(MiniMapElement));
    result.blades = hdr.numElements;
    if (hdr.numElements == 0)
        return true;
    // LoadData: x = sx * GRASS_STEP, then += startX + mx * GRASS_BLK_DST
    float low = src[0].y, high = src[0].y;
    for (int32_t i = 0; i < hdr.numElements; ++i)
        low = std::min(low, src[i].y), high = std::max(high, src[i].y);
    const float heightStep = high > low ? (high - low) / kHeightSteps : 1.0f;
    // The window around a camera in the middle of a map of the largest kind must stay within 15-bit places
    const Window widest = around(hdr.miniX / 2, hdr.miniZ / 2, kReach, kWindowMargin, hdr.miniX, hdr.miniZ);
    if (widest.width() > kMaxWindowBlocks || widest.z1 - widest.z0 > kMaxWindowBlocks)
        fail(file, "window wider than 1023 blocks", widest.width(), widest.z1 - widest.z0);
    // Peak blades in a window (prefix sums over the blocks' blade counts)
    std::vector<long> sum((hdr.miniX + 1) * static_cast<size_t>(hdr.miniZ + 1), 0);
    const auto at = [&](int32_t x, int32_t z) -> long & { return sum[static_cast<size_t>(z) * (hdr.miniX + 1) + x]; };
    for (int32_t z = 0; z < hdr.miniZ; ++z)
        for (int32_t x = 0; x < hdr.miniX; ++x)
            at(x + 1, z + 1) = at(x, z + 1) + at(x + 1, z) - at(x, z) + map[z * hdr.miniX + x].num[0];
    for (int32_t z = 0; z < hdr.miniZ; z += 4)
        for (int32_t x = 0; x < hdr.miniX; x += 4)
        {
            const Window w = around(x, z, kReach, kWindowMargin, hdr.miniX, hdr.miniZ);
            result.peak = std::max(result.peak, at(w.x1, w.z1) - at(w.x0, w.z1) - at(w.x1, w.z0) + at(w.x0, w.z0));
        }
    for (int32_t mz = 0; mz < hdr.miniZ; ++mz)
        for (int32_t mx = 0; mx < hdr.miniX; ++mx)
        {
            const MiniMapElement &mm = map[mz * hdr.miniX + mx];
            // The window that puts this block farthest from its corner
            const int32_t span = 2 * (kReach + kWindowMargin);
            const Window window{std::max(0, mx - span), std::max(0, mz - span), mx + 1, mz + 1};
            const float cx = hdr.startX + mx * 1.0f, cz = hdr.startZ + mz * 1.0f;
            for (int32_t i = 0; i < mm.num[0]; ++i)
            {
                const MapElement &sb = src[mm.start + i];
                const float x = sb.x * kStep + cx, z = sb.z * kStep + cz; // GRSMapElementEx after LoadData
                const auto bytes17 = [](unsigned v) { return static_cast<uint8_t>((v * 255) / 15); };
                int32_t maxLod = 0;
                for (int32_t lod = 1; lod < 4; ++lod)
                    if (i < mm.num[lod])
                        maxLod = lod;
                const int32_t sx = step_in_block(x, cx), sz = step_in_block(z, cz);
                if (sx != sb.x || sz != sb.z)
                    fail(file, "step in the block not recovered", sx, sb.x);
                const int16_t data = pack_data(bytes17(sb.frame), bytes17(sb.h), bytes17(sb.w), bytes17(sb.ang));
                const int32_t h = height_step(sb.y, low, heightStep);
                for (int32_t c = 0; c < 4; ++c)
                {
                    const Vertex v = pack(window, mx, mz, sx, sz, h, maxLod, data, c);
                    // vs_port_grass.sc
                    const float ax = v.x, az = v.z;
                    const float cornerX = ax < 0.0f ? 1.0f : 0.0f, cornerZ = az < 0.0f ? 1.0f : 0.0f;
                    const float xq = ax + cornerX * 32768.0f, zq = az + cornerZ * 32768.0f;
                    const float wx = std::floor(xq / 32.0f), wz = std::floor(zq / 32.0f);
                    const float dmx = wx + window.x0, dmz = wz + window.z0;
                    const float dx = (xq - wx * 32.0f) * (1.0f / 32.0f) + (hdr.startX + dmx);
                    const float dz = (zq - wz * 32.0f) * (1.0f / 32.0f) + (hdr.startZ + dmz);
                    const float hq = v.height + 32768.0f, hs = std::floor(hq / 4.0f);
                    const float dy = low + hs * heightStep;
                    const float packed = v.data < 0 ? v.data + 65536.0f : v.data;
                    if (dx != x || dz != z)
                        fail(file, "position not decoded exactly", mx, mz);
                    if (dmx != mx || dmz != mz)
                        fail(file, "block not decoded", mx, mz);
                    if (static_cast<int32_t>(hq - hs * 4.0f) != maxLod)
                        fail(file, "LOD not decoded", maxLod, static_cast<long>(hq - hs * 4.0f));
                    if (std::fabs(dy - sb.y) > heightStep * 0.51f)
                        fail(file, "height off by more than half a step", mx, mz);
                    if (cornerX != (c & 1 ? 1.0f : 0.0f) || cornerZ != (c & 2 ? 1.0f : 0.0f))
                        fail(file, "corner not decoded", c, 0);
                    if (std::fmod(packed, 16.0f) != sb.frame || std::fmod(std::floor(packed / 16.0f), 16.0f) != sb.h ||
                        std::fmod(std::floor(packed / 256.0f), 16.0f) != sb.w || std::floor(packed / 4096.0f) != sb.ang)
                        fail(file, "blade data not decoded", mx, mz);
                }
            }
        }
    return true;
}

inline bool is_grs(const fs::path &path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return ext == ".grs";
}

} // namespace grass_maps
