// Where a relief's height can be trusted with the parallax: a per-texel mask made when the relief is generated.
//
// PBRify's height is right for plain wood and stone, wrong where the picture is a window, a door's glass or a sign:
// a pane of glass has no relief, but its dark picture gets a deep one, and the parallax shifts the pane's texels and
// the frame's against each other (the owner: some houses look excellent with it, but the windows look awful). The mask
// takes the parallax away only there: normals stay everywhere, and so does the cavity wherever the height is trusted.
//
// Classical image processing on the colour texture and the height at the map's size (no model: nothing to ship, no
// licence, a few ms per texture), two detectors. Both work on blobs, because what the parallax gets wrong is a compact
// recess or pane, not the thin joints and seams (planks, mortar) that make wood and stone:
//   - recess: the height lies deeper than its surroundings (a grey closing of kTrustClosingTexels minus the height above
//     pit_depth) and the colour is darker than its neighbourhood (below pit_dark of the large-scale mean): a window set
//     into a wall. Kept when the blob is at least pit_min_area texels and fills at least pit_extent of its bounding box
//     (windows, doors and arches do; the round hollows between stones, 0.785 for an ellipse, mostly do not).
//   - glass: dark (below glass_luminance) and uniform (local deviation below glass_flat) over a blob of at least
//     glass_min_area texels: night windows, panes, shadows painted into the picture.
// What they find is grown by grow_texels (the frame around a pane) and feathered by kTrustFeatherSigma, so the
// parallax fades out over a few texels instead of tearing the picture at the mask's edge. Distances are in texels of
// the 512 map (scaled with the map's size).
//
// The mask is part of the relief method (kHeightMapVersion). It is stored in the material texture's height: where the
// trust is t, the height is 1 - t (1 - h): the top, where the parallax shifts nothing. Beside it the file keeps the mask
// itself at a quarter of the resolution (NormalMap::trust) for the debug view, and the share of trusted texels
// (NormalMap::trust_coverage), by which a texture with almost none gets no parallax at all (kParallaxMinCoverage).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

inline constexpr float kTrustClosingTexels = 14.0f;
inline constexpr float kTrustFeatherSigma = 2.5f;

// The detectors' thresholds. Strict (house atlases, everything else: the owner's «when in doubt, mask it out»): a recess
// deeper than 0.15 of the height's range and darker than 0.9 of its surroundings; glass below 0.22 luminance and 0.04
// local deviation; grown by 8 texels. Masonry (class Stone: cobbles, stone and brick walls, where the hollows between
// stones look like recesses to the strict one: measured, it masked 40 % of a plain stone wall): 0.22 / 0.8, glass 0.16 /
// 0.025, grown by 5. Both keep recesses to blobs of at least 120 texels (windows and doors; the hollows of stones are
// smaller) that fill at least `pit_extent` of their box.
struct TrustParams
{
    float pit_depth, pit_dark, pit_min_area, pit_extent;
    float glass_luminance, glass_flat, glass_min_area;
    float grow_texels;
};
inline constexpr TrustParams kTrustStrict{0.15f, 0.9f, 120.0f, 0.45f, 0.22f, 0.04f, 40.0f, 8.0f};
inline constexpr TrustParams kTrustMasonry{0.22f, 0.8f, 120.0f, 0.55f, 0.16f, 0.025f, 60.0f, 5.0f};

// A texture whose trusted texels (trust above one half) are fewer than this share of it gets no parallax.
inline constexpr float kParallaxMinCoverage = 0.5f;

// The trust, 0..1 per texel of a w x h map, from `rgba` (the colour texture at the same size, 4 bytes a texel, R first)
// and `height` (0..1, the map's height before the mask). Empty on mismatched sizes.
std::vector<float> ParallaxTrust(std::span<const uint8_t> rgba, std::span<const float> height, int w, int h,
                                 const TrustParams &params = kTrustStrict);

// The share of texels with a trust above one half.
float TrustCoverage(std::span<const float> trust);

// The mask for the file: R8, ceil(w / 4) x ceil(h / 4), box-averaged.
std::vector<uint8_t> TrustThumbnail(std::span<const float> trust, int w, int h);
inline int TrustThumbnailSize(int texels) { return (texels + 3) / 4; }

} // namespace storm::fx
