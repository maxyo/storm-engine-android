// SEA's vertex budget (sea.cpp: the block tree, the frustum test, FitBlocksToBuffer, PrepareIndicesForBlock) on a
// stub SEA. For the game's sea set-ups and cameras around the compass every frame must write at most NUM_VERTEXS
// vertices (16-bit indices), index only vertices it wrote and keep the block under the camera; blocks that do not fit
// are the farthest. The original cap (original.inc) is run as the reference and must fail.
#include "math_inlines.h"
#include "plane.h"
#include "storm_glm.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "defines.inc"

// The production code reports through fmt and spdlog; the check counts the reports.
namespace fmt
{
template <typename... Args> std::string format(const char *text, Args &&...)
{
    return text;
}
} // namespace fmt
namespace spdlog
{
inline int warnings = 0;
template <typename... Args> void warn(const char *, Args &&...)
{
    ++warnings;
}
} // namespace spdlog

namespace
{
struct SeaBlockData
{
    int32_t iX1, iX2, iY1, iY2;
    int32_t iSize0;
    int32_t iTX, iTY;
    int32_t iSize;
    int32_t iLOD;
    int32_t iIStart;
    int32_t iIFirst, iILast;
    bool bInProgress, bDone;
};
} // namespace

// The members the code uses, under the names of sea.h.
#define STUB_SEA                                                                                                       \
  public:                                                                                                              \
    using SeaBlock = SeaBlockData;                                                                                     \
    glm::vec3 vCamPos{0.0f}, vSeaCenterPos{0.0f};                                                                      \
    uint32_t dwMaxDim = 65536, dwMinDim = 128;                                                                         \
    float fMaxSeaHeight = 20.0f, fGridStep = 0.07f, fLodScale = 0.5f;                                                  \
    PLANE pFrustumPlanes[4]{};                                                                                         \
    std::vector<SeaBlock> aBlocks;                                                                                     \
    std::vector<uint32_t> indices = std::vector<uint32_t>(NUM_VERTEXS * 3);                                            \
    std::vector<uint16_t> triangles = std::vector<uint16_t>(NUM_INDICES * 3);                                          \
    uint32_t *pIndices = indices.data();                                                                               \
    uint16_t *pTriangles = triangles.data();                                                                           \
    int32_t iVStart = 0, iTStart = 0, iIStart = 0;                                                                     \
    uint32_t dwFramesCut = 0, dwMostBlocksCut = 0;                                                                     \
    uint32_t TreeDim() const;                                                                                          \
    void PlaceTree();                                                                                                  \
    int32_t VisCode(const glm::vec3 &vP);                                                                              \
    bool isVisibleBBox(const glm::vec3 &vCenter, const glm::vec3 &v1, const glm::vec3 &v2);                            \
    float CalcLod(const float &x, const float &y, const float &z);                                                     \
    void CalculateLOD(const glm::vec3 &v1, const glm::vec3 &v2, int32_t &iMaxLOD, int32_t &iMinLOD);                   \
    void AddBlock(int32_t iTX, int32_t iTY, int32_t iSize, int32_t iLOD);                                              \
    void BuildTree(int32_t iTX, int32_t iTY, int32_t iLev);                                                            \
    void FitBlocksToBuffer();                                                                                          \
    void PrepareIndicesForBlock(uint32_t dwBlockIndex);

namespace now
{
class SEA
{
    STUB_SEA
};
#include "tree.inc"
#include "production.inc"
} // namespace now

namespace original
{
class SEA
{
    STUB_SEA
};
#include "tree.inc"
#include "original.inc"
} // namespace original

namespace
{
int failures = 0;

#define CHECK(condition, ...)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::printf("FAILED %s:%d: %s: ", __FILE__, __LINE__, #condition);                                        \
            std::printf(__VA_ARGS__);                                                                                  \
            std::printf("\n");                                                                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false)

struct Setup
{
    const char *name;
    uint32_t maxDim, minDim;
    float gridStep, lodScale, maxSeaHeight;
};

