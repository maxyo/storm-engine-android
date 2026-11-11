// <ui-icon name="ship">: one symbol of the icon pack, rasterised at the exact pixel size
// in the element's `color`. Full-colour symbols (nation-*, c-*, or the `plain` attribute)
// keep their own fills. The accent layer of a symbol, when it has one, is drawn on top.
//
// `icon-shadow: <colour> <x> <y> <sigma>` bakes a drop shadow into the raster (the same
// shadow as `filter: drop-shadow(...)` on the icon: Gaussian blur of the glyph's alpha,
// offset, under the glyph). The filter costs an offscreen layer, blur passes and a
// composite on every frame the icon is drawn uncached (a moved or changed icon); the baked
// one is part of the cached icon texture.
#include "elements.h"

#include <cmath>
#include <cstdlib>

#include <lunasvg.h>
#include <memory>
#include <unordered_map>

namespace storm::ui
{

namespace
{

struct Symbol
{
    std::string svg;
    std::vector<std::string> deps;
    bool accent = false;
};

struct IconPack
{
    std::string defs;
    std::unordered_map<std::string, Symbol> symbols;
};

IconPack &Pack()
{
    static IconPack pack;
    return pack;
}

struct Shadow
{
    Rml::Colourb colour{0, 0, 0, 0};
    float x = 0, y = 0, sigma = 0; // pixels
    float fade = 1.0f; // the element's opacity, applied to the glyph raster before the shadow is cast
    bool On() const
    {
        return colour.alpha > 0;
    }
    int Margin() const // pixels the shadow reaches past the glyph square
    {
        return On() ? static_cast<int>(std::ceil(std::max(std::fabs(x), std::fabs(y)) + 3.0f * sigma)) + 1 : 0;
    }
};

struct ShadowProperties
{
    Rml::PropertyId colour, x, y, sigma;
} shadow_ids;

std::unordered_map<std::string, std::unique_ptr<Rml::CallbackTextureSource>> &Cache()
{
    static std::unordered_map<std::string, std::unique_ptr<Rml::CallbackTextureSource>> cache;
    return cache;
}

void Collect(const std::string &id, std::vector<std::string> &order)
{
    for (const std::string &seen : order)
        if (seen == id)
            return;
    const auto found = Pack().symbols.find(id);
    if (found == Pack().symbols.end())
        return;
    order.push_back(id);
    for (const std::string &dep : found->second.deps)
        Collect(dep, order);
}

bool Rasterise(const std::string &id, bool plain, Rml::Colourb colour, int pixels, std::vector<Rml::byte> &out)
{
    const auto found = Pack().symbols.find(id);
    if (found == Pack().symbols.end())
        return false;
    std::vector<std::string> order;
    Collect(id, order);
    const bool accent = found->second.accent && !plain;
    if (accent)
        Collect(id + "-acc", order);

    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 48 48\"><defs>";
    svg += Pack().defs;
    for (const std::string &part : order)
        svg += Pack().symbols[part].svg;
    svg += "</defs><use href=\"#" + id + "\"";
    if (!plain)
    {
        char fill[64];
        std::snprintf(fill, sizeof(fill), " fill=\"#%02x%02x%02x\" fill-opacity=\"%.3f\"", colour.red, colour.green, colour.blue, colour.alpha / 255.0f);
        svg += fill;
    }
    svg += "/>";
    if (accent)
        svg += "<use href=\"#" + id + "-acc\"/>";
    svg += "</svg>";

    const auto document = lunasvg::Document::loadFromData(svg);
    if (!document)
        return false;
    const lunasvg::Bitmap bitmap = document->renderToBitmap(pixels, pixels);
    if (bitmap.isNull())
        return false;
    out.resize(static_cast<size_t>(pixels) * static_cast<size_t>(pixels) * 4);
    for (int y = 0; y < pixels; ++y)
    {
        // ARGB32 premultiplied, native byte order.
        const auto *row = reinterpret_cast<const uint32_t *>(bitmap.data() + static_cast<size_t>(y) * static_cast<size_t>(bitmap.stride()));
        Rml::byte *target = &out[static_cast<size_t>(y) * static_cast<size_t>(pixels) * 4];
        for (int x = 0; x < pixels; ++x)
        {
            const uint32_t argb = row[x];
            target[x * 4 + 0] = static_cast<Rml::byte>(argb >> 16);
            target[x * 4 + 1] = static_cast<Rml::byte>(argb >> 8);
            target[x * 4 + 2] = static_cast<Rml::byte>(argb);
            target[x * 4 + 3] = static_cast<Rml::byte>(argb >> 24);
        }
    }
    return true;
}

// Puts `shadow` under the premultiplied glyph raster (side `pixels`): returns a raster of
// side pixels + 2 * margin with the glyph in the middle.
std::vector<Rml::byte> AddShadow(const std::vector<Rml::byte> &glyph, int pixels, const Shadow &shadow)
{
    const int margin = shadow.Margin(), side = pixels + 2 * margin;
    // The glyph's alpha on the larger canvas, blurred (separable Gaussian, as RmlUi's filter).
    std::vector<float> alpha(static_cast<size_t>(side) * side, 0.0f), scratch(alpha.size(), 0.0f);
    for (int y = 0; y < pixels; ++y)
        for (int x = 0; x < pixels; ++x)
            alpha[static_cast<size_t>(y + margin) * side + x + margin] = shadow.fade * glyph[(static_cast<size_t>(y) * pixels + x) * 4 + 3] / 255.0f;
    if (shadow.sigma > 0.0f)
    {
        const int radius = static_cast<int>(std::ceil(3.0f * shadow.sigma));
        std::vector<float> kernel(static_cast<size_t>(2 * radius + 1));
        float sum = 0.0f;
        for (int i = -radius; i <= radius; ++i)
            sum += kernel[static_cast<size_t>(i + radius)] = std::exp(-0.5f * static_cast<float>(i * i) / (shadow.sigma * shadow.sigma));
        for (float &k : kernel)
            k /= sum;
        const auto pass = [&](const std::vector<float> &from, std::vector<float> &to, int dx, int dy) {
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x)
                {
                    float value = 0.0f;
                    for (int i = -radius; i <= radius; ++i)
                    {
                        const int sx = x + i * dx, sy = y + i * dy;
                        if (sx >= 0 && sy >= 0 && sx < side && sy < side)
                            value += kernel[static_cast<size_t>(i + radius)] * from[static_cast<size_t>(sy) * side + sx];
                    }
                    to[static_cast<size_t>(y) * side + x] = value;
                }
        };
        pass(alpha, scratch, 1, 0);
        pass(scratch, alpha, 0, 1);
    }
    // Shadow alpha at a fractional offset (bilinear, as the filter samples its layer).
    const auto sample = [&](float fx, float fy) {
        const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
        const auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < side && y < side ? alpha[static_cast<size_t>(y) * side + x] : 0.0f; };
        return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
    };
    const Rml::ColourbPremultiplied colour = shadow.colour.ToPremultiplied();
    std::vector<Rml::byte> out(static_cast<size_t>(side) * side * 4, 0);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
        {
            const float a = sample(static_cast<float>(x) - shadow.x, static_cast<float>(y) - shadow.y);
            float front[4] = {0, 0, 0, 0};
            const int gx = x - margin, gy = y - margin;
            if (gx >= 0 && gy >= 0 && gx < pixels && gy < pixels)
                for (int c = 0; c < 4; ++c)
                    front[c] = shadow.fade * glyph[(static_cast<size_t>(gy) * pixels + gx) * 4 + c];
            const float behind[4] = {colour.red * a, colour.green * a, colour.blue * a, colour.alpha * a};
            Rml::byte *pixel = &out[(static_cast<size_t>(y) * side + x) * 4];
            for (int c = 0; c < 4; ++c) // premultiplied source-over: glyph over shadow
                pixel[c] = static_cast<Rml::byte>(std::lround(std::min(255.0f, front[c] + behind[c] * (1.0f - front[3] / 255.0f))));
        }
    return out;
}

