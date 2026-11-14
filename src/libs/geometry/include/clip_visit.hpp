#pragma once
// A side channel of GEOM::Clip for callers that keep the polygons of a clip (the blob shadows' fan).
//
// GEOM::Clip walks a BSP tree and offers the faces of the nodes whose plane the sphere touches; which faces that is
// depends on the tree, not on the faces' own positions. With `owner` set by the node being clipped, `face` and
// `owner` name the face behind every polygon handed to the ADD_POLYGON_FUNC. With `collect` set, a clip only walks:
// it appends the key of every face it would have offered (clipped or not) and clips and offers nothing.
#include <cstdint>
#include <vector>

namespace storm::geometry
{
struct ClipVisit
{
    const void *owner = nullptr; // the node of the model being clipped (set by NODER::Clip)
    int32_t face = -1;           // the face of the polygon being offered
    std::vector<uint64_t> *collect = nullptr;
};

inline ClipVisit clipVisit;

inline uint64_t clipFaceKey(const void *owner, int32_t face)
{
    return (reinterpret_cast<uintptr_t>(owner) << 24) ^ static_cast<uint64_t>(face);
}
} // namespace storm::geometry
