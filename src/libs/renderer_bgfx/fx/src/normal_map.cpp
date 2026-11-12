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

    // Texel index of coordinate i on an axis of `length` (wrapped or clamped).
    int Index(int i, int length) const { return wrap ? ((i % length) + length) % length : std::clamp(i, 0, length - 1); }
    // Indices of -pad .. length + pad - 1 on an axis: offsets[i + pad].
    std::vector<int> Indices(int length, int pad) const
    {
        std::vector<int> indices(static_cast<size_t>(length + 2 * pad));
        for (int i = -pad; i < length + pad; ++i)
            indices[static_cast<size_t>(i + pad)] = Index(i, length);
        return indices;
    }
};

// One filter pass along x (horizontal) or y: out[i] = sum_k taps[k] * in[i + k - r].
Field Convolve(const Field &in, std::span<const float> taps, bool horizontal)
{
    Field out = in;
    const int r = static_cast<int>(taps.size() / 2);
    const int length = horizontal ? in.width : in.height, lines = horizontal ? in.height : in.width;
    const std::vector<int> at = in.Indices(length, r);
    const size_t step = horizontal ? 1 : static_cast<size_t>(in.width), line_step = horizontal ? static_cast<size_t>(in.width) : 1;
    for (int line = 0; line < lines; ++line)
    {
        const float *src = in.values.data() + line * line_step;
        float *dst = out.values.data() + line * line_step;
        for (int i = 0; i < length; ++i)
        {
            float sum = 0.0f;
            for (size_t k = 0; k < taps.size(); ++k)
                sum += taps[k] * src[at[static_cast<size_t>(i) + k] * step];
            dst[i * step] = sum;
        }
    }
    return out;
}

// 3x3 binomial.
Field Blur(const Field &in)
{
    static constexpr float kTaps[] = {0.25f, 0.5f, 0.25f};
    return Convolve(Convolve(in, kTaps, true), kTaps, false);
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
                    sum += in.values[static_cast<size_t>(y * sy + dy) * in.width + x * sx + dx];
            out.values[static_cast<size_t>(y) * out.width + x] = sum / static_cast<float>(sx * sy);
        }
    return out;
}

// One box filter of width 2r+1 along x (horizontal) or y, with a running sum.
Field Box(const Field &in, int r, bool horizontal)
{
    Field out = in;
    const int length = horizontal ? in.width : in.height, lines = horizontal ? in.height : in.width;
    const std::vector<int> at = in.Indices(length, r + 1);
    const size_t step = horizontal ? 1 : static_cast<size_t>(in.width), line_step = horizontal ? static_cast<size_t>(in.width) : 1;
    const float scale = 1.0f / static_cast<float>(2 * r + 1);
    for (int line = 0; line < lines; ++line)
    {
        const float *src = in.values.data() + line * line_step;
        float *dst = out.values.data() + line * line_step;
        const auto value = [&](int i) { return src[at[static_cast<size_t>(i + r + 1)] * step]; };
        float sum = 0.0f;
        for (int i = -r; i <= r; ++i)
            sum += value(i);
        for (int i = 0; i < length; ++i)
        {
            dst[i * step] = sum * scale;
            sum += value(i + r + 1) - value(i - r);
        }
    }
    return out;
}

// Gaussian of `sigma` texels as three box passes per axis (their variance adds up: 3 (w² - 1) / 12 = sigma²).
Field Gaussian(const Field &in, float sigma)
{
    const int r = std::max(1, static_cast<int>(std::lround((std::sqrt(4.0f * sigma * sigma + 1.0f) - 1.0f) * 0.5f)));
    Field out = in;
    for (int pass = 0; pass < 3; ++pass)
        out = Box(Box(out, r, true), r, false);
    return out;
}

// The detail of the luminance as height: the high pass relative to the local brightness (a dark stone floor has as
// much relief as a bright one, and baked light or shade is not shape), softly limited (a lone black or white texel,
// or the edge of a painted patch, is not a deep hole or a wall).
void BandPass(Field &field, float sigma)
{
    const Field low = Gaussian(field, sigma);
    constexpr float kDark = 0.08f; // below this local brightness the contrast is noise, not shape
    constexpr float kLimit = 0.6f;
    for (size_t i = 0; i < field.values.size(); ++i)
        field.values[i] = kLimit * std::tanh((field.values[i] - low.values[i]) / (low.values[i] + kDark) / kLimit);
}