// WhrSea.c of the two script sets: open sea and the sea of a location, at SeaDetails 1.0.
constexpr Setup kSetups[] = {
    {"TEHO open sea", 65536, 128, 0.07f, 0.5f, 200.0f},
    {"TEHO location", 65536, 64, 0.0375f, 1.0f, 1.01f},
    {"CL location", 65536 * 2, 128, 0.051f, 0.5f, 2.0f},
    {"CL open sea", 65536 * 14, 128, 0.051f, 0.5f, 20.0f},
    {"CL open sea, storm", 65536 * 14, 128, 0.051f, 0.5f, 200.0f},
};

struct Camera
{
    const char *name;
    float horizontalFov, aspect, height, pitch;
};

constexpr Camera kCameras[] = {
    {"ship camera", 1.285f, 9.0f / 20.0f, 12.0f, 0.15f},
    {"ship camera high", 1.285f, 9.0f / 20.0f, 40.0f, 0.35f},
    {"sea field of view at its maximum", 2.085f, 9.0f / 20.0f, 12.0f, 0.15f},
    {"free camera", 1.285f, 9.0f / 20.0f, 300.0f, 0.6f},
};

// The four side planes of a perspective camera, inward normals, as Renderer::GetPlanes gives them to the sea.
template <typename Sea> void Place(Sea &sea, const Setup &setup, glm::vec3 position, float yaw, const Camera &camera)
{
    sea.dwMaxDim = setup.maxDim;
    sea.dwMinDim = setup.minDim;
    sea.fGridStep = setup.gridStep;
    sea.fLodScale = setup.lodScale;
    sea.fMaxSeaHeight = setup.maxSeaHeight;
    sea.vCamPos = position;
    const float th = std::tan(camera.horizontalFov / 2.0f), tv = th * camera.aspect;
    const glm::vec3 forward{std::sin(yaw) * std::cos(camera.pitch), -std::sin(camera.pitch),
                            std::cos(yaw) * std::cos(camera.pitch)};
    const glm::vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    glm::vec3 up = glm::cross(forward, right);
    if (up.y < 0.0f)
        up = -up;
    const auto plane = [&](float tangent, glm::vec3 side, int index) {
        const glm::vec3 n = forward * tangent + side;
        sea.pFrustumPlanes[index] = PLANE{n.x, n.y, n.z, glm::dot(n, position)};
    };
    plane(th, right, 0);
    plane(th, -right, 1);
    plane(tv, up, 2);
    plane(tv, -up, 3);
}

template <typename Sea> bool UnderCamera(const Sea &sea, const SeaBlockData &b)
{
    const float size = sea.fGridStep * static_cast<float>(b.iSize);
    const float x1 = sea.vSeaCenterPos.x + static_cast<float>(b.iTX) * size;
    const float z1 = sea.vSeaCenterPos.z + static_cast<float>(b.iTY) * size;
    return sea.vCamPos.x >= x1 && sea.vCamPos.x < x1 + size && sea.vCamPos.z >= z1 && sea.vCamPos.z < z1 + size;
}

int32_t GridPoints(const std::vector<SeaBlockData> &blocks)
{
    int32_t points = 0;
    for (const auto &b : blocks)
        points += (b.iSize0 + 1) * (b.iSize0 + 1);
    return points;
}

struct Totals
{
    int frames = 0, cut = 0, cameraBlockLost = 0, overflow = 0, mostVertices = 0;
};

