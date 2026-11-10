// <ui-ring>: hold-to-confirm progress frame (rectangle or circle).
// <ui-paper>: aged parchment sheet with a torn deckle edge, fibres, foxing and a fold.
// <ui-clamp>: text cut to a number of lines with an ellipsis.
// <ui-pages>: a long text shown a page at a time, before a reserved part (dialogue replies).
// <ui-wheel>: a disc cut into sectors around a centre disc (the sword-move wheel).
// Colours come from RCSS properties registered here, so the tokens stay the only source.
#include "elements.h"
#include "text_pages.h"

#include <RmlUi/Core/ElementUtilities.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace storm::ui
{

namespace
{

constexpr float kPi = 3.14159265358979f;

Rml::PropertyId ring_fill, ring_fill_end, ring_track, paper_top, paper_bottom, paper_edge, paper_line, paper_shadow, paper_fibre;
Rml::PropertyId wheel_fill, wheel_selected, wheel_line, wheel_edge, wheel_centre;

Rml::Colourb ColourOf(Rml::Element &element, Rml::PropertyId id)
{
    const Rml::Property *property = element.GetProperty(id);
    return property ? property->Get<Rml::Colourb>() : Rml::Colourb(0, 0, 0, 0);
}

Rml::ColourbPremultiplied Mix(Rml::Colourb a, Rml::Colourb b, float t, float opacity = 1.0f)
{
    const auto lerp = [t](Rml::byte x, Rml::byte y) { return static_cast<Rml::byte>(std::lround(x + (y - x) * t)); };
    return Rml::Colourb(lerp(a.red, b.red), lerp(a.green, b.green), lerp(a.blue, b.blue), lerp(a.alpha, b.alpha)).ToPremultiplied(opacity);
}

// `over` painted on top of the opaque `base`.
Rml::Colourb Over(Rml::Colourb base, Rml::Colourb over, float strength = 1.0f)
{
    const float a = over.alpha / 255.0f * strength;
    const auto mix = [a](Rml::byte x, Rml::byte y) { return static_cast<Rml::byte>(std::lround(x + (y - x) * a)); };
    return Rml::Colourb(mix(base.red, over.red), mix(base.green, over.green), mix(base.blue, over.blue), base.alpha);
}

int Push(Rml::Mesh &mesh, Rml::Vector2f position, Rml::ColourbPremultiplied colour, Rml::Vector2f uv = {0, 0})
{
    mesh.vertices.push_back(Rml::Vertex{position, colour, uv});
    return static_cast<int>(mesh.vertices.size()) - 1;
}

void Triangle(Rml::Mesh &mesh, int a, int b, int c)
{
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

void Quad(Rml::Mesh &mesh, int a, int b, int c, int d)
{
    Triangle(mesh, a, b, c);
    Triangle(mesh, a, c, d);
}

class DrawElement : public Rml::Element
{
  public:
    explicit DrawElement(const Rml::String &tag) : Rml::Element(tag)
    {
    }

  protected:
    // `overlay` is drawn after `mesh`, with MeshTexture().
    virtual void Build(Rml::Mesh &mesh, Rml::Mesh &overlay, Rml::Vector2f size, float dp) = 0;
    virtual Rml::Texture MeshTexture(Rml::RenderManager &)
    {
        return {};
    }

    void OnRender() override
    {
        Rml::RenderManager *manager = GetRenderManager();
        if (!manager)
            return;
        const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Border);
        if (size.x <= 0 || size.y <= 0)
            return;
        if (dirty_ || size != built_size_)
        {
            Rml::Mesh mesh = geometry_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            Rml::Mesh overlay = overlay_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            float dp = 1.0f;
            if (Rml::Context *context = GetContext())
                dp = context->GetDensityIndependentPixelRatio();
            Build(mesh, overlay, size, dp);
            has_overlay_ = !overlay.indices.empty();
            geometry_ = manager->MakeGeometry(std::move(mesh));
            overlay_ = manager->MakeGeometry(std::move(overlay));
            built_size_ = size;
            dirty_ = false;
        }
        const Rml::Vector2f origin = GetAbsoluteOffset(Rml::BoxArea::Border);
        geometry_.Render(origin);
        if (has_overlay_)
            overlay_.Render(origin, MeshTexture(*manager));
    }

    void OnAttributeChange(const Rml::ElementAttributes &changed) override
    {
        Rml::Element::OnAttributeChange(changed);
        dirty_ = true;
    }
    void OnPropertyChange(const Rml::PropertyIdSet &changed) override
    {
        Rml::Element::OnPropertyChange(changed);
        dirty_ = true;
    }
    void OnDpRatioChange() override
    {
        Rml::Element::OnDpRatioChange();
        dirty_ = true;
    }

    bool dirty_ = true;

  private:
    Rml::Geometry geometry_;
    Rml::Geometry overlay_;
    bool has_overlay_ = false;
    Rml::Vector2f built_size_;
};

class RingElement final : public DrawElement
{
  public:
    using DrawElement::DrawElement;

  protected:
    void Build(Rml::Mesh &mesh, Rml::Mesh &, Rml::Vector2f size, float dp) override
    {
        const float value = std::clamp(GetAttribute<float>("value", 0.0f), 0.0f, 100.0f) / 100.0f;
        const float thickness = GetAttribute<float>("thickness", 3.0f) * dp;
        const bool circle = GetAttribute<Rml::String>("shape", "rect") == "circle";
        const float opacity = GetComputedValues().opacity();
        const Rml::Colourb fill = ColourOf(*this, ring_fill), fill_end = ColourOf(*this, ring_fill_end), track = ColourOf(*this, ring_track);
        const Rml::Vector2f centre = size * 0.5f;
        const Rml::Vector2f half = centre, inner = {std::max(0.0f, half.x - thickness), std::max(0.0f, half.y - thickness)};

        // Angles run clockwise from 12 o'clock, like a conic gradient.
        std::vector<float> angles;
        const int steps = circle ? 96 : 64;
        for (int i = 0; i <= steps; ++i)
            angles.push_back(2.0f * kPi * static_cast<float>(i) / static_cast<float>(steps));
        if (!circle)
        {
            const float corner = std::atan2(half.x, half.y);
            for (const float a : {corner, kPi - corner, kPi + corner, 2.0f * kPi - corner})
                angles.push_back(a);
        }
        if (value > 0.0f && value < 1.0f)
            angles.push_back(value * 2.0f * kPi);
        std::sort(angles.begin(), angles.end());

        // Outer edge point along the ray; the inner one is the same point pulled onto the
        // inner outline, so the band keeps its thickness around the corners.
        const auto outer_point = [&](float angle) {
            const float s = std::sin(angle), c = -std::cos(angle);
            if (circle)
                return centre + Rml::Vector2f(s, c) * std::min(half.x, half.y);
            const float scale = std::min(std::fabs(s) > 1e-5f ? half.x / std::fabs(s) : 1e9f, std::fabs(c) > 1e-5f ? half.y / std::fabs(c) : 1e9f);
            return centre + Rml::Vector2f(s, c) * scale;
        };
        const auto inner_point = [&](float angle) {
            if (circle)
                return centre + Rml::Vector2f(std::sin(angle), -std::cos(angle)) * std::max(0.0f, std::min(half.x, half.y) - thickness);
            const Rml::Vector2f p = outer_point(angle);
            return Rml::Vector2f(std::clamp(p.x, centre.x - inner.x, centre.x + inner.x), std::clamp(p.y, centre.y - inner.y, centre.y + inner.y));
        };
        const auto colour = [&](float angle, bool filled) {
            if (!filled)
                return track.ToPremultiplied(opacity);
            return Mix(fill, fill_end, value > 0.0f ? std::min(1.0f, angle / (value * 2.0f * kPi)) : 0.0f, opacity);
        };
        for (size_t i = 0; i + 1 < angles.size(); ++i)
        {
            const float a = angles[i], b = angles[i + 1];
            if (b - a < 1e-6f)
                continue;
            const bool filled = (a + b) * 0.5f < value * 2.0f * kPi;
            const int v0 = Push(mesh, outer_point(a), colour(a, filled));
            const int v1 = Push(mesh, outer_point(b), colour(b, filled));
            const int v2 = Push(mesh, inner_point(b), colour(b, filled));
            const int v3 = Push(mesh, inner_point(a), colour(a, filled));
            Quad(mesh, v0, v1, v2, v3);
        }
    }
};

// size="dp" (the box), inner="dp" (radius of the centre disc), bounds="deg,deg,…" (sector
// edges clockwise from the positive x axis with y down, one more than sectors),
// selected="<sector index>|centre". Children marked data-sector="<index>|centre" with
// data-size="dp" are centred in their sector by the element.
class WheelElement final : public DrawElement
{
  public:
    using DrawElement::DrawElement;

  protected:
    void OnAttributeChange(const Rml::ElementAttributes &changed) override
    {
        DrawElement::OnAttributeChange(changed);
        placed_ = false;
    }
    void OnChildAdd(Rml::Element *child) override
    {
        DrawElement::OnChildAdd(child);
        placed_ = false;
    }

    void OnUpdate() override
    {
        DrawElement::OnUpdate();
        if (placed_)
            return;
        placed_ = true;
        const std::vector<float> bounds = Bounds();
        const float size = GetAttribute<float>("size", 0.0f), inner = GetAttribute<float>("inner", 0.0f);
        const float radius = (inner + size * 0.5f - kRim) * 0.5f;
        for (int i = 0; i < GetNumChildren(); ++i)
        {
            Rml::Element *child = GetChild(i);
            const Rml::String sector = child->GetAttribute<Rml::String>("data-sector", "");
            if (sector.empty())
                continue;
            Rml::Vector2f centre = {size * 0.5f, size * 0.5f};
            const int index = std::atoi(sector.c_str());
            if (sector != "centre" && index >= 0 && static_cast<size_t>(index) + 1 < bounds.size())
            {
                const float angle = (bounds[static_cast<size_t>(index)] + bounds[static_cast<size_t>(index) + 1]) * 0.5f * kPi / 180.0f;
                centre += Rml::Vector2f(std::cos(angle), std::sin(angle)) * radius;
            }
            const float half = child->GetAttribute<float>("data-size", 0.0f) * 0.5f;
            child->SetProperty(Rml::PropertyId::Position, Rml::Property(Rml::Style::Position::Absolute));
            child->SetProperty(Rml::PropertyId::Left, Rml::Property(centre.x - half, Rml::Unit::DP));
            child->SetProperty(Rml::PropertyId::Top, Rml::Property(centre.y - half, Rml::Unit::DP));
        }
    }

    void Build(Rml::Mesh &mesh, Rml::Mesh &, Rml::Vector2f size, float dp) override
    {
        const std::vector<float> bounds = Bounds();
        if (bounds.size() < 2)
            return;
        const float opacity = GetComputedValues().opacity();
        const Rml::Colourb fill = ColourOf(*this, wheel_fill), selected = ColourOf(*this, wheel_selected), line = ColourOf(*this, wheel_line),
                           edge = ColourOf(*this, wheel_edge), middle = ColourOf(*this, wheel_centre);
        const Rml::String chosen = GetAttribute<Rml::String>("selected", "");
        const Rml::Vector2f centre = size * 0.5f;
        const float outer = std::min(centre.x, centre.y) - kRim * dp, inner = GetAttribute<float>("inner", 0.0f) * dp;
        const auto point = [&](float degrees, float radius) {
            const float angle = degrees * kPi / 180.0f;
            return centre + Rml::Vector2f(std::cos(angle), std::sin(angle)) * radius;
        };
        const auto band = [&](float from, float to, float near, float far, Rml::Colourb colour) {
            const int steps = std::max(1, static_cast<int>(std::ceil((to - from) / 3.0f)));
            const Rml::ColourbPremultiplied paint = colour.ToPremultiplied(opacity);
            for (int i = 0; i < steps; ++i)
            {
                const float a = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
                const float b = from + (to - from) * static_cast<float>(i + 1) / static_cast<float>(steps);
                Quad(mesh, Push(mesh, point(a, far), paint), Push(mesh, point(b, far), paint), Push(mesh, point(b, near), paint),
                     Push(mesh, point(a, near), paint));
            }
        };
        for (size_t i = 0; i + 1 < bounds.size(); ++i)
            band(bounds[i], bounds[i + 1], inner, outer, chosen == std::to_string(i) ? selected : fill);
        band(0.0f, 360.0f, 0.0f, inner, chosen == "centre" ? selected : middle);
        // Sector edges, then the rims on top.
        const float hair = 0.5f * dp;
        for (size_t i = 0; i + 1 < bounds.size(); ++i)
        {
            const float angle = bounds[i] * kPi / 180.0f;
            const Rml::Vector2f across = Rml::Vector2f(-std::sin(angle), std::cos(angle)) * hair;
            const Rml::ColourbPremultiplied paint = line.ToPremultiplied(opacity);
            Quad(mesh, Push(mesh, point(bounds[i], inner) - across, paint), Push(mesh, point(bounds[i], outer) - across, paint),
                 Push(mesh, point(bounds[i], outer) + across, paint), Push(mesh, point(bounds[i], inner) + across, paint));
        }
        band(0.0f, 360.0f, outer - 1.5f * dp, outer, edge);
        band(0.0f, 360.0f, inner - 1.5f * dp, inner, edge);
    }

  private:
    static constexpr float kRim = 4.0f; // dp between the box and the outer rim (room for the shadow)

    std::vector<float> Bounds() const
    {
        std::vector<float> bounds;
        const Rml::String text = GetAttribute<Rml::String>("bounds", "");
        for (size_t at = 0; at < text.size();)
        {
            size_t end = text.find(',', at);
            if (end == Rml::String::npos)
                end = text.size();
            bounds.push_back(std::strtof(text.substr(at, end - at).c_str(), nullptr));
            at = end + 1;
        }
        return bounds;
    }

    bool placed_ = false;
};

class PaperElement final : public DrawElement
{
  public:
    using DrawElement::DrawElement;

  protected:
    Rml::Texture MeshTexture(Rml::RenderManager &manager) override
    {
        return FibreSource().GetTexture(manager);
    }

    // Uniform noise stretched along y: short vertical paper fibres. Tiled over the sheet.
  public:
    static std::unique_ptr<Rml::CallbackTextureSource> &FibreSlot()
    {
        static std::unique_ptr<Rml::CallbackTextureSource> slot;
        return slot;
    }

  protected:
    static Rml::CallbackTextureSource &FibreSource()
    {
        if (FibreSlot())
            return *FibreSlot();
        FibreSlot() = std::make_unique<Rml::CallbackTextureSource>([](const Rml::CallbackTextureInterface &texture) {
            constexpr int size = 128;
            std::vector<float> base(static_cast<size_t>(size * size));
            uint32_t seed = 7;
            for (float &value : base)
            {
                seed = seed * 1664525u + 1013904223u;
                value = static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
            }
            const auto at = [&](int x, int y) { return base[static_cast<size_t>(((y + size) % size) * size + (x + size) % size)]; };
            std::vector<Rml::byte> pixels(static_cast<size_t>(size * size * 4));
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    float sum = 0.0f;
                    for (int k = -3; k <= 3; ++k)
                        sum += at(x, y + k) * (4.0f - std::fabs(static_cast<float>(k)));
                    const float noise = std::clamp((sum / 16.0f - 0.36f) * 2.6f, 0.0f, 1.0f);
                    const auto alpha = static_cast<Rml::byte>(noise * 255.0f);
                    Rml::byte *pixel = &pixels[static_cast<size_t>((y * size + x) * 4)];
                    pixel[0] = pixel[1] = pixel[2] = pixel[3] = alpha; // premultiplied white; tinted by vertices
                }
            return texture.GenerateTexture({pixels.data(), pixels.size()}, {size, size});
        });
        return *FibreSlot();
    }

    void Build(Rml::Mesh &mesh, Rml::Mesh &overlay, Rml::Vector2f size, float dp) override
    {
        const float opacity = GetComputedValues().opacity();
        const Rml::Colourb top = ColourOf(*this, paper_top), bottom = ColourOf(*this, paper_bottom);
        const Rml::Colourb edge = ColourOf(*this, paper_edge), line = ColourOf(*this, paper_line);
        const Rml::Colourb shadow = ColourOf(*this, paper_shadow), fibre = ColourOf(*this, paper_fibre);
        const float w = size.x, h = size.y;

        // Same generator and the same sequence as the canvas component (C-Panel `deckle`).
        long long state = std::max(1, GetAttribute<int>("seed", 11));
        const auto random = [&state]() {
            state = (state * 16807) % 2147483647;
            return static_cast<float>(state % 1000) / 1000.0f;
        };
        const float bite = 2.6f * dp;
        std::vector<Rml::Vector2f> outline;
        constexpr int across = 110, down = 28;
        for (int i = 0; i <= across; ++i)
            outline.push_back({w * static_cast<float>(i) / across, random() * bite});
        for (int i = 1; i <= down; ++i)
            outline.push_back({w - random() * bite, h * static_cast<float>(i) / down});
        for (int i = across - 1; i >= 0; --i)
            outline.push_back({w * static_cast<float>(i) / across, h - random() * bite});
        for (int i = down - 1; i >= 1; --i)
            outline.push_back({random() * bite, h * static_cast<float>(i) / down});

        // Soft drop shadow under the sheet, slightly lower than the sheet itself.
        {
            const float blur = 6.0f * dp, drop = 3.0f * dp, inset = 2.0f * dp;
            const Rml::ColourbPremultiplied dark = shadow.ToPremultiplied(opacity), clear = Rml::ColourbPremultiplied(0, 0, 0, 0);
            const Rml::Vector2f in0 = {inset, inset + drop}, in1 = {w - inset, h - inset + drop};
            const Rml::Vector2f out0 = {-blur, drop - blur}, out1 = {w + blur, h + drop + blur};
            const int i0 = Push(mesh, in0, dark), i1 = Push(mesh, {in1.x, in0.y}, dark);
            const int i2 = Push(mesh, in1, dark), i3 = Push(mesh, {in0.x, in1.y}, dark);
            const int o0 = Push(mesh, out0, clear), o1 = Push(mesh, {out1.x, out0.y}, clear);
            const int o2 = Push(mesh, out1, clear), o3 = Push(mesh, {out0.x, out1.y}, clear);
            Quad(mesh, o0, o1, i1, i0);
            Quad(mesh, o1, o2, i2, i1);
            Quad(mesh, o2, o3, i3, i2);
            Quad(mesh, o3, o0, i0, i3);
        }

        const auto paper = [&](float y) {
            const float t = std::clamp(y / h, 0.0f, 1.0f);
            const auto lerp = [t](Rml::byte a, Rml::byte b) { return static_cast<Rml::byte>(std::lround(a + (b - a) * t)); };
            return Rml::Colourb(lerp(top.red, bottom.red), lerp(top.green, bottom.green), lerp(top.blue, bottom.blue), 255);
        };

        // Sheet: a band that darkens towards the torn edge around a plain centre.
        const float band = std::min(28.0f * dp, std::min(w, h) * 0.5f - 1.0f);
        const Rml::Vector2f c0 = {band, band}, c1 = {w - band, h - band};
        const auto clamp_in = [&](Rml::Vector2f p) { return Rml::Vector2f(std::clamp(p.x, c0.x, c1.x), std::clamp(p.y, c0.y, c1.y)); };
        const size_t count = outline.size();
        // Three rings approximate the soft falloff of the edge stain: full at the torn
        // edge, a third of it a little way in, none at the centre block.
        const float ring_at[3] = {0.0f, 0.4f, 1.0f};
        const float ring_stain[3] = {1.0f, 0.34f, 0.0f};
        std::vector<int> rings[3];
        for (int ring = 0; ring < 3; ++ring)
        {
            rings[ring].resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                const Rml::Vector2f p = outline[i], q = clamp_in(p);
                const Rml::Vector2f at = p + (q - p) * ring_at[ring];
                Rml::Colourb colour = Over(paper(at.y), edge, ring_stain[ring]);
                if (ring == 0)
                    colour = Over(colour, line);
                rings[ring][i] = Push(mesh, at, colour.ToPremultiplied(opacity));
            }
        }
        for (int ring = 0; ring < 2; ++ring)
            for (size_t i = 0; i < count; ++i)
            {
                const size_t next = (i + 1) % count;
                Quad(mesh, rings[ring][i], rings[ring][next], rings[ring + 1][next], rings[ring + 1][i]);
            }
        {
            const int a = Push(mesh, c0, paper(c0.y).ToPremultiplied(opacity));
            const int b = Push(mesh, {c1.x, c0.y}, paper(c0.y).ToPremultiplied(opacity));
            const int c = Push(mesh, c1, paper(c1.y).ToPremultiplied(opacity));
            const int d = Push(mesh, {c0.x, c1.y}, paper(c1.y).ToPremultiplied(opacity));
            Quad(mesh, a, b, c, d);
        }

        // Fibres: the tiled noise, tinted, over the whole sheet.
        {
            const float tile = 128.0f * dp * 0.75f;
            const Rml::ColourbPremultiplied tint = fibre.ToPremultiplied(opacity);
            const int centre = Push(overlay, size * 0.5f, tint, size * 0.5f / tile);
            std::vector<int> ring(count);
            for (size_t i = 0; i < count; ++i)
                ring[i] = Push(overlay, outline[i], tint, outline[i] / tile);
            for (size_t i = 0; i < count; ++i)
                Triangle(overlay, centre, ring[i], ring[(i + 1) % count]);
        }

        // Foxing: two faint stains, three small spots; and one fold.
        const auto blot = [&](float cx, float cy, float rx, float ry, float alpha, float core) {
            const Rml::Colourb stain(138, 92, 46, static_cast<Rml::byte>(alpha * 255.0f));
            const int centre = Push(mesh, {cx, cy}, stain.ToPremultiplied(opacity));
            constexpr int segments = 20;
            int first = 0, previous = 0, first_rim = 0, previous_rim = 0;
            for (int i = 0; i < segments; ++i)
            {
                const float angle = 2.0f * kPi * static_cast<float>(i) / segments;
                const Rml::Vector2f direction = {std::cos(angle), std::sin(angle)};
                const int mid = Push(mesh, {cx + direction.x * rx * core, cy + direction.y * ry * core}, stain.ToPremultiplied(opacity));
                const int rim = Push(mesh, {cx + direction.x * rx, cy + direction.y * ry}, Rml::ColourbPremultiplied(0, 0, 0, 0));
                if (i == 0)
                {
                    first = mid;
                    first_rim = rim;
                }
                else
                {
                    Triangle(mesh, centre, previous, mid);
                    Quad(mesh, previous, previous_rim, rim, mid);
                }
                previous = mid;
                previous_rim = rim;
            }
            Triangle(mesh, centre, previous, first);
            Quad(mesh, previous, previous_rim, first_rim, first);
        };
        blot(w * 0.09f, h * 0.88f, w * 0.22f, h * 0.38f, 0.16f, 0.05f);
        blot(w * 0.93f, h * 0.14f, w * 0.14f, h * 0.26f, 0.13f, 0.05f);
        blot(w * 0.71f, h * 0.78f, 2.5f * dp, 2.5f * dp, 0.16f, 0.6f);
        blot(w * 0.74f, h * 0.74f, 2.0f * dp, 2.0f * dp, 0.12f, 0.5f);
        blot(w * 0.41f, h * 0.90f, 2.5f * dp, 2.5f * dp, 0.12f, 0.6f);
        {
            const float x = w * 0.62f, half = std::max(1.0f, w * 0.005f);
            const Rml::ColourbPremultiplied crease = Rml::Colourb(110, 74, 36, 26).ToPremultiplied(opacity), clear(0, 0, 0, 0);
            const int a = Push(mesh, {x - half, bite}, clear), b = Push(mesh, {x, bite}, crease);
            const int c = Push(mesh, {x + half, bite}, clear), d = Push(mesh, {x + half, h - bite}, clear);
            const int e = Push(mesh, {x, h - bite}, crease), f = Push(mesh, {x - half, h - bite}, clear);
            Quad(mesh, a, b, e, f);
            Quad(mesh, b, c, d, e);
        }
    }
};

} // namespace

