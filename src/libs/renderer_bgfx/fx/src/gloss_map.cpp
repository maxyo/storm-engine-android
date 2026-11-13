#include "storm/fx/gloss_map.hpp"
#include "storm/fx/upscale.hpp"

#include "image_field.hpp"
#include "name_rules.hpp"
#include "storm/fx/relief_model.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
using namespace img;
using rules::Match;

const char *const kClassNames[] = {"none",     "matte",    "dry wood", "painted wood", "lacquered wood", "stone",
                                   "wet stone", "building", "interior", "tile",     "leather",      "metal",          "glass"};

struct Rule
{
    const char *folder;
    Match match;
    const char *pattern;
    GlossClass gloss;
    const char *text;
};

constexpr GlossClass N = GlossClass::None, M = GlossClass::Matte, D = GlossClass::DryWood, H = GlossClass::PaintedWood,
                     L = GlossClass::Lacquered, S = GlossClass::Stone, Q = GlossClass::WetStone, B = GlossClass::Building,
                     I = GlossClass::Interior,
                     T = GlossClass::Tile, K = GlossClass::Leather, X = GlossClass::Metal, G = GlossClass::Glass;
#define STORM_GLOSS(folder, match, pattern, gloss) {folder, Match::match, pattern, gloss, folder pattern}
// TEHO's RESOURCE/Textures, first match wins (the relief table's folders and names, normal_map.cpp, with the glossy
// materials the relief leaves out). A texture no rule names gets no gloss map.
const Rule kRules[] = {
    // Grey "bump" textures are heights (the relief's), not colour.
    STORM_GLOSS("", Contains, "bump", N),
    // Folders without highlights of their own: characters (matte like the original, the owner's choice), UI, sky
    // and weather, light maps, effects, foliage, flags, the islands seen from the sea.
    STORM_GLOSS("characters/", Folder, "", N),
    STORM_GLOSS("interfaces/", Folder, "", N),
    STORM_GLOSS("battle_interface/", Folder, "", N),
    STORM_GLOSS("mainmenu/", Folder, "", N),
    STORM_GLOSS("loading/", Folder, "", N),
    STORM_GLOSS("fonts/", Folder, "", N),
    STORM_GLOSS("dialog/", Folder, "", N),
    STORM_GLOSS("crosshair/", Folder, "", N),
    STORM_GLOSS("tips/", Folder, "", N),
    STORM_GLOSS("quest_signs/", Folder, "", N),
    STORM_GLOSS("weather/", Folder, "", N),
    STORM_GLOSS("lighting/", Folder, "", N),
    STORM_GLOSS("worldmap/", Folder, "", N),
    STORM_GLOSS("islands/", Folder, "", N),
    STORM_GLOSS("grass/", Folder, "", N),
    STORM_GLOSS("animals/", Folder, "", N),
    STORM_GLOSS("flags/", Folder, "", N),
    STORM_GLOSS("shipsflares/", Folder, "", N),
    STORM_GLOSS("locefx/", Folder, "", N),
    STORM_GLOSS("tornado/", Folder, "", N),
    STORM_GLOSS("texturesequence/", Folder, "", N),
    // Items and ammunition held or lying about: weapons are metal.
    STORM_GLOSS("ammo/", Folder, "", X),
    STORM_GLOSS("items/", Folder, "", N),
    // Ships: sails and ropes matte, wakes and alpha-tested rigging none, metal fittings, the rest painted planks
    // (hulls) or decks.
    STORM_GLOSS("ships/", Contains, "parus", M),
    STORM_GLOSS("ships/", Contains, "sail", M),
    STORM_GLOSS("ships/", Contains, "trail", N),
    STORM_GLOSS("ships/", Contains, "alpha", N),
    STORM_GLOSS("ships/", Contains, "col", N),
    STORM_GLOSS("ships/", Contains, "rope", M),
    STORM_GLOSS("ships/", Contains, "glass", G),
    STORM_GLOSS("ships/", Contains, "deck", D),
    STORM_GLOSS("ships/", Contains, "metal", X),
    STORM_GLOSS("ships/", Contains, "gun", X),
    STORM_GLOSS("ships/", Contains, "cannon", X),
    STORM_GLOSS("ships/", Folder, "", H),

    // Things without highlights (before the names that would match them): effects, light, water, foliage, sky.
    STORM_GLOSS("", Contains, "windmill", B),
    STORM_GLOSS("", Prefix, "occl", N),
    STORM_GLOSS("", Prefix, "refl", N),
    STORM_GLOSS("", Contains, "mask", N),
    STORM_GLOSS("", Contains, "cannonflash", N),
    STORM_GLOSS("", Contains, "alpha", N),
    STORM_GLOSS("", Contains, "alfa", N),
    STORM_GLOSS("", Contains, "corona", N),
    STORM_GLOSS("", Contains, "fire", N),
    STORM_GLOSS("", Contains, "flash", N),
    STORM_GLOSS("", Contains, "spark", N),
    STORM_GLOSS("", Contains, "smoke", N),
    STORM_GLOSS("", Contains, "shadow", N),
    STORM_GLOSS("", Contains, "sky", N),
    STORM_GLOSS("", Prefix, "sea", N),
    STORM_GLOSS("", Contains, "water", N),
    STORM_GLOSS("", Contains, "under", N),
    STORM_GLOSS("", Contains, "caust", N),
    STORM_GLOSS("", Contains, "foam", N),
    STORM_GLOSS("", Contains, "splash", N),
    STORM_GLOSS("", Contains, "fall", N),
    STORM_GLOSS("", Contains, "river", N),
    STORM_GLOSS("", Contains, "lava", N),
    STORM_GLOSS("", Contains, "leaf", N),
    STORM_GLOSS("", Contains, "leaves", N),
    STORM_GLOSS("", Contains, "palm", N),
    STORM_GLOSS("", Contains, "tree", N),
    STORM_GLOSS("", Contains, "plant", N),
    STORM_GLOSS("", Contains, "flower", N),
    STORM_GLOSS("", Contains, "flora", N),
    STORM_GLOSS("", Contains, "fern", N),
    STORM_GLOSS("", Contains, "back", N), // background cards
    STORM_GLOSS("", Contains, "blood", N),
    STORM_GLOSS("", Contains, "bird", N),
    STORM_GLOSS("", Contains, "seagull", N),
    STORM_GLOSS("", Contains, "fish", N),
    STORM_GLOSS("", Contains, "map", N),
    STORM_GLOSS("", Contains, "menu", N),
    STORM_GLOSS("", Contains, "particles", N),
    STORM_GLOSS("", Contains, "painting", N),
    STORM_GLOSS("", Contains, "freska", N),
    STORM_GLOSS("", Prefix, "eye", N),
    STORM_GLOSS("", Contains, "skin", N),
    STORM_GLOSS("", Contains, "telo", N),   // body
    STORM_GLOSS("", Contains, "golova", N), // head

    // Glass: windows (the night-lit "wind..." window atlases too) and bottles.
    STORM_GLOSS("", Prefix, "wind", G),
    STORM_GLOSS("", Contains, "glass", G),
    STORM_GLOSS("", Contains, "window", G),
    STORM_GLOSS("", Contains, "steklo", G),
    STORM_GLOSS("", Contains, "bottle", G),
    STORM_GLOSS("", Contains, "butyl", G),
    // Metal.
    STORM_GLOSS("", Contains, "metal", X),
    STORM_GLOSS("", Contains, "iron", X),
    STORM_GLOSS("", Contains, "cannon", X),
    STORM_GLOSS("", Contains, "gundeck", D), // the gun deck's planks
    STORM_GLOSS("", Contains, "gun", X),
    STORM_GLOSS("", Contains, "pistol", X),
    STORM_GLOSS("", Contains, "blade", X),
    STORM_GLOSS("", Contains, "sword", X),
    STORM_GLOSS("", Contains, "chain", X),
    STORM_GLOSS("", Contains, "anchor", X),
    STORM_GLOSS("", Contains, "yakor", X), // anchor
    STORM_GLOSS("", Contains, "bell", X),
    STORM_GLOSS("", Contains, "kolokol", X), // bell
    STORM_GLOSS("", Contains, "gold", X),
    STORM_GLOSS("", Contains, "treasure", X),
    STORM_GLOSS("", Contains, "copper", X),
    STORM_GLOSS("", Contains, "bronze", X),
    STORM_GLOSS("", Contains, "brass", X),
    STORM_GLOSS("", Contains, "fonar", X), // lanterns
    STORM_GLOSS("", Contains, "lamp", X),
    STORM_GLOSS("", Contains, "candle", X), // candlesticks
    STORM_GLOSS("", Contains, "lantern", X),
    // Leather.
    STORM_GLOSS("", Contains, "leather", K),
    STORM_GLOSS("", Contains, "kozh", K),
    STORM_GLOSS("", Prefix, "bag", K),
    STORM_GLOSS("", Contains, "saddle", K),
    STORM_GLOSS("", Contains, "book", K),
    // Cloth and rope.
    STORM_GLOSS("", Contains, "cloth", M),
    STORM_GLOSS("", Contains, "carpet", M),
    STORM_GLOSS("", Contains, "fabric", M),
    STORM_GLOSS("", Contains, "sail", M),
    STORM_GLOSS("", Contains, "flag", N),
    STORM_GLOSS("", Contains, "rope", M),
    STORM_GLOSS("", Contains, "nets", M),
    STORM_GLOSS("", Contains, "web", N),
    STORM_GLOSS("", Contains, "canopy", M),
    STORM_GLOSS("", Contains, "vant", M), // shrouds
    // Wet stone: caves, grottos, crypts, dungeons, wells.
    STORM_GLOSS("", Contains, "cave", Q),
    STORM_GLOSS("", Contains, "grot", Q),
    STORM_GLOSS("", Contains, "crypt", Q),
    STORM_GLOSS("", Contains, "dungeon", Q),
    STORM_GLOSS("", Contains, "prison", Q),
    STORM_GLOSS("", Contains, "stal", Q), // stalactites
    STORM_GLOSS("", Contains, "mine", Q),
    STORM_GLOSS("", Contains, "well", Q),
    // Glazed tiles and marble.
    STORM_GLOSS("", Contains, "tile", T),
    STORM_GLOSS("", Contains, "marble", T),
    STORM_GLOSS("", Contains, "mramor", T),
    // Lacquered wood: furniture, cabins, chests, doors.
    STORM_GLOSS("", Contains, "furniture", L),
    STORM_GLOSS("", Contains, "mebel", L),
    STORM_GLOSS("", Contains, "chest", L),
    STORM_GLOSS("", Contains, "cabin", L),
    STORM_GLOSS("", Prefix, "cap", L), // captain's cabins
    STORM_GLOSS("", Contains, "door", L),
    STORM_GLOSS("", Contains, "table", L),
    STORM_GLOSS("", Contains, "shkaf", L), // cupboard
    // Stone (before the wood: "063pierres" holds "pier").
    STORM_GLOSS("", Contains, "stone", S),
    STORM_GLOSS("", Contains, "brick", S),
    STORM_GLOSS("", Contains, "rock", S),
    STORM_GLOSS("", Contains, "skala", S),
    STORM_GLOSS("", Contains, "pierre", S),
    STORM_GLOSS("", Contains, "cobble", S),
    STORM_GLOSS("", Contains, "beton", S),
    STORM_GLOSS("", Contains, "temple", S),
    STORM_GLOSS("", Contains, "pyramid", S),
    STORM_GLOSS("", Contains, "piramid", S),
    STORM_GLOSS("", Contains, "incas", S),
    STORM_GLOSS("", Contains, "border", S),
    STORM_GLOSS("", Contains, "stair", S),
    STORM_GLOSS("", Contains, "steps", S),
    STORM_GLOSS("", Contains, "arch", S),
    STORM_GLOSS("", Contains, "ruin", S),
    STORM_GLOSS("", Contains, "grave", S),
    STORM_GLOSS("", Contains, "cemetery", S),
    STORM_GLOSS("", Contains, "fontain", S),
    STORM_GLOSS("", Contains, "mayak", S),
    STORM_GLOSS("", Contains, "kamin", S),
    // Dry wood.
    STORM_GLOSS("", Contains, "wood", D),
    STORM_GLOSS("", Contains, "plank", D),
    STORM_GLOSS("", Contains, "deck", D),
    STORM_GLOSS("", Contains, "board", D),
    STORM_GLOSS("", Contains, "balk", D),
    STORM_GLOSS("", Contains, "derevo", D),
    STORM_GLOSS("", Contains, "gate", D),
    STORM_GLOSS("", Contains, "pier", D),
    STORM_GLOSS("", Contains, "prichal", D),
    STORM_GLOSS("", Contains, "hold", D),
    STORM_GLOSS("", Contains, "boat", D),
    STORM_GLOSS("", Prefix, "dm_", D),
    STORM_GLOSS("", Contains, "barrel", D),
    STORM_GLOSS("", Contains, "ladder", D),
    STORM_GLOSS("", Contains, "grating", D),
    STORM_GLOSS("", Contains, "lafet", D), // gun carriages
    STORM_GLOSS("", Contains, "roof", D),
    STORM_GLOSS("", Contains, "hut", D),
    STORM_GLOSS("", Contains, "barn", D),
    STORM_GLOSS("", Contains, "objects_tex", D),
    // Interiors: the atlases of a room ("tavern1ins", "store3_inside", "tavern_room_tex").
    STORM_GLOSS("", Contains, "ins", I),
    STORM_GLOSS("", Contains, "room", I),
    // Buildings: one atlas per house (the texel cues tell its materials apart).
    STORM_GLOSS("", Prefix, "flh", B),
    STORM_GLOSS("", Prefix, "sph", B),
    STORM_GLOSS("", Contains, "shipyard", B),
    STORM_GLOSS("", Contains, "house", B),
    STORM_GLOSS("", Contains, "residence", B),
    STORM_GLOSS("", Contains, "tavern", B),
    STORM_GLOSS("", Contains, "store", B),
    STORM_GLOSS("", Contains, "bank", B),
    STORM_GLOSS("", Contains, "church", B),
    STORM_GLOSS("", Contains, "barracks", B),
    STORM_GLOSS("", Contains, "brothel", B),
    STORM_GLOSS("", Contains, "usurer", B),
    STORM_GLOSS("", Contains, "estate", B),
    STORM_GLOSS("", Contains, "arsenal", B),
    STORM_GLOSS("", Contains, "college", B),
    STORM_GLOSS("", Contains, "office", B),
    STORM_GLOSS("", Contains, "home", B),
    STORM_GLOSS("", Contains, "village", B),
    STORM_GLOSS("", Contains, "quarter", B),
    STORM_GLOSS("", Contains, "town", B),
    STORM_GLOSS("", Contains, "lighthouse", B),
    STORM_GLOSS("", Contains, "wall", B),
    STORM_GLOSS("", Contains, "fort", B),
    STORM_GLOSS("", Contains, "castle", B),
    // Ground, sand and plaster.
    STORM_GLOSS("", Contains, "sand", M),
    STORM_GLOSS("", Contains, "ground", M),
    STORM_GLOSS("", Contains, "grass", M),
    STORM_GLOSS("", Contains, "earth", M),
    STORM_GLOSS("", Contains, "dirt", M),
    STORM_GLOSS("", Contains, "road", M),
    STORM_GLOSS("", Contains, "plaster", M),
    STORM_GLOSS("", Contains, "shtukatur", M),
};
#undef STORM_GLOSS