const char *const kClassNames[] = {"none", "height map", "stone", "wood", "building", "plaster"};

enum class Match : uint8_t
{
    Folder,   // the path starts with the pattern (a folder, "ships/"); the name is not looked at
    Prefix,   // the file name starts with the pattern
    Contains, // the file name contains the pattern
};

struct Rule
{
    const char *folder; // only paths in this folder ("" = any)
    Match match;
    const char *pattern;
    ReliefClass relief;
    const char *text; // the rule as logged
};

// TEHO's RESOURCE/Textures, first match wins. Location textures lie in the root folder under descriptive names
// (English, Russian transliterations, a few French); ships in Ships/<type>/Hull<n>/. Written from the texture list of
// the game; a texture no rule names gets no relief.
constexpr ReliefClass N = ReliefClass::None, S = ReliefClass::Stone, W = ReliefClass::Wood, B = ReliefClass::Building,
                      P = ReliefClass::Plaster;
#define STORM_RULE(folder, match, pattern, relief) {folder, Match::match, pattern, relief, folder pattern}
const Rule kRules[] = {
    // Folders of things without relief: characters, UI, sky and weather, light maps, effects, foliage, items.
    STORM_RULE("characters/", Folder, "", N),
    STORM_RULE("interfaces/", Folder, "", N),
    STORM_RULE("battle_interface/", Folder, "", N),
    STORM_RULE("mainmenu/", Folder, "", N),
    STORM_RULE("loading/", Folder, "", N),
    STORM_RULE("fonts/", Folder, "", N),
    STORM_RULE("dialog/", Folder, "", N),
    STORM_RULE("crosshair/", Folder, "", N),
    STORM_RULE("tips/", Folder, "", N),
    STORM_RULE("quest_signs/", Folder, "", N),
    STORM_RULE("weather/", Folder, "", N),
    STORM_RULE("lighting/", Folder, "", N), // the locations' baked light maps
    STORM_RULE("worldmap/", Folder, "", N),
    STORM_RULE("islands/", Folder, "", N), // islands seen from the sea: too far for relief
    STORM_RULE("grass/", Folder, "", N),
    STORM_RULE("animals/", Folder, "", N),
    STORM_RULE("items/", Folder, "", N),
    STORM_RULE("ammo/", Folder, "", N),
    STORM_RULE("flags/", Folder, "", N),
    STORM_RULE("shipsflares/", Folder, "", N),
    STORM_RULE("locefx/", Folder, "", N),
    STORM_RULE("tornado/", Folder, "", N),
    STORM_RULE("texturesequence/", Folder, "", N),
    // Ships: sails, wakes and the alpha-tested rigging have none; hulls, decks and boats are planks.
    STORM_RULE("ships/", Contains, "parus", N),
    STORM_RULE("ships/", Contains, "trail", N),
    STORM_RULE("ships/", Contains, "alpha", N),
    STORM_RULE("ships/", Contains, "col", N),
    STORM_RULE("ships/", Folder, "", W),

    // Names. Buildings whose names start like an excluded word come first.
    STORM_RULE("", Contains, "windmill", B),
    STORM_RULE("", Contains, "fonar", N), // lanterns
    STORM_RULE("", Contains, "lighthouse", S),
    // Not surfaces with relief: occlusion / reflection / mask maps, windows and glass, light, fire, smoke, water,
    // foliage cards, cloth, metal, bones, signs, UI and effect sheets.
    STORM_RULE("", Prefix, "occl", N),
    STORM_RULE("", Prefix, "refl", N),
    STORM_RULE("", Contains, "mask", N),
    STORM_RULE("", Prefix, "wind", N), // windows lit at night (windTavern1, windBigHouse01...)
    STORM_RULE("", Contains, "window", N),
    STORM_RULE("", Contains, "glass", N),
    STORM_RULE("", Contains, "win_", N),
    STORM_RULE("", Contains, "_win", N),
    STORM_RULE("", Contains, "alpha", N),
    STORM_RULE("", Contains, "alfa", N),
    STORM_RULE("", Contains, "light", N),
    STORM_RULE("", Contains, "lamp", N),
    STORM_RULE("", Contains, "candle", N),
    STORM_RULE("", Contains, "corona", N),
    STORM_RULE("", Contains, "fire", N),
    STORM_RULE("", Contains, "bonfire", N),
    STORM_RULE("", Contains, "flash", N),
    STORM_RULE("", Contains, "spark", N),
    STORM_RULE("", Contains, "smoke", N),
    STORM_RULE("", Contains, "shadow", N),
    STORM_RULE("", Contains, "sky", N),
    STORM_RULE("", Prefix, "sea", N),
    STORM_RULE("", Contains, "water", N),
    STORM_RULE("", Contains, "under", N), // underwater
    STORM_RULE("", Contains, "caust", N),
    STORM_RULE("", Contains, "foam", N),
    STORM_RULE("", Contains, "splash", N),
    STORM_RULE("", Contains, "fall", N), // waterfalls
    STORM_RULE("", Contains, "river", N),
    STORM_RULE("", Contains, "lava", N),
    STORM_RULE("", Contains, "reef", N),
    STORM_RULE("", Contains, "coral", N),
    STORM_RULE("", Contains, "algae", N),
    STORM_RULE("", Contains, "leaf", N),
    STORM_RULE("", Contains, "leaves", N),
    STORM_RULE("", Contains, "palm", N),
    STORM_RULE("", Contains, "tree", N),
    STORM_RULE("", Contains, "plant", N), // also Plantation: its walls are atlases with foliage
    STORM_RULE("", Contains, "flower", N),
    STORM_RULE("", Contains, "flora", N),
    STORM_RULE("", Contains, "fern", N),
    STORM_RULE("", Contains, "grass", N), // grass ground: its blades read as worms, not as relief
    STORM_RULE("", Contains, "back", N), // background cards (back_jungle, back_mountains)
    STORM_RULE("", Contains, "cloth", N),
    STORM_RULE("", Contains, "carpet", N),
    STORM_RULE("", Contains, "fabric", N),
    STORM_RULE("", Contains, "sail", N),
    STORM_RULE("", Contains, "flag", N),
    STORM_RULE("", Contains, "rope", N),
    STORM_RULE("", Contains, "nets", N),
    STORM_RULE("", Contains, "web", N),
    STORM_RULE("", Contains, "chain", N),
    STORM_RULE("", Contains, "canopy", N),
    STORM_RULE("", Contains, "vant", N), // shrouds
    STORM_RULE("", Contains, "metal", N),
    STORM_RULE("", Contains, "iron", N),
    STORM_RULE("", Contains, "gold", N),
    STORM_RULE("", Contains, "gundeck", W),
    STORM_RULE("", Contains, "cannon", N),
    STORM_RULE("", Contains, "gun", N),
    STORM_RULE("", Contains, "skelet", N),
    STORM_RULE("", Contains, "skull", N),
    STORM_RULE("", Contains, "skin", N),
    STORM_RULE("", Contains, "telo", N),   // body
    STORM_RULE("", Contains, "golova", N), // head
    STORM_RULE("", Prefix, "eye", N),
    STORM_RULE("", Contains, "sign", N),
    STORM_RULE("", Contains, "painting", N),
    STORM_RULE("", Contains, "freska", N),
    STORM_RULE("", Contains, "map", N),
    STORM_RULE("", Contains, "menu", N),
    STORM_RULE("", Contains, "particles", N),
    STORM_RULE("", Contains, "item", N),
    STORM_RULE("", Contains, "treasure", N),
    STORM_RULE("", Contains, "blood", N),
    STORM_RULE("", Contains, "bird", N),
    STORM_RULE("", Contains, "seagull", N),
    STORM_RULE("", Contains, "fish", N),

    // Stone and brick.
    STORM_RULE("", Contains, "stone", S),
    STORM_RULE("", Contains, "brick", S),
    STORM_RULE("", Contains, "rock", S),
    STORM_RULE("", Contains, "skala", S), // rock
    STORM_RULE("", Contains, "pierre", S),
    STORM_RULE("", Contains, "cobble", S),
    STORM_RULE("", Contains, "town_cob", S),
    STORM_RULE("", Contains, "beton", S),
    STORM_RULE("", Contains, "fort", S),
    STORM_RULE("", Contains, "castle", S),
    STORM_RULE("", Contains, "cave", S),
    STORM_RULE("", Contains, "grot", S),
    STORM_RULE("", Contains, "crypt", S),
    STORM_RULE("", Contains, "temple", S),
    STORM_RULE("", Contains, "pyramid", S),
    STORM_RULE("", Contains, "piramid", S),
    STORM_RULE("", Contains, "incas", S),
    STORM_RULE("", Contains, "tenochtitlan", S),
    STORM_RULE("", Contains, "stal", S), // stalactites, stalagmites
    STORM_RULE("", Contains, "tile", S),
    STORM_RULE("", Contains, "border", S),
    STORM_RULE("", Contains, "stair", S),
    STORM_RULE("", Contains, "steps", S),
    STORM_RULE("", Contains, "arch", S),
    STORM_RULE("", Contains, "ruin", S),
    STORM_RULE("", Contains, "grave", S),
    STORM_RULE("", Contains, "cemetery", S),
    STORM_RULE("", Contains, "fontain", S),
    STORM_RULE("", Contains, "mayak", S), // lighthouse
    STORM_RULE("", Contains, "prison", S),
    STORM_RULE("", Contains, "mine", S),
    STORM_RULE("", Contains, "kamin", S), // fireplace

    // Wood and planks.
    STORM_RULE("", Contains, "wood", W),
    STORM_RULE("", Contains, "plank", W),
    STORM_RULE("", Contains, "deck", W),
    STORM_RULE("", Contains, "board", W),
    STORM_RULE("", Contains, "balk", W), // beams
    STORM_RULE("", Contains, "derevo", W),
    STORM_RULE("", Contains, "door", W),
    STORM_RULE("", Contains, "gate", W),
    STORM_RULE("", Contains, "pier", W),
    STORM_RULE("", Contains, "prichal", W), // pier
    STORM_RULE("", Contains, "hold", W),
    STORM_RULE("", Contains, "cabin", W),
    STORM_RULE("", Contains, "boat", W),
    STORM_RULE("", Prefix, "dm_", W), // deck models of the ships
    STORM_RULE("", Prefix, "cap", W), // captain's cabins and decks
    STORM_RULE("", Contains, "furniture", W),
    STORM_RULE("", Contains, "chest", W),
    STORM_RULE("", Contains, "barrel", W),
    STORM_RULE("", Contains, "ladder", W),
    STORM_RULE("", Contains, "grating", W),
    STORM_RULE("", Contains, "lafet", W), // gun carriages
    STORM_RULE("", Contains, "roof", W),
    STORM_RULE("", Contains, "hut", W),
    STORM_RULE("", Contains, "barn", W),
    STORM_RULE("", Contains, "objects_tex", W), // town props: crates, barrels, carts

    // Buildings: one atlas per house, tavern or church with its stone, plaster, beams and doors.
    STORM_RULE("", Prefix, "flh", B), // Flemish houses
    STORM_RULE("", Prefix, "sph", B), // Spanish houses
    STORM_RULE("", Contains, "shipyard", B),
    STORM_RULE("", Contains, "house", B),
    STORM_RULE("", Contains, "residence", B),
    STORM_RULE("", Contains, "tavern", B),
    STORM_RULE("", Contains, "store", B),
    STORM_RULE("", Contains, "bank", B),
    STORM_RULE("", Contains, "church", B),
    STORM_RULE("", Contains, "barracks", B),
    STORM_RULE("", Contains, "brothel", B),
    STORM_RULE("", Contains, "usurer", B),
    STORM_RULE("", Contains, "estate", B),
    STORM_RULE("", Contains, "arsenal", B),
    STORM_RULE("", Contains, "college", B),
    STORM_RULE("", Contains, "office", B),
    STORM_RULE("", Contains, "home", B),
    STORM_RULE("", Contains, "village", B),
    STORM_RULE("", Contains, "quarter", B),
    STORM_RULE("", Contains, "town", B),
    STORM_RULE("", Contains, "wall", B),

    // Plaster and ground.
    STORM_RULE("", Contains, "celling", P), // ceilings
    STORM_RULE("", Contains, "potolok", P), // ceiling
    STORM_RULE("", Contains, "floor", P),
    STORM_RULE("", Contains, "ground", P),
    STORM_RULE("", Contains, "grnd", P),
    STORM_RULE("", Contains, "land", P),
    STORM_RULE("", Contains, "sand", P),
    STORM_RULE("", Contains, "pesok", P), // sand
    STORM_RULE("", Contains, "beach", P),
    STORM_RULE("", Contains, "shore", P),
    STORM_RULE("", Contains, "dirt", P),
    STORM_RULE("", Contains, "path", P),
    STORM_RULE("", Contains, "tropa", P), // path
    STORM_RULE("", Contains, "jungle", P),

};
#undef STORM_RULE
} // namespace