void ReleaseDrawTextures()
{
    PaperElement::FibreSlot().reset();
}

namespace
{

bool clamps_changed = false;

// <ui-clamp lines="2" text="…">: at most `lines` lines (1–3 in the design: list rows,
// dialogue replies, table cells); a longer text ends in "…" after the last whole word.
// The text is wrapped here with the same font metrics RmlUi uses, after each layout that
// changed the width.
class ClampElement final : public Rml::Element
{
  public:
    using Rml::Element::Element;

  protected:
    void OnAttributeChange(const Rml::ElementAttributes &changed) override
    {
        Rml::Element::OnAttributeChange(changed);
        if (changed.count("text") || changed.count("lines"))
        {
            width_ = -1.0f;
            Show(GetAttribute<Rml::String>("text", ""));
        }
    }

    void OnLayout() override
    {
        Rml::Element::OnLayout();
        const float width = GetBox().GetSize(Rml::BoxArea::Content).x;
        if (width <= 0.0f || std::fabs(width - width_) < 0.5f)
            return;
        width_ = width;
        // Children created during layout would be drawn before their styles (the font)
        // are resolved: refit in the next update instead.
        refit_ = true;
        clamps_changed = true;
    }

    void OnUpdate() override
    {
        Rml::Element::OnUpdate();
        if (!refit_)
            return;
        refit_ = false;
        Show(Fit(GetAttribute<Rml::String>("text", ""), width_));
    }