// The class caps: the matte classes stay matte whatever the network reads (its smooth patches on cloth, ground and
// plaster are baked light, not polish).
float ClassCap(GlossClass gloss)
{
    switch (gloss)
    {
    case GlossClass::Matte: return 0.1f;
    case GlossClass::Stone: return 0.35f;
    default: return 1.0f;
    }
}

float Smooth(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Hue in degrees (0 red, 60 yellow, 120 green, 240 blue) of an RGB texel, 0..1 channels.
float Hue(float r, float g, float b, float max, float min)
{
    const float c = max - min;
    if (c < 1e-4f)
        return 0.0f;
    float h;
    if (max == r)
        h = std::fmod((g - b) / c + 6.0f, 6.0f);
    else if (max == g)
        h = (b - r) / c + 2.0f;
    else
        h = (r - g) / c + 4.0f;
    return h * 60.0f;
}
} // namespace

const char *GlossClassName(GlossClass gloss)
{
    const auto index = static_cast<size_t>(gloss);
    return index < std::size(kClassNames) ? kClassNames[index] : "?";
}

GlossVerdict ClassifyGlossName(std::string_view path)
{
    for (const Rule &rule : kRules)
        if (rules::Matches(path, rule.folder, rule.match, rule.pattern))
            return {rule.gloss, rule.text};
    return {};
}