const char *ReliefClassName(ReliefClass relief)
{
    return kClassNames[static_cast<int>(relief)];
}

NormalMapOptions ReliefOptions(ReliefClass relief)
{
    NormalMapOptions options;
    switch (relief)
    {
    case ReliefClass::None:
        options.strength = 0.0f;
        break;
    case ReliefClass::HeightMap:
        options.source = NormalMapSource::Height;
        options.strength = 2.0f;
        break;
    case ReliefClass::Stone:
        options.source = NormalMapSource::Luminance;
        options.strength = 7.0f;
        options.band_sigma = 4.0f;
        break;
    case ReliefClass::Wood:
        options.source = NormalMapSource::Luminance;
        options.strength = 5.0f;
        options.band_sigma = 3.0f;
        options.pre_blur = 2; // the grain is fine noise: smooth it, keep the planks' gaps
        break;
    case ReliefClass::Building:
        options.source = NormalMapSource::Luminance;
        options.strength = 5.0f;
        options.band_sigma = 3.0f;
        break;
    case ReliefClass::Plaster:
        options.source = NormalMapSource::Luminance;
        options.strength = 3.0f;
        options.band_sigma = 3.0f;
        options.pre_blur = 2;
        break;
    }
    return options;
}

uint64_t NormalMapCacheKey(const NormalMapOptions &options)
{
    uint32_t strength, sigma;
    std::memcpy(&strength, &options.strength, sizeof(strength));
    std::memcpy(&sigma, &options.band_sigma, sizeof(sigma));
    uint64_t key = 0xcbf29ce484222325ull;
    for (uint64_t part : {uint64_t{kNormalMapVersion}, uint64_t{static_cast<uint8_t>(options.source)}, uint64_t{strength},
                          uint64_t{options.wrap}, static_cast<uint64_t>(options.pre_blur), uint64_t{sigma},
                          static_cast<uint64_t>(options.max_size)})
        key = (key ^ part) * 0x100000001b3ull;
    return key;
}