  private:
    void Show(const std::string &text)
    {
        if (text == shown_ && GetNumChildren() > 0)
            return;
        shown_ = text;
        SetInnerRML(EscapeRml(text));
        clamps_changed = true;
    }

    float Width(const std::string &text)
    {
        return static_cast<float>(Rml::ElementUtilities::GetStringWidth(this, text));
    }

    std::string Fit(const std::string &text, float width)
    {
        const int lines = std::clamp(GetAttribute<int>("lines", 1), 1, 6);
        std::vector<std::string> words;
        for (size_t at = 0; at <= text.size();)
        {
            const size_t end = std::min(text.find(' ', at), text.size());
            if (end > at)
                words.push_back(text.substr(at, end - at));
            at = end + 1;
        }
        std::string done, line;
        int line_count = 1;
        for (size_t i = 0; i < words.size(); ++i)
        {
            const std::string candidate = line.empty() ? words[i] : line + " " + words[i];
            if (line.empty() || Width(candidate) <= width)
            {
                line = candidate;
                continue;
            }
            if (line_count == lines)
                return done + Ellipsis(line, width);
            done += line + " ";
            line = words[i];
            ++line_count;
        }
        return text;
    }

    // The last line with as much of it as fits before "…".
    std::string Ellipsis(std::string line, float width)
    {
        static const std::string dots = "\xE2\x80\xA6";
        while (!line.empty() && Width(line + dots) > width)
        {
            const size_t space = line.rfind(' ');
            if (space != std::string::npos)
                line.erase(space);
            else
            {
                line.pop_back();
                while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80)
                    line.pop_back();
                if (!line.empty() && (static_cast<unsigned char>(line.back()) & 0x80))
                    line.pop_back(); // lead byte of the cut character
            }
        }
        while (!line.empty() && std::strchr(" ,;:.!?-", line.back()))
            line.pop_back();
        return line + dots;
    }

