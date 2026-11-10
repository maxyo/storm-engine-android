#include "storm/fx/normal_map.hpp"

#include "storm/fx/log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace storm::fx
{
namespace
{
struct Field
{
    int width, height;
    bool wrap;
    std::vector<float> values;

    float At(int x, int y) const
    {
        if (wrap)
        {
            x = ((x % width) + width) % width;
            y = ((y % height) + height) % height;
        }
        else
        {
            x = std::clamp(x, 0, width - 1);
            y = std::clamp(y, 0, height - 1);
        }
        return values[static_cast<size_t>(y) * width + x];
    }
};

Field Blur(const Field &in)
{
    Field out = in;
    for (int y = 0; y < in.height; ++y)
        for (int x = 0; x < in.width; ++x)
        {
            float sum = 0.0f;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    sum += in.At(x + dx, y + dy) * static_cast<float>((2 - std::abs(dx)) * (2 - std::abs(dy)));
            out.values[static_cast<size_t>(y) * in.width + x] = sum / 16.0f;
        }
    return out;
}

Field Halve(const Field &in)
{
    Field out{std::max(in.width / 2, 1), std::max(in.height / 2, 1), in.wrap, {}};
    out.values.resize(static_cast<size_t>(out.width) * out.height);
    const int sx = in.width > 1 ? 2 : 1, sy = in.height > 1 ? 2 : 1;
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x)
        {
            float sum = 0.0f;
            for (int dy = 0; dy < sy; ++dy)
                for (int dx = 0; dx < sx; ++dx)
                    sum += in.At(x * sx + dx, y * sy + dy);
            out.values[static_cast<size_t>(y) * out.width + x] = sum / static_cast<float>(sx * sy);
        }
    return out;
}
} // namespace

uint64_t NormalMapCacheKey(const NormalMapOptions &options)
{
    uint32_t strength;
    std::memcpy(&strength, &options.strength, sizeof(strength));
    uint64_t key = 0xcbf29ce484222325ull;
    for (uint64_t part : {uint64_t{kNormalMapVersion}, uint64_t{strength}, uint64_t{options.wrap}, uint64_t{options.pre_blur}})
        key = (key ^ part) * 0x100000001b3ull;
    return key;
}

NormalMap DeriveNormalMap(std::span<const uint8_t> height, int width, int height_px, const NormalMapOptions &options)
{
    NormalMap map;
    if (width <= 0 || height_px <= 0 || height.size() < static_cast<size_t>(width) * height_px)
        return map;
    map.width = width;
    map.height = height_px;
    Field field{width, height_px, options.wrap, {}};
    field.values.resize(static_cast<size_t>(width) * height_px);
    for (size_t i = 0; i < field.values.size(); ++i)
        field.values[i] = static_cast<float>(height[i]) / 255.0f;
    if (options.pre_blur)
        field = Blur(field);

    // Slope scale of the top mip; each smaller mip has texels twice as large, so the same world slope is
    // half as many height units per texel.
    float scale = options.strength;
    for (;;)
    {
        ++map.mip_count;
        for (int y = 0; y < field.height; ++y)
            for (int x = 0; x < field.width; ++x)
            {
                // Scharr: [-1 0 1] derivative, [3 10 3] / 16 smoothing across.
                const auto row = [&](int dy) { return field.At(x + 1, y + dy) - field.At(x - 1, y + dy); };
                const auto column = [&](int dx) { return field.At(x + dx, y + 1) - field.At(x + dx, y - 1); };
                const float du = (3.0f * row(-1) + 10.0f * row(0) + 3.0f * row(1)) / 32.0f;
                const float dv = (3.0f * column(-1) + 10.0f * column(0) + 3.0f * column(1)) / 32.0f;
                float nx = -du * scale, ny = -dv * scale;
                const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + 1.0f);
                nx *= inv;
                ny *= inv;
                map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(0.5f + 0.5f * nx, 0.0f, 1.0f) * 255.0f)));
                map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(0.5f + 0.5f * ny, 0.0f, 1.0f) * 255.0f)));
            }
        if (field.width == 1 && field.height == 1)
            break;
        field = Halve(field);
        scale *= 0.5f;
    }
    return map;
}