uint64_t GlossMethod(GlossClass gloss)
{
    // The roughness network runs on the texture's ×2 when it is small enough (ReliefMapsFromUpscale): the upscaler's
    // digest too.
    return (((uint64_t{kGlossMethod} << 8) | static_cast<uint64_t>(gloss)) ^ kReliefRoughnessModelDigest) * 0x100000001b3ull ^
           kUpscaleModelDigest;
}

float GlossMetalTint(GlossClass gloss)
{
    return gloss == GlossClass::Metal ? 0.7f : 0.0f;
}

namespace
{
Field Plane(int width, int height)
{
    Field f{width, height, true, {}};
    f.values.resize(static_cast<size_t>(width) * height);
    return f;
}
} // namespace

std::vector<float> GlossMetalMask(std::span<const uint8_t> rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
        return {};
    Field r = Plane(width, height), g = r, b = r, lum = r;
    for (size_t i = 0; i < r.values.size(); ++i)
    {
        r.values[i] = rgba[i * 4] / 255.0f;
        g.values[i] = rgba[i * 4 + 1] / 255.0f;
        b.values[i] = rgba[i * 4 + 2] / 255.0f;
        lum.values[i] = 0.299f * r.values[i] + 0.587f * g.values[i] + 0.114f * b.values[i];
    }
    // The colour blurred a little (single noisy texels of grime must not flip between materials); the local contrast
    // over sigma 3 texels of the map (rust is grainy, polished metal flat).
    const Field cr = Gaussian(r, 1.5f), cg = Gaussian(g, 1.5f), cb = Gaussian(b, 1.5f), mean = Gaussian(lum, 3.0f);
    Field square = lum;
    for (size_t i = 0; i < square.values.size(); ++i)
        square.values[i] = (lum.values[i] - mean.values[i]) * (lum.values[i] - mean.values[i]);
    const Field contrast = Gaussian(square, 3.0f);
    std::vector<float> mask(r.values.size());
    for (size_t i = 0; i < mask.size(); ++i)
    {
        const float hi = std::max({cr.values[i], cg.values[i], cb.values[i]}), lo = std::min({cr.values[i], cg.values[i], cb.values[i]});
        const float sat = hi > 1e-3f ? (hi - lo) / hi : 0.0f, l = lum.values[i];
        const float hue = Hue(cr.values[i], cg.values[i], cb.values[i], hi, lo);
        const float flat = 1.0f - Smooth(0.02f, 0.10f, std::sqrt(std::max(contrast.values[i], 0.0f)));
        // Rust: orange-brown, saturated, not bright, grainy (brass and gold are brighter and more yellow).
        const float rust = hue > 8.0f && hue < 40.0f
                               ? Smooth(0.4f, 0.65f, sat) * (1.0f - Smooth(0.2f, 0.34f, l)) * (1.0f - 0.7f * flat)
                               : 0.0f;
        // The gun atlases hold their carriages too: brown, grainy wood, less saturated than brass.
        const float wood = hue > 12.0f && hue < 42.0f ? Smooth(0.25f, 0.4f, sat) * (1.0f - Smooth(0.6f, 0.75f, sat)) *
                                                            (1.0f - Smooth(0.4f, 0.55f, l)) * (1.0f - 0.3f * flat)
                                                      : 0.0f;
        mask[i] = std::clamp(1.0f - rust - wood, 0.0f, 1.0f);
    }
    return mask;
}