    float width_ = -1.0f;
    bool refit_ = false;
    std::string shown_;
};

// <ui-pages text="…" page="n" mode="paged|speech|replies">: the text wrapped with the
// element's own font into lines of its content width, shown one page (as many lines as
// its height holds) at a time. `page` counts how often the player asked for the next page;
// the element advances from where the shown page starts, so a new width or height reflows
// the rest of the text without skipping any. The final page must leave room for the
// sibling marked data-reserve (the replies), whose height it measures. The element gets
// the class `more` while pages remain and `last` on the final page; the template shows
// the «next» control or the replies with sibling selectors (`ui-pages.more ~ …`). `mode`
// speech / replies forces one of the two states (the design canvas shows them so).
class PagesElement final : public Rml::Element
{
  public:
    using Rml::Element::Element;

  protected:
    void OnAttributeChange(const Rml::ElementAttributes &changed) override
    {
        Rml::Element::OnAttributeChange(changed);
        if (changed.count("text"))
        {
            const std::string text = GetAttribute<Rml::String>("text", "");
            if (text != text_)
            {
                text_ = text;
                offset_ = 0;
                applied_ = GetAttribute<int>("page", 0);
            }
            dirty_ = true;
        }
        if (changed.count("page"))
        {
            const int page = GetAttribute<int>("page", 0);
            if (page < applied_) // the screen started over
            {
                offset_ = 0;
                applied_ = 0;
            }
            dirty_ = true;
        }
        if (changed.count("mode"))
            dirty_ = true;
        if (changed.count("class")) // the template patch rewrote the classes: keep the state
            ShowState();
    }

