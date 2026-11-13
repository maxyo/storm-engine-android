#include "upscale.hpp"

#include "backend.hpp"
#include "storm/fx/upscale.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace storm::relief
{
namespace
{
// sRGB <-> linear by tables: the network's output quantised to 12 bits on the way in (finer than the 8-bit result),
// linear light to 16 bits on the way out.
struct Transfer
{
    float to_linear[4096];
    uint8_t to_srgb[65536];
    Transfer()
    {
        for (int i = 0; i < 4096; ++i)
        {
            const float c = static_cast<float>(i) / 4095.0f;
            to_linear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        for (int i = 0; i < 65536; ++i)
        {
            const float l = static_cast<float>(i) / 65535.0f;
            const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
            to_srgb[i] = static_cast<uint8_t>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
};
const Transfer &transfer()
{
    static const Transfer table;
    return table;
}

// A texel index beyond the image: wrapped around (a tiling texture) or mirrored without repeating the edge.
int Edge(int i, int n, bool wrap)
{
    if (n == 1)
        return 0;
    if (wrap)
        return ((i % n) + n) % n;
    const int period = 2 * (n - 1);
    i = ((i % period) + period) % period;
    return i < n ? i : period - i;
}
} // namespace

Upscaler::Upscaler(std::unique_ptr<Network> network) : network_(std::move(network))
{
}
Upscaler::~Upscaler() = default;

void Upscaler::Cancel()
{
    network_->Cancel();
}

bool Upscaler::Run(std::span<const uint8_t> rgba, int width, int height, bool wrap, std::vector<uint8_t> &out, std::string &error)
{
    using Clock = std::chrono::steady_clock;
    infer_ms_ = 0.0;
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
    {
        error = "no image";
        return false;
    }
    const Transfer &t = transfer();
    const int ow = width * 2, oh = height * 2;
    out.assign(static_cast<size_t>(ow) * oh * 4, 255);
    constexpr int pad = fx::kUpscalePad;
    std::vector<float> input, output;
    for (int y0 = 0; y0 < height; y0 += fx::kUpscaleTile)
        for (int x0 = 0; x0 < width; x0 += fx::kUpscaleTile)
        {
            const int tw = std::min(fx::kUpscaleTile, width - x0), th = std::min(fx::kUpscaleTile, height - y0);
            // Pad from the neighbouring texels; beyond the image's edges wrapped or mirrored.
            const int pw = tw + 2 * pad, ph = th + 2 * pad;
            const size_t plane = static_cast<size_t>(pw) * ph;
            input.resize(plane * 3);
            for (int y = 0; y < ph; ++y)
            {
                const int sy = Edge(y0 + y - pad, height, wrap);
                for (int x = 0; x < pw; ++x)
                {
                    const uint8_t *texel = rgba.data() + (static_cast<size_t>(sy) * width + Edge(x0 + x - pad, width, wrap)) * 4;
                    for (int c = 0; c < 3; ++c)
                        input[c * plane + static_cast<size_t>(y) * pw + x] = static_cast<float>(texel[c]) / 255.0f;
                }
            }
            output.resize(plane * 16 * 3);
            const Clock::time_point start = Clock::now();
            if (!network_->Infer(input.data(), pw, ph, output.data(), error))
                return false;
            infer_ms_ += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            // The tile's ×4 without its padding, each 2x2 averaged in linear light.
            const size_t big = plane * 16;
            const int bw = pw * 4;
            for (int y = 0; y < th * 2; ++y)
                for (int x = 0; x < tw * 2; ++x)
                {
                    uint8_t *texel = out.data() + (static_cast<size_t>(y0 * 2 + y) * ow + x0 * 2 + x) * 4;
                    const size_t top = static_cast<size_t>(pad * 4 + y * 2) * bw + pad * 4 + x * 2;
                    for (int c = 0; c < 3; ++c)
                    {
                        const float *p = output.data() + c * big + top;
                        const auto lin = [&](float v) {
                            return t.to_linear[static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 4095.0f + 0.5f)];
                        };
                        const float l = (lin(p[0]) + lin(p[1]) + lin(p[bw]) + lin(p[bw + 1])) * 0.25f;
                        texel[c] = t.to_srgb[static_cast<int>(l * 65535.0f + 0.5f)];
                    }
                }
        }
    // The alpha bilinearly, texel centres aligned, with the same edges; an opaque texture keeps its 255.
    bool opaque = true;
    for (size_t i = 3; i < rgba.size() && opaque; i += 4)
        opaque = rgba[i] == 255;
    if (!opaque)
        for (int y = 0; y < oh; ++y)
            for (int x = 0; x < ow; ++x)
            {
                const float fx = (static_cast<float>(x) + 0.5f) * 0.5f - 0.5f, fy = (static_cast<float>(y) + 0.5f) * 0.5f - 0.5f;
                const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
                const float ax = fx - static_cast<float>(x0), ay = fy - static_cast<float>(y0);
                const auto a = [&](int xi, int yi) {
                    return static_cast<float>(
                        rgba[(static_cast<size_t>(Edge(yi, height, wrap)) * width + Edge(xi, width, wrap)) * 4 + 3]);
                };
                const float v = (a(x0, y0) * (1 - ax) + a(x0 + 1, y0) * ax) * (1 - ay) +
                                (a(x0, y0 + 1) * (1 - ax) + a(x0 + 1, y0 + 1) * ax) * ay;
                out[(static_cast<size_t>(y) * ow + x) * 4 + 3] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
            }
    return true;
}

} // namespace storm::relief