HeightMapManifest HeightMapManifest::Parse(std::string_view text)
{
    HeightMapManifest manifest;
    while (!text.empty())
    {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        line = line.substr(0, line.find('#'));
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.remove_suffix(1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
            line.remove_prefix(1);
        if (line.size() < 2 || (line[0] != '+' && line[0] != '-'))
            continue;
        (line[0] == '+' ? manifest.include : manifest.exclude).push_back(NormalizeTexturePath(line.substr(1)));
    }
    return manifest;
}

std::string NormalizeTexturePath(std::string_view path)
{
    std::string out;
    for (char c : path)
        if (!std::isspace(static_cast<unsigned char>(c)) || !out.empty())
            out.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (std::string_view extension : {".tx", ".tga"})
        if (out.size() > extension.size() && out.compare(out.size() - extension.size(), extension.size(), extension) == 0)
            out.resize(out.size() - extension.size());
    return out;
}

bool NameSuggestsHeightMap(std::string_view normalized_path)
{
    const size_t slash = normalized_path.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? normalized_path : normalized_path.substr(slash + 1);
    return name.find("bump") != std::string_view::npos;
}

bool IsGrayscale(std::span<const uint8_t> rgba)
{
    if (rgba.size() < 4)
        return false;
    // DXT stores colours as 5:6:5, so a grey image comes back with channel differences of a few steps.
    size_t far_off = 0;
    uint64_t total = 0;
    const size_t pixels = rgba.size() / 4;
    for (size_t i = 0; i < pixels; ++i)
    {
        const int r = rgba[4 * i], g = rgba[4 * i + 1], b = rgba[4 * i + 2];
        const int spread = std::max({r, g, b}) - std::min({r, g, b});
        total += static_cast<uint64_t>(spread);
        far_off += spread > 24;
    }
    return total <= pixels * 8 && far_off * 100 <= pixels;
}

HeightMapClassifier::HeightMapClassifier(HeightMapManifest manifest) : manifest_(std::move(manifest))
{
}

HeightMapVerdict HeightMapClassifier::Classify(std::string_view path, std::span<const uint8_t> rgba)
{
    const std::string name = NormalizeTexturePath(path);
    const auto listed = [&name](const std::vector<std::string> &list) { return std::find(list.begin(), list.end(), name) != list.end(); };
    HeightMapVerdict verdict;
    if (listed(manifest_.exclude))
        verdict = {false, HeightMapReason::ManifestExclude};
    else if (listed(manifest_.include))
        verdict = {true, HeightMapReason::ManifestInclude};
    else if (!NameSuggestsHeightMap(name))
        verdict = {false, HeightMapReason::NameNoMatch};
    else if (name.find("weather/sea/") != std::string::npos)
        verdict = {false, HeightMapReason::ExcludedFolder};
    else if (!IsGrayscale(rgba))
        verdict = {false, HeightMapReason::NotGrayscale};
    else
        verdict = {true, HeightMapReason::NameAndGrayscale};

    // Ordinary textures (no name match, no manifest entry) are not worth a line each; everything else is.
    if (verdict.reason != HeightMapReason::NameNoMatch && std::find(logged_.begin(), logged_.end(), name) == logged_.end())
    {
        logged_.push_back(name);
        static constexpr const char *kReasons[] = {
            "height map (manifest)",        "not a height map (manifest)",          "height map (name contains \"bump\", grayscale)",
            "not a height map (name)",      "not a height map (animated sea frames)", "not a height map (name contains \"bump\" but coloured)"};
        Log(LogLevel::Info, "texture %s: %s", name.c_str(), kReasons[static_cast<int>(verdict.reason)]);
    }
    return verdict;
}

} // namespace storm::fx