    void OnLayout() override
    {
        Rml::Element::OnLayout();
        const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
        const float reserve = Reserve();
        if (std::fabs(size.x - width_) >= 0.5f || std::fabs(size.y - height_) >= 0.5f || std::fabs(reserve - reserve_) >= 0.5f)
        {
            width_ = size.x;
            height_ = size.y;
            reserve_ = reserve;
            dirty_ = true;
        }
        if (dirty_)
            clamps_changed = true; // refit in the next update, then lay out again
    }

    void OnUpdate() override
    {
        Rml::Element::OnUpdate();
        if (!dirty_ || width_ <= 0.0f || height_ <= 0.0f)
            return;
        dirty_ = false;
        Refit();
    }

  private:
    float Reserve()
    {
        Rml::Element *parent = GetParentNode();
        for (int i = 0; parent && i < parent->GetNumChildren(); ++i)
        {
            Rml::Element *child = parent->GetChild(i);
            if (child != this && child->HasAttribute("data-reserve") && child->GetNumChildren() > 0)
                return child->GetBox().GetSize(Rml::BoxArea::Border).y + child->GetBox().GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Top);
        }
        return 0.0f;
    }

    float LineHeight()
    {
        const float line = GetComputedValues().line_height().value;
        return line > 0.0f ? line : std::max(1.0f, GetComputedValues().font_size() * 1.2f);
    }

    PageBreak Page(size_t offset, std::vector<size_t> &starts)
    {
        const std::string rest = text_.substr(std::min(offset, text_.size()));
        starts = WrapLines(rest, width_, [this](const std::string &line) { return static_cast<float>(Rml::ElementUtilities::GetStringWidth(this, line)); });
        const float line = LineHeight();
        std::vector<float> bottoms;
        for (size_t i = 0; i + 1 < starts.size(); ++i)
            bottoms.push_back(line * static_cast<float>(i + 1));
        const float total = bottoms.empty() ? 0.0f : bottoms.back();
        // Rounding of the box must not cost a line that fits.
        const float slack = 0.5f;
        return ChoosePage(text_, offset, starts, bottoms, total, height_ + slack, height_ - reserve_ + slack);
    }

    void Refit()
    {
        const std::string mode = GetAttribute<Rml::String>("mode", "paged");
        const int page = GetAttribute<int>("page", 0);
        std::vector<size_t> starts;
        PageBreak shown = Page(offset_, starts);
        while (mode == "paged" && applied_ < page && shown.more)
        {
            offset_ = shown.next;
            ++applied_;
            shown = Page(offset_, starts);
        }
        applied_ = std::max(applied_, page);
        more_ = mode == "speech" || (mode == "paged" && shown.more);
        // Lines of this page, each exactly as measured (the element does not wrap again).
        std::string rml;
        const size_t end = mode == "replies" ? text_.size() : shown.display;
        for (size_t i = 0; i + 1 < starts.size() && offset_ + starts[i] < end; ++i)
        {
            std::string line = text_.substr(offset_ + starts[i], starts[i + 1] - starts[i]);
            while (!line.empty() && (line.back() == '\n' || line.back() == ' '))
                line.pop_back();
            rml += (i ? "<br/>" : "") + EscapeRml(line);
        }
        if (rml != shown_)
        {
            shown_ = rml;
            SetInnerRML(rml);
        }
        ShowState();
    }

    void ShowState()
    {
        if (IsClassSet("more") != more_)
            SetClass("more", more_);
        if (IsClassSet("last") == more_)
            SetClass("last", !more_);
    }

    std::string text_, shown_;
    size_t offset_ = 0; // start of the page shown
    int applied_ = 0;   // `page` requests already turned into offsets
    float width_ = -1.0f, height_ = -1.0f, reserve_ = -1.0f;
    bool dirty_ = true;
    bool more_ = false;
};

} // namespace