void NormalMapSize(int width, int height, const NormalMapOptions &options, int &out_width, int &out_height)
{
    out_width = width;
    out_height = height;
    while (std::max(out_width, out_height) > std::max(options.max_size, 1))
    {
        out_width = std::max(out_width / 2, 1);
        out_height = std::max(out_height / 2, 1);
    }
}

size_t NormalMapLevelBytes(NormalMapFormat format, int width, int height)
{
    if (format == NormalMapFormat::EacRg11)
        return static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4) * 16;
    return static_cast<size_t>(width) * height * 2;
}

size_t NormalMapBytes(int width, int height, NormalMapFormat format)
{
    size_t bytes = 0;
    for (;;)
    {
        bytes += NormalMapLevelBytes(format, width, height);
        if (width == 1 && height == 1)
            return bytes;
        width = std::max(width / 2, 1);
        height = std::max(height / 2, 1);
    }
}

NormalMap DeriveNormalMap(std::span<const uint8_t> rgba, int width, int height, const NormalMapOptions &options)
{
    NormalMap map;
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
        return map;
    Field field{width, height, options.wrap, {}};
    field.values.resize(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < field.values.size(); ++i)
    {
        const uint8_t *texel = &rgba[i * 4];
        field.values[i] = options.source == NormalMapSource::Height
                              ? texel[0] / 255.0f
                              : (0.299f * texel[0] + 0.587f * texel[1] + 0.114f * texel[2]) / 255.0f;
    }
    int top_width, top_height;
    NormalMapSize(width, height, options, top_width, top_height);
    // Strength and band are given at kNormalMapReferenceSize: a map derived smaller (a lower preset) has the same
    // relief, only less detail.
    int reference_width, reference_height;
    NormalMapOptions reference = options;
    reference.max_size = kNormalMapReferenceSize;
    NormalMapSize(width, height, reference, reference_width, reference_height);
    const float texel_ratio = static_cast<float>(top_width) / static_cast<float>(reference_width);
    while (field.width != top_width || field.height != top_height)
        field = Halve(field);
    for (int pass = 0; pass < options.pre_blur; ++pass)
        field = Blur(field);
    if (options.source == NormalMapSource::Luminance)
        BandPass(field, std::max(options.band_sigma * texel_ratio, 1.0f));
    map.width = field.width;
    map.height = field.height;
    map.rg.reserve(NormalMapBytes(map.width, map.height));

    // Slope scale of the top mip; each smaller mip has texels twice as large, so the same world slope is
    // half as many height units per texel.
    float scale = options.strength * texel_ratio;
    for (;;)
    {
        ++map.mip_count;
        const std::vector<int> xs = field.Indices(field.width, 1), ys = field.Indices(field.height, 1);
        const float *v = field.values.data();
        const size_t w = static_cast<size_t>(field.width);
        for (int y = 0; y < field.height; ++y)
        {
            const float *above = v + ys[static_cast<size_t>(y)] * w, *row = v + ys[static_cast<size_t>(y) + 1] * w,
                        *below = v + ys[static_cast<size_t>(y) + 2] * w;
            for (int x = 0; x < field.width; ++x)
            {
                // Scharr: [-1 0 1] derivative, [3 10 3] / 16 smoothing across.
                const int l = xs[static_cast<size_t>(x)], c = xs[static_cast<size_t>(x) + 1], r = xs[static_cast<size_t>(x) + 2];
                const float du = (3.0f * (above[r] - above[l]) + 10.0f * (row[r] - row[l]) + 3.0f * (below[r] - below[l])) / 32.0f;
                const float dv = (3.0f * (below[l] - above[l]) + 10.0f * (below[c] - above[c]) + 3.0f * (below[r] - above[r])) / 32.0f;
                float nx = -du * scale, ny = -dv * scale;
                const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + 1.0f);
                nx *= inv;
                ny *= inv;
                map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(0.5f + 0.5f * nx, 0.0f, 1.0f) * 255.0f)));
                map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(0.5f + 0.5f * ny, 0.0f, 1.0f) * 255.0f)));
            }
        }
        if (field.width == 1 && field.height == 1)
            break;
        field = Halve(field);
        scale *= 0.5f;
    }
    return map;
}