// Premultiplied RGBA of the "missing icon" marker: a red frame with a cross.
std::vector<Rml::byte> MissingIconMarker(int pixels)
{
    std::vector<Rml::byte> data(static_cast<size_t>(pixels) * pixels * 4, 0);
    const int line = std::max(1, pixels / 12);
    for (int y = 0; y < pixels; ++y)
        for (int x = 0; x < pixels; ++x)
        {
            const bool frame = x < line || y < line || x >= pixels - line || y >= pixels - line;
            const bool cross = std::abs(x - y) < line || std::abs(x + y - (pixels - 1)) < line;
            if (!frame && !cross)
                continue;
            Rml::byte *pixel = &data[(static_cast<size_t>(y) * pixels + x) * 4];
            pixel[0] = 0xc4, pixel[1] = 0x47, pixel[2] = 0x3a, pixel[3] = 0xff; // dangerFill
        }
    return data;
}

Rml::CallbackTextureSource &Source(const std::string &id, bool plain, Rml::Colourb colour, int pixels, const Shadow &shadow)
{
    char suffix[128];
    std::snprintf(suffix, sizeof(suffix), "|%d|%02x%02x%02x%02x|%d", plain ? 1 : 0, colour.red, colour.green, colour.blue, colour.alpha, pixels);
    std::string key = id + suffix;
    if (shadow.On())
    {
        std::snprintf(suffix, sizeof(suffix), "|s%02x%02x%02x%02x|%.2f|%.2f|%.2f|%.3f", shadow.colour.red, shadow.colour.green, shadow.colour.blue,
                      shadow.colour.alpha, shadow.x, shadow.y, shadow.sigma, shadow.fade);
        key += suffix;
    }
    auto &entry = Cache()[key];
    if (!entry)
    {
        entry = std::make_unique<Rml::CallbackTextureSource>([id, plain, colour, pixels, shadow](const Rml::CallbackTextureInterface &texture) {
            std::vector<Rml::byte> data;
            if (!Rasterise(id, plain, colour, pixels, data))
            {
                // No silent blank: a missing or broken symbol is logged and drawn as a
                // crossed red box so it is seen on the screen.
                Rml::Log::Message(Rml::Log::LT_ERROR, "Icon '%s' is not in the icon pack (or does not rasterise)", id.c_str());
                data = MissingIconMarker(pixels);
            }
            const int side = pixels + 2 * shadow.Margin();
            if (shadow.On())
                data = AddShadow(data, pixels, shadow);
            return texture.GenerateTexture({data.data(), data.size()}, {side, side});
        });
    }
    return *entry;
}

