// <ui-icon name="ship">: one symbol of the icon pack, rasterised at the exact pixel size
// in the element's `color`. Full-colour symbols (nation-*, c-*, or the `plain` attribute)
// keep their own fills. The accent layer of a symbol, when it has one, is drawn on top.
#include "elements.h"

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

Rml::CallbackTextureSource &Source(const std::string &id, bool plain, Rml::Colourb colour, int pixels)
{
    char suffix[48];
    std::snprintf(suffix, sizeof(suffix), "|%d|%02x%02x%02x%02x|%d", plain ? 1 : 0, colour.red, colour.green, colour.blue, colour.alpha, pixels);
    auto &entry = Cache()[id + suffix];
    if (!entry)
    {
        entry = std::make_unique<Rml::CallbackTextureSource>([id, plain, colour, pixels](const Rml::CallbackTextureInterface &texture) {
            std::vector<Rml::byte> data;
            if (!Rasterise(id, plain, colour, pixels, data))
            {
                // No silent blank: a missing or broken symbol is logged and drawn as a
                // crossed red box so it is seen on the screen.
                Rml::Log::Message(Rml::Log::LT_ERROR, "Icon '%s' is not in the icon pack (or does not rasterise)", id.c_str());
                data = MissingIconMarker(pixels);
            }
            return texture.GenerateTexture({data.data(), data.size()}, {pixels, pixels});
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
        const float opacity = GetComputedValues().opacity();
        const int pixels = std::max(1, static_cast<int>(std::lround(std::min(size.x, size.y))));

        if (size != built_size_ || opacity != built_opacity_)
        {
            Rml::Mesh mesh = geometry_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            const Rml::Vector2f side = {static_cast<float>(pixels), static_cast<float>(pixels)};
            const Rml::Vector2f origin = {std::round((size.x - side.x) * 0.5f), std::round((size.y - side.y) * 0.5f)};
            Rml::MeshUtilities::GenerateQuad(mesh, origin, side, Rml::Colourb(255, 255, 255, 255).ToPremultiplied(opacity), {0, 0}, {1, 1});
            geometry_ = manager->MakeGeometry(std::move(mesh));
            built_size_ = size;
            built_opacity_ = opacity;
        }
        const Rml::Vector2f offset = GetAbsoluteOffset(Rml::BoxArea::Content);
        geometry_.Render({std::round(offset.x), std::round(offset.y)}, Source(id, plain, colour, pixels).GetTexture(*manager));
    }

  private:
    Rml::Geometry geometry_;
    Rml::Vector2f built_size_;
    float built_opacity_ = -1.0f;
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
    static Rml::ElementInstancerGeneric<IconElement> instancer;
    Rml::Factory::RegisterElementInstancer("ui-icon", &instancer);
}

} // namespace storm::ui