GlossField GlossOfRoughness(std::span<const float> roughness, std::span<const float> height, std::span<const uint8_t> rgba,
                            int width, int height_texels, GlossClass gloss)
{
    GlossField field;
    const int w = width, h = height_texels;
    const size_t texels = static_cast<size_t>(std::max(w, 0)) * std::max(h, 0);
    if (gloss == GlossClass::None || w <= 0 || h <= 0 || roughness.size() != texels || height.size() != texels ||
        rgba.size() < texels * 4)
        return field;
    // The filters' sigmas are in texels of a 512 map (where they were tuned): halved at 256.
    const float scale = static_cast<float>(std::max(w, h)) / 512.0f;
    // The grooves: texels below their surroundings in the height, by how many of the texture's own spreads.
    Field hf = Plane(w, h);
    std::copy(height.begin(), height.end(), hf.values.begin());
    const Field around = Gaussian(hf, 4.0f * scale);
    double sum = 0.0;
    for (size_t i = 0; i < texels; ++i)
        sum += (hf.values[i] - around.values[i]) * (hf.values[i] - around.values[i]);
    const float spread = std::max(static_cast<float>(std::sqrt(sum / static_cast<double>(texels))), 1e-6f);
    Field lum = Plane(w, h);
    for (size_t i = 0; i < texels; ++i)
        lum.values[i] = (0.299f * rgba[i * 4] + 0.587f * rgba[i * 4 + 1] + 0.114f * rgba[i * 4 + 2]) / 255.0f;
    lum = Gaussian(lum, 1.5f * scale);
    const bool metal = gloss == GlossClass::Metal;
    if (metal)
        field.metal = GlossMetalMask(rgba, w, h);
    const float cap = ClassCap(gloss);
    field.width = w;
    field.height = h;
    field.gloss.resize(texels);
    for (size_t i = 0; i < texels; ++i)
    {
        // GGX's perceptual roughness r (alpha r^2) as the Blinn-Phong power that matches it, on the highlight's scale.
        const float alpha = std::clamp(roughness[i], 0.05f, 1.0f) * std::clamp(roughness[i], 0.05f, 1.0f);
        const float power = std::max(2.0f / (alpha * alpha) - 2.0f, 1.0f);
        float g = std::clamp((std::log2(power) - 3.0f) / 4.5f, 0.0f, 1.0f);
        const float t = std::clamp(((hf.values[i] - around.values[i]) / spread + 1.5f) / 1.5f, 0.0f, 1.0f);
        g *= 0.25f + 0.75f * t * t * (3.0f - 2.0f * t);
        if (metal)
            g = std::max(g, 0.5f * field.metal[i]);
        else
            g *= Smooth(0.06f, 0.25f, lum.values[i]);
        field.gloss[i] = std::min(g, cap);
    }
    return field;
}

NormalMap GlossMaterial(const GlossField &field)
{
    NormalMap map;
    if (field.width <= 0 || field.height <= 0 || field.gloss.size() != static_cast<size_t>(field.width) * field.height)
        return map;
    Field g{field.width, field.height, true, field.gloss}, m = g;
    m.values = field.metal.empty() ? std::vector<float>(g.values.size(), 0.0f) : field.metal;
    map.width = field.width;
    map.height = field.height;
    map.format = NormalMapFormat::Rg8;
    map.rg.reserve(NormalMapBytes(map.width, map.height, NormalMapFormat::Rg8));
    for (;;)
    {
        ++map.mip_count;
        for (size_t i = 0; i < g.values.size(); ++i)
        {
            map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(m.values[i], 0.0f, 1.0f) * 255.0f)));
            map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(g.values[i], 0.0f, 1.0f) * 255.0f)));
        }
        if (g.width == 1 && g.height == 1)
            break;
        g = Halve(g);
        m = Halve(m);
    }
    return map;
}

} // namespace storm::fx