class IconElement final : public Rml::Element
{
  public:
    explicit IconElement(const Rml::String &tag) : Rml::Element(tag)
    {
    }

  protected:
    bool GetIntrinsicDimensions(Rml::Vector2f &dimensions, float &ratio) override
    {
        float dp = 1.0f;
        if (Rml::Context *context = GetContext())
            dp = context->GetDensityIndependentPixelRatio();
        const float size = GetAttribute<float>("size", 24.0f) * dp;
        dimensions = {size, size};
        ratio = 1.0f;
        return true;
    }

    void OnAttributeChange(const Rml::ElementAttributes &changed) override
    {
        Rml::Element::OnAttributeChange(changed);
        if (changed.count("size"))
            DirtyLayout();
    }

    void OnRender() override
    {
        Rml::RenderManager *manager = GetRenderManager();
        const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
        if (!manager || size.x < 1.0f || size.y < 1.0f)
            return;
        std::string name = GetAttribute<Rml::String>("name", "");
        if (name.empty())
            return;
        bool plain = HasAttribute("plain");
        const std::string id = IconSymbolId(name, plain);
        const Rml::Colourb colour = GetComputedValues().color();
        float opacity = GetComputedValues().opacity();
        const int pixels = std::max(1, static_cast<int>(std::lround(std::min(size.x, size.y))));
        Shadow shadow;
        if (const Rml::Property *shadow_colour = GetProperty(shadow_ids.colour))
            shadow.colour = shadow_colour->Get<Rml::Colourb>();
        if (shadow.On())
        {
            shadow.x = ResolveLength(GetProperty(shadow_ids.x)->GetNumericValue());
            shadow.y = ResolveLength(GetProperty(shadow_ids.y)->GetNumericValue());
            shadow.sigma = std::max(0.0f, ResolveLength(GetProperty(shadow_ids.sigma)->GetNumericValue()));
        }
        if (shadow.On())
        {
            // As under the filter: the faded glyph casts a faded shadow and shows it through
            // itself, so the opacity goes into the raster, not onto the finished quad.
            shadow.fade = opacity;
            opacity = 1.0f;
        }
        const int margin = shadow.Margin();

        if (size != built_size_ || opacity != built_opacity_ || margin != built_margin_)
        {
            Rml::Mesh mesh = geometry_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            const float extent = static_cast<float>(pixels + 2 * margin);
            const Rml::Vector2f side = {extent, extent};
            const Rml::Vector2f origin = {std::round((size.x - static_cast<float>(pixels)) * 0.5f) - static_cast<float>(margin),
                                          std::round((size.y - static_cast<float>(pixels)) * 0.5f) - static_cast<float>(margin)};
            Rml::MeshUtilities::GenerateQuad(mesh, origin, side, Rml::Colourb(255, 255, 255, 255).ToPremultiplied(opacity), {0, 0}, {1, 1});
            geometry_ = manager->MakeGeometry(std::move(mesh));
            built_size_ = size;
            built_opacity_ = opacity;
            built_margin_ = margin;
        }
        const Rml::Vector2f offset = GetAbsoluteOffset(Rml::BoxArea::Content);
        geometry_.Render({std::round(offset.x), std::round(offset.y)}, Source(id, plain, colour, pixels, shadow).GetTexture(*manager));
    }