void DropTopMips(NormalMap &map, int max_size)
{
    size_t offset = 0;
    while (map.mip_count > 1 && std::max(map.width, map.height) > max_size)
    {
        offset += NormalMapLevelBytes(map.format, map.width, map.height);
        map.width = std::max(map.width / 2, 1);
        map.height = std::max(map.height / 2, 1);
        --map.mip_count;
    }
    map.rg.erase(map.rg.begin(), map.rg.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::string NormalizeTexturePath(std::string_view path)
{
    std::string out;
    for (char c : path)
        if (!std::isspace(static_cast<unsigned char>(c)) || !out.empty())
            out.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (std::string_view prefix : {"resource/textures/", "textures/"})
        if (out.compare(0, prefix.size(), prefix) == 0)
            out.erase(0, prefix.size());
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

ReliefVerdict ClassifyTextureName(std::string_view path)
{
    const size_t slash = path.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    for (const Rule &rule : kRules)
    {
        if (path.compare(0, std::strlen(rule.folder), rule.folder) != 0)
            continue;
        bool hit = false;
        switch (rule.match)
        {
        case Match::Folder:
            hit = true;
            break;
        case Match::Prefix:
            hit = name.compare(0, std::strlen(rule.pattern), rule.pattern) == 0;
            break;
        case Match::Contains:
            hit = name.find(rule.pattern) != std::string_view::npos;
            break;
        }
        if (hit)
            return {rule.relief, rule.text, rule.match == Match::Folder};
    }
    return {};
}

ReliefVerdict ReliefClassifier::Classify(std::string_view path, std::span<const uint8_t> rgba)
{
    const std::string name = NormalizeTexturePath(path);
    ReliefVerdict verdict = ClassifyTextureName(name);
    // A grey "bump" texture is a height map, except in the folders without relief (the sea's animated bump frames).
    if (!(verdict.by_folder && verdict.relief == ReliefClass::None) && NameSuggestsHeightMap(name) && IsGrayscale(rgba))
        verdict = {ReliefClass::HeightMap, "bump + grey", false};
    if (std::find(logged_.begin(), logged_.end(), name) == logged_.end())
    {
        logged_.push_back(name);
        if (verdict.rule)
            Log(LogLevel::Debug, "texture %s: relief %s (rule %s)", name.c_str(), ReliefClassName(verdict.relief), verdict.rule);
        else
            Log(LogLevel::Debug, "texture %s: no relief (no rule knows it)", name.c_str());
    }
    return verdict;
}

} // namespace storm::fx