bool TextClampsChanged()
{
    const bool changed = clamps_changed;
    clamps_changed = false;
    return changed;
}

void RegisterDrawElements()
{
    const auto colour = [](const char *name, const char *fallback) {
        return Rml::StyleSheetSpecification::RegisterProperty(name, fallback, false, false).AddParser("color").GetId();
    };
    ring_fill = colour("ring-fill", "#ffe7a0");
    ring_fill_end = colour("ring-fill-end", "#ecd08a");
    ring_track = colour("ring-track", "rgba(236,208,138,56)");
    paper_top = colour("paper-top", "#e9dcc0");
    paper_bottom = colour("paper-bottom", "#d9c7a3");
    paper_edge = colour("paper-edge", "rgba(128,84,38,92)");
    paper_line = colour("paper-line", "rgba(120,76,34,128)");
    paper_shadow = colour("paper-shadow", "rgba(0,0,0,140)");
    paper_fibre = colour("paper-fibre", "rgba(115,82,46,41)");
    wheel_fill = colour("wheel-fill", "rgba(14,27,32,230)");
    wheel_selected = colour("wheel-selected", "rgba(118,94,50,247)");
    wheel_line = colour("wheel-line", "rgba(203,177,116,115)");
    wheel_edge = colour("wheel-edge", "#d8bd7a");
    wheel_centre = colour("wheel-centre", "rgba(60,18,14,242)");

    static Rml::ElementInstancerGeneric<RingElement> ring;
    static Rml::ElementInstancerGeneric<PaperElement> paper;
    Rml::Factory::RegisterElementInstancer("ui-ring", &ring);
    Rml::Factory::RegisterElementInstancer("ui-paper", &paper);
    static Rml::ElementInstancerGeneric<WheelElement> wheel;
    Rml::Factory::RegisterElementInstancer("ui-wheel", &wheel);
    static Rml::ElementInstancerGeneric<ClampElement> clamp;
    Rml::Factory::RegisterElementInstancer("ui-clamp", &clamp);
    static Rml::ElementInstancerGeneric<PagesElement> pages;
    Rml::Factory::RegisterElementInstancer("ui-pages", &pages);
}

} // namespace storm::ui