  private:
    Rml::Geometry geometry_;
    Rml::Vector2f built_size_;
    float built_opacity_ = -1.0f;
    int built_margin_ = -1;
};

} // namespace

std::string IconSymbolId(std::string name, bool &plain)
{
    if (name.rfind("i-", 0) == 0)
        name = name.substr(2);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, "-acc") == 0)
        name.resize(name.size() - 4); // the accent layer is never drawn alone
    const bool colour_art = name.rfind("c-", 0) == 0;
    plain = plain || colour_art || name.rfind("nation-", 0) == 0;
    return colour_art ? name : "i-" + name;
}

bool HasIcon(const std::string &name)
{
    bool plain = false;
    return Pack().symbols.count(IconSymbolId(name, plain)) > 0;
}

void LoadIconPack(const Json &pack)
{
    IconPack &target = Pack();
    target.defs = pack.value("defs", std::string());
    target.symbols.clear();
    for (const auto &item : pack.at("symbols").items())
    {
        Symbol symbol;
        symbol.svg = item.value().at("svg").get<std::string>();
        symbol.accent = item.value().value("acc", false);
        const Json deps = item.value().value("deps", Json::array());
        for (const Json &dep : deps)
            symbol.deps.push_back(dep.get<std::string>());
        target.symbols[item.key()] = std::move(symbol);
    }
    Cache().clear();
}

void ReleaseIconTextures()
{
    Cache().clear();
}

void RegisterIconElement()
{
    using Rml::StyleSheetSpecification;
    shadow_ids.colour = StyleSheetSpecification::RegisterProperty("icon-shadow-color", "transparent", false, false).AddParser("color").GetId();
    shadow_ids.x = StyleSheetSpecification::RegisterProperty("icon-shadow-x", "0px", false, false).AddParser("length").GetId();
    shadow_ids.y = StyleSheetSpecification::RegisterProperty("icon-shadow-y", "0px", false, false).AddParser("length").GetId();
    shadow_ids.sigma = StyleSheetSpecification::RegisterProperty("icon-shadow-sigma", "0px", false, false).AddParser("length").GetId();
    StyleSheetSpecification::RegisterShorthand("icon-shadow", "icon-shadow-color, icon-shadow-x, icon-shadow-y, icon-shadow-sigma",
                                               Rml::ShorthandType::FallThrough);
    static Rml::ElementInstancerGeneric<IconElement> instancer;
    Rml::Factory::RegisterElementInstancer("ui-icon", &instancer);
}

} // namespace storm::ui