// One frame of the fixed code: the whole sea pass up to the vertex upload.
void FrameNow(const Setup &setup, const Camera &camera, glm::vec3 position, float yaw, Totals &totals)
{
    now::SEA sea;
    Place(sea, setup, position, yaw, camera);
    sea.PlaceTree();
    sea.BuildTree(0, 0, 0);
    const auto all = sea.aBlocks;
    const bool cameraBlock = std::any_of(all.begin(), all.end(), [&](const auto &b) { return UnderCamera(sea, b); });
    sea.FitBlocksToBuffer();
    std::fill(sea.indices.begin(), sea.indices.end(), 0xFFFFFFFFu);
    for (uint32_t i = 0; i < sea.aBlocks.size(); i++)
        sea.PrepareIndicesForBlock(i);

    ++totals.frames;
    totals.mostVertices = std::max(totals.mostVertices, sea.iVStart);
    CHECK(sea.iVStart <= NUM_VERTEXS && sea.iTStart <= NUM_INDICES, "%s, %s: %d vertices, %d triangles", setup.name,
          camera.name, sea.iVStart, sea.iTStart);
    CHECK(std::all_of(sea.triangles.begin(), sea.triangles.begin() + sea.iTStart * 3,
                      [&](uint16_t index) { return index < sea.iVStart; }),
          "%s, %s: a triangle indexes a vertex that was not written", setup.name, camera.name);
    if (sea.aBlocks.size() < all.size())
    {
        ++totals.cut;
        CHECK(GridPoints(sea.aBlocks) <= NUM_VERTEXS, "%s, %s: kept blocks need %d points", setup.name, camera.name,
              GridPoints(sea.aBlocks));
        // The kept blocks are the nearest: the finest LOD is kept, and no dropped block is finer than a kept one.
        int keptFinest = 1000, droppedFinest = 1000;
        for (const auto &b : sea.aBlocks)
            keptFinest = std::min(keptFinest, b.iLOD);
        for (const auto &b : all)
            if (std::none_of(sea.aBlocks.begin(), sea.aBlocks.end(),
                             [&](const auto &k) { return k.iTX == b.iTX && k.iTY == b.iTY && k.iSize == b.iSize; }))
                droppedFinest = std::min(droppedFinest, b.iLOD);
        int keptCoarsest = 0;
        for (const auto &b : sea.aBlocks)
            keptCoarsest = std::max(keptCoarsest, b.iLOD);
        CHECK(droppedFinest >= keptCoarsest, "%s, %s: a dropped block (LOD %d) is nearer than a kept one (LOD %d)",
              setup.name, camera.name, droppedFinest, keptCoarsest);
    }
    if (cameraBlock)
        CHECK(std::any_of(sea.aBlocks.begin(), sea.aBlocks.end(), [&](const auto &b) { return UnderCamera(sea, b); }),
              "%s, %s: the block under the camera is not drawn", setup.name, camera.name);
}

// One frame of the original cap; its index pass would overflow the buffers, so only the kept blocks are counted.
void FrameOriginal(const Setup &setup, const Camera &camera, glm::vec3 position, float yaw, Totals &totals)
{
    original::SEA sea;
    Place(sea, setup, position, yaw, camera);
    sea.PlaceTree();
    sea.BuildTree(0, 0, 0);
    const bool cameraBlock =
        std::any_of(sea.aBlocks.begin(), sea.aBlocks.end(), [&](const auto &b) { return UnderCamera(sea, b); });
    sea.FitBlocksToBuffer();
    ++totals.frames;
    if (cameraBlock &&
        std::none_of(sea.aBlocks.begin(), sea.aBlocks.end(), [&](const auto &b) { return UnderCamera(sea, b); }))
        ++totals.cameraBlockLost;
    if (GridPoints(sea.aBlocks) > NUM_VERTEXS)
        ++totals.overflow;
}
} // namespace

int main()
{
    Totals fixed, reference;
    for (const auto &setup : kSetups)
        for (const auto &camera : kCameras)
        {
            Totals frames;
            for (int a = 0; a < 36; a++)
                for (int p = 0; p < 4; p++)
                {
                    const glm::vec3 position{1234.5f + p * 37.7f, camera.height, -777.3f + p * 91.1f};
                    const float yaw = a * (6.2831853f / 36.0f);
                    FrameNow(setup, camera, position, yaw, frames);
                    FrameOriginal(setup, camera, position, yaw, reference);
                }
            std::printf("%-20s %-34s at most %5d vertices, %3d of %d frames drop far blocks\n", setup.name, camera.name,
                        frames.mostVertices, frames.cut, frames.frames);
            fixed.cut += frames.cut;
        }
    std::printf("original cap: %d of %d frames lose the block under the camera, %d overflow the buffer\n",
                reference.cameraBlockLost, reference.frames, reference.overflow);
    CHECK(reference.cameraBlockLost + reference.overflow > 0, "the reference must show the original defect");
    CHECK(fixed.cut > 0, "some set-up must exceed the buffer, or the budget is not tested");
    CHECK(spdlog::warnings > 0, "a frame that drops blocks must be reported");
    if (failures)
        std::printf("%d failures\n", failures);
    else
        std::printf("sea vertex budget: OK\n");
    return failures ? 1 : 0;
}
