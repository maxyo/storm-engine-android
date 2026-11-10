#include "android_render_resolution.hpp"

#include <cassert>
#include <climits>
#include <cstring>
#include <iostream>

using namespace storm::android;

static void expect(RenderExtent view, RenderExtent legacy, RenderExtent wanted, const char *reason)
{
    const auto result = chooseStartupRenderResolution(view, legacy);
    assert(result.extent.width == wanted.width && result.extent.height == wanted.height);
    assert(std::strcmp(result.reason, reason) == 0);
}

int main()
{
    // The same imported PC settings must not supersample either Android view.
    expect({1280, 720}, {1920, 1080}, {1280, 720}, "view");
    expect({960, 540}, {1920, 1080}, {960, 540}, "view");
    // Native high-density, ultrawide, tablet and portrait views have no 720p cap,
    // minimum-axis upscale or inherited desktop aspect restriction.
    for (const auto view : {RenderExtent{2560, 1440}, {2400, 1080}, {2048, 1536}, {720, 1280}, {480, 270}})
        expect(view, {640, 480}, view, "view");
    expect({1280, 720}, {-1, INT_MAX}, {1280, 720}, "view");
    expect({16384, 9216}, {1920, 1080}, {8192, 4608}, "view-limited");
    expect({9216, 16384}, {1920, 1080}, {4608, 8192}, "view-limited");
    expect({INT_MAX, INT_MAX}, {0, 0}, {8192, 8192}, "view-limited");
    for (const auto view : {RenderExtent{0, 0}, {1280, 0}, {0, 720}, {-1, 720}, {INT_MAX, 1}})
    {
        expect(view, {1920, 1080}, {1920, 1080}, "unavailable-view-legacy");
        expect(view, {16384, 9216}, {8192, 4608}, "unavailable-view-legacy-limited");
        for (const auto legacy : {RenderExtent{0, 0}, {-1, 1080}, {1920, 0}, {1, INT_MAX}})
            expect(view, legacy, {1024, 768}, "unavailable-view-default");
    }
    // Bounds preserve the source aspect within the unavoidable subpixel floor.
    for (const auto extent : {RenderExtent{10001, 7003}, {7003, 10001}, {INT_MAX, INT_MAX / 2}})
    {
        const auto bounded = boundedRenderExtent(extent);
        const auto longest = std::max(extent.width, extent.height);
        assert(bounded.width > 0 && bounded.height > 0);
        assert(bounded.width <= maxRenderDimension && bounded.height <= maxRenderDimension);
        assert(int64_t(extent.width) * maxRenderDimension - int64_t(bounded.width) * longest < longest);
        assert(int64_t(extent.height) * maxRenderDimension - int64_t(bounded.height) * longest < longest);
    }
    std::cout << "PASS Android startup render size: view, fallback, aspect bounds, overflow\n";
}
