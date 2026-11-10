#pragma once

#include <algorithm>
#include <cstdint>

namespace storm::android
{
struct RenderExtent
{
    int width;
    int height;
};

struct StartupRenderResolution
{
    RenderExtent extent;
    const char *reason;
};

// Retain the renderer's existing 8192-axis safety bound. This is not a quality
// preset or a claim about a device's supported texture dimensions.
inline constexpr int maxRenderDimension = 8192;

inline RenderExtent boundedRenderExtent(RenderExtent extent)
{
    if (extent.width <= 0 || extent.height <= 0)
        return {};
    const auto longest = std::max(extent.width, extent.height);
    if (longest <= maxRenderDimension)
        return extent;
    // One scale for both axes; independent clamps would change camera aspect.
    // Use wide arithmetic even for malformed INT_MAX configuration values.
    extent.width = static_cast<int>(int64_t(extent.width) * maxRenderDimension / longest);
    extent.height = static_cast<int>(int64_t(extent.height) * maxRenderDimension / longest);
    // An aspect ratio that cannot retain even one pixel is not a usable view.
    return extent.width > 0 && extent.height > 0 ? extent : RenderExtent{};
}

inline StartupRenderResolution chooseStartupRenderResolution(RenderExtent view, RenderExtent legacy)
{
    const auto actual = boundedRenderExtent(view);
    if (actual.width > 0)
        return {actual, actual.width == view.width && actual.height == view.height ? "view" : "view-limited"};
    const auto fallback = boundedRenderExtent(legacy);
    if (fallback.width > 0)
        return {fallback, fallback.width == legacy.width && fallback.height == legacy.height
            ? "unavailable-view-legacy" : "unavailable-view-legacy-limited"};
    return {{1024, 768}, "unavailable-view-default"};
}
} // namespace storm::android
