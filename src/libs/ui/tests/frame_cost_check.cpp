// What a frame costs, held as checks so that a regression fails here, not on a device:
//
//   * FrameRenderer alone, with a recording backend: a filtered layer that did not change
//     is drawn from its cached result (no backend layer, no filter pass); a change, a
//     nested layer, a clip mask and an empty region behave as documented.
//   * The whole runtime with the synthetic pack (tests/pack, NoBackend of hud_bench.h): a
//     steady HUD needs no frame (Prepare false) and a drawn steady frame rebuilds, lays out,
//     compiles and uploads nothing; a state that changes nothing visible needs no frame; one
//     that does is drawn. Every frame Prepare calls unchanged is verified (Runtime::Frame).
//   * The tap budget: press in the next frame, action in the touch call, the answer's
//     screen in the next frame after the model.
//   * Touch scrolling (ScrollFling): drag 1:1, fling on the frame clock, nothing laid out.
#include "../src/frame_renderer.h"
#include "hud_bench.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Transform.h>
#include <RmlUi/Core/TransformPrimitive.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{

// The backend side: records what reaches it.
class Recorder final : public Rml::RenderInterface
{
  public:
    std::vector<std::string> calls;
    uintptr_t next = 100;

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    {
        return ++next;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f, Rml::TextureHandle texture) override
    {
        calls.push_back("draw " + std::to_string(geometry) + " " + std::to_string(texture));
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override
    {
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &) override
    {
        return 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
    {
        return ++next;
    }
    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        calls.push_back("release-texture " + std::to_string(texture));
    }
    void EnableScissorRegion(bool) override
    {
    }
    void SetScissorRegion(Rml::Rectanglei) override
    {
    }
    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f) override
    {
        calls.push_back("mask " + std::to_string(geometry));
    }
    Rml::LayerHandle PushLayer() override
    {
        calls.push_back("push");
        return ++next;
    }
    void CompositeLayers(Rml::LayerHandle, Rml::LayerHandle, Rml::BlendMode, Rml::Span<const Rml::CompiledFilterHandle>) override
    {
        calls.push_back("composite");
    }
    void PopLayer() override
    {
        calls.push_back("pop");
    }
    Rml::TextureHandle SaveLayerAsTexture() override
    {
        calls.push_back("save");
        return ++next;
    }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String &, const Rml::Dictionary &) override
    {
        return ++next;
    }
    void ReleaseFilter(Rml::CompiledFilterHandle) override
    {
    }
    int Count(const std::string &prefix) const
    {
        int count = 0;
        for (const std::string &call : calls)
            count += call.rfind(prefix, 0) == 0;
        return count;
    }
};

Rml::CompiledGeometryHandle Mesh(FrameRenderer &renderer, float x)
{
    Rml::Vertex vertices[3] = {};
    vertices[0].position = {x, 0};
    vertices[1].position = {x + 10, 0};
    vertices[2].position = {x, 10};
    const int indices[3] = {0, 1, 2};
    return renderer.CompileGeometry({vertices, 3}, {indices, 3});
}

// What RmlUi does for an element with `filter:` (ElementEffects): a layer at Enter, the
// element's draws, the filter region as scissor at Exit, the composite, the pop.
void FilteredElement(FrameRenderer &renderer, Rml::CompiledGeometryHandle geometry, Rml::CompiledFilterHandle filter, Rml::Rectanglei region,
                     bool nested = false, Rml::CompiledGeometryHandle mask = 0, float scrolled = 0)
{
    const Rml::LayerHandle layer = renderer.PushLayer();
    if (mask)
        renderer.RenderToClipMask(Rml::ClipMaskOperation::Set, mask, {0, 0});
    renderer.RenderGeometry(geometry, {5, 5 - scrolled}, 0);
    if (nested)
    {
        const Rml::LayerHandle inner = renderer.PushLayer();
        renderer.RenderGeometry(geometry, {7, 7}, 0);
        renderer.EnableScissorRegion(true);
        renderer.SetScissorRegion(region);
        renderer.CompositeLayers(inner, layer, Rml::BlendMode::Blend, {&filter, 1});
        renderer.PopLayer();
    }
    renderer.EnableScissorRegion(true);
    renderer.SetScissorRegion(region);
    renderer.CompositeLayers(layer, 0, Rml::BlendMode::Blend, {&filter, 1});
    renderer.PopLayer();
    renderer.EnableScissorRegion(false);
}

void FilterCache()
{
    Recorder backend;
    FrameStats stats;
    FrameRenderer renderer(backend, stats);
    const Rml::CompiledGeometryHandle geometry = Mesh(renderer, 0), other = Mesh(renderer, 50), mask = Mesh(renderer, 90);
    const Rml::CompiledFilterHandle filter = renderer.CompileFilter("drop-shadow", {});
    const Rml::Rectanglei region = Rml::Rectanglei::FromPositionSize({0, 0}, {40, 40});

    const auto frame = [&](const auto &draw) {
        backend.calls.clear();
        stats = FrameStats();
        renderer.BeginFrame();
        draw();
        renderer.EndFrame();
    };

    frame([&] { FilteredElement(renderer, geometry, filter, region); });
    Expect(backend.Count("push") == 1 && backend.Count("composite") == 1 && backend.Count("save") == 0,
           "content seen once is composited as it came: no extra layer or texture");
    const uint64_t first = renderer.FrameHash();

    frame([&] { FilteredElement(renderer, geometry, filter, region); });
    Expect(backend.Count("push") == 2 && backend.Count("save") == 1, "seen unchanged again: filtered into a second layer and saved");
    Expect(stats.cached_layers == 0, "nothing was cached before");

    frame([&] { FilteredElement(renderer, geometry, filter, region); });
    Expect(backend.Count("push") == 0 && backend.Count("composite") == 0 && backend.Count("save") == 0,
           "an unchanged filtered layer costs the backend no layer, filter or composite");
    Expect(backend.Count("draw") == 1, "it is one textured quad");
    Expect(stats.cached_layers == 1 && stats.layers == 1, "counted as a cached layer");
    Expect(renderer.FrameHash() == first, "the same frame hashes the same, cached or not");

    // Scrolled: the content and its region moved together by 7 px.
    const Rml::Rectanglei moved = Rml::Rectanglei::FromPositionSize({0, -7}, {40, 40});
    frame([&] { FilteredElement(renderer, geometry, filter, moved, false, 0, 7); });
    Expect(backend.Count("push") == 0 && backend.Count("draw") == 1, "content that only moved (a scrolled list) stays cached");
    Expect(renderer.FrameHash() != first, "but the frame is another one");

    frame([&] { FilteredElement(renderer, other, filter, region); });
    Expect(backend.Count("push") == 1 && backend.Count("save") == 0, "changed content is composited as it came");
    Expect(backend.Count("release-texture") == 1, "the result no frame used any more is released");

    for (int i = 0; i < 3; ++i) // an animation: other content every frame is never saved
        frame([&] { FilteredElement(renderer, geometry, filter, Rml::Rectanglei::FromPositionSize({i * 3, 0}, {40 + i, 40})); });
    Expect(backend.Count("save") == 0, "content that changes every frame is never saved");

    frame([&] { FilteredElement(renderer, other, filter, region, true); });
    frame([&] { FilteredElement(renderer, other, filter, region, true); });
    Expect(backend.Count("push") == 3, "a layer inside the filtered one is part of its content (drawn when saved)");
    frame([&] { FilteredElement(renderer, other, filter, region, true); });
    Expect(backend.Count("push") == 0 && backend.Count("draw") == 1, "and cached with it");

    for (int i = 0; i < 3; ++i)
        frame([&] { FilteredElement(renderer, geometry, filter, region, false, mask); });
    Expect(backend.Count("push") == 0 && backend.Count("mask") == 1, "a cached layer still sets the clip mask it sets (the stencil is shared)");

    frame([&] { FilteredElement(renderer, geometry, filter, Rml::Rectanglei::FromPositionSize({0, 0}, {40, 0})); });
    Expect(backend.Count("push") == 0 && backend.Count("draw") == 0, "a layer composited into an empty region draws nothing");
}

// RmlUi's box shadow: a layer drawn, saved as a texture, popped. An element rebuilt with the
// same look draws the same: it gets the texture made before, and it lives while held.
void SharedShadows()
{
    Recorder backend;
    FrameStats stats;
    FrameRenderer renderer(backend, stats);
    const Rml::CompiledGeometryHandle shape = Mesh(renderer, 0);
    const auto shadow = [&](float x) {
        renderer.EnableScissorRegion(true);
        renderer.SetScissorRegion(Rml::Rectanglei::FromPositionSize({static_cast<int>(x), 0}, {60, 30}));
        renderer.PushLayer();
        renderer.RenderGeometry(shape, {x + 4, 4}, 0);
        const Rml::TextureHandle texture = renderer.SaveLayerAsTexture();
        renderer.PopLayer();
        renderer.EnableScissorRegion(false);
        return texture;
    };
    renderer.BeginFrame();
    const Rml::TextureHandle first = shadow(0);
    renderer.EndFrame();
    Expect(backend.Count("save") == 1, "the first box shadow is made");
    backend.calls.clear();
    renderer.BeginFrame();
    renderer.ReleaseTexture(first); // the element was rebuilt: RmlUi lets go of the old one...
    const Rml::TextureHandle again = shadow(100); // ...and makes the same shadow elsewhere
    renderer.EndFrame();
    Expect(again == first && backend.Count("save") == 0 && backend.Count("push") == 0, "the same shadow again is the texture made before");
    Expect(stats.textures_shared == 1, "counted as shared");
    Expect(backend.Count("release-texture") == 0, "a held texture is not released");
}

void SteadyRuntime(const std::string &pack)
{
    Runtime runtime(pack);
    runtime.Explore();
    double delay = 0.0;
    Expect(!runtime.host->Prepare(delay), "a steady HUD needs no frame");
    runtime.Frame();
    const FrameStats steady = runtime.host->LastFrameStats();
    Expect(steady.rebuilds == 0 && steady.laid_out == 0, "a steady frame rebuilds and lays out nothing");
    Expect(steady.geometry == 0 && steady.textures == 0, "a steady frame compiles and uploads nothing");

    runtime.Feed("vitals", "vitals"); // published again, same content
    Expect(!runtime.host->Prepare(delay), "a state that changes nothing visible needs no frame");
    runtime.Frame();

    runtime.Feed("vitals", "vitals-status");
    Expect(runtime.host->Prepare(delay), "a state that changes the HUD asks for a frame");
    runtime.Frame();
    Expect(runtime.host->LastFrameStats().rebuilds > 0, "and that frame rebuilds what changed");
    for (int i = 0; i < 5; ++i)
        runtime.Frame();
    Expect(!runtime.host->Prepare(delay), "then the HUD is steady again");
}

// The tap budget, in frames: the press shows in the next drawn frame, the action leaves in
// the touch call itself, and the engine's answer (the interface model) shows in the next
// drawn frame after it arrives. Nothing in the UI waits a further frame, a timer or a reply.
void TapLatency(const std::string &pack)
{
    Runtime runtime(pack);
    runtime.Explore();
    double delay = 0.0;
    Expect(!runtime.host->Prepare(delay), "the HUD is steady before the tap");

    runtime.host->Touch(TouchPhase::Down, 1, 766, 34); // the pause (menu) button
    Expect(runtime.host->Prepare(delay), "a touch-down asks for a frame at once (the press)");
    runtime.Frame();
    Expect(!runtime.host->Prepare(delay), "the press is complete in that one frame: no second frame needed");

    const size_t before = runtime.commands.size();
    runtime.host->Touch(TouchPhase::Up, 1, 766, 34);
    Expect(runtime.commands.size() > before && runtime.Sent("shortcut:mainmenu"), "the action leaves in the touch-up call, before any frame");

    runtime.Feed("presentation", "presentation-menu");
    std::ifstream file(states_directory + "/../models/game-menu.json", std::ios::binary);
    const std::string model{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    Expect(runtime.host->SetInterface(model), "the engine's answer (the game menu model) is accepted");
    Expect(runtime.host->Prepare(delay), "the model asks for a frame at once");
    runtime.Frame();
    Expect(runtime.host->InterfaceActive(), "the next frame shows the screen");
    // A new screen: its document, the clamped text re-cut for the widths it got, the HUD hidden.
    Expect(runtime.host->LastFrameStats().layout_passes <= 3, "a new screen costs at most three layout passes");
    runtime.host->SetInterface(model); // the same revision again
    runtime.Frame();
    Expect(runtime.host->LastFrameStats().layout_passes == 0, "a model that changed nothing lays nothing out");
    Expect(!runtime.host->Prepare(delay), "the screen is complete in that one frame");
}

// The list of scroll.rml (300 px high, 60 rows of 41 px: 2160 px to scroll at density 1).
Rml::Element *List()
{
    Rml::Context *context = Rml::GetContext(0);
    for (int i = 0; context && i < context->GetNumDocuments(); ++i)
        if (Rml::Element *list = context->GetDocument(i)->GetElementById("list"))
            return list;
    return nullptr;
}

struct Gesture
{
    std::vector<float> drag;  // offset after each frame of the drag
    float released = 0.0f;    // offset at the release
    std::vector<float> fling; // offset after each frame from the release until the list rests
    int layouts = 0;          // layout passes in those frames (the first drag frame excluded)
    bool settled = false;     // the runtime was idle once the list rested
};

// One finger: down at y 290, then upwards at `speed` px/s with a sample every 1/120 s (two
// per 60 Hz frame, as a touchscreen gives them: every sample with its own time), `travel` px
// in all; `pause` seconds still before the release, which is `back` px behind the last move
// (an injected swipe ends on its target); frames of `frame` seconds throughout.
Gesture Swipe(Runtime &runtime, float speed, float travel, double pause, double frame, float back = 0.0f)
{
    Gesture out;
    Rml::Element *list = List();
    const double step = 1.0 / 120.0;
    double next_frame = runtime.clock + frame;
    float y = 290.0f;
    runtime.host->Touch(TouchPhase::Down, 1, 150, y, runtime.clock);
    double t = runtime.clock;
    const auto frames_until = [&](double time, std::vector<float> *offsets) {
        while (next_frame <= time + 1e-9)
        {
            runtime.Frame(next_frame - runtime.clock);
            next_frame += frame;
            if (offsets)
            {
                if (!offsets->empty())
                    out.layouts += runtime.host->LastFrameStats().layout_passes;
                offsets->push_back(list->GetScrollTop());
            }
        }
    };
    for (float moved = 0.0f; moved < travel;)
    {
        t += step;
        frames_until(t, &out.drag);
        moved = std::min(travel, moved + speed * static_cast<float>(step));
        runtime.host->Touch(TouchPhase::Move, 1, 150, 290.0f - moved, t);
        y = 290.0f - moved;
    }
    t += pause;
    frames_until(t, &out.drag);
    out.released = list->GetScrollTop();
    runtime.host->Touch(TouchPhase::Up, 1, 150, y + back, t);
    double delay = 0.0;
    for (int i = 0; i < 600 && runtime.host->Prepare(delay); ++i)
    {
        Expect(delay <= 0.0, "while the list moves the next frame is wanted at once");
        runtime.Frame(next_frame - runtime.clock);
        next_frame += frame;
        out.layouts += runtime.host->LastFrameStats().layout_passes;
        out.fling.push_back(list->GetScrollTop());
    }
    out.settled = !runtime.host->Prepare(delay);
    return out;
}

// A drag follows the finger 1:1 from where it leaves the slop, every frame; the release
// flings on at the finger's speed and slows to rest (monotonic, ever smaller steps), on the
// frame clock: the same gesture at 30 fps ends where it ends at 60 fps. Nothing is laid out
// while the list moves. A finger that rested before the release does not fling; one that
// goes down on a list in motion stops it.
// A runtime showing the list of scroll.rml at rest.
std::unique_ptr<Runtime> ListRuntime(const std::string &pack)
{
    auto runtime = std::make_unique<Runtime>(pack);
    Expect(runtime->host->LoadDocument("scroll.rml"), "the scroll test document loads");
    for (int i = 0; i < 3; ++i)
        runtime->Frame();
    Expect(List() != nullptr, "the list is there");
    double delay = 0.0;
    Expect(!runtime->host->Prepare(delay), "a still list needs no frame");
    return runtime;
}

// The vertical shift a row of the list has from a stretch (0: no transform).
float Shift(Rml::Element *row)
{
    const Rml::Property *property = row->GetLocalProperty(Rml::PropertyId::Transform);
    if (!property)
        return 0.0f;
    const Rml::TransformPtr transform = property->Get<Rml::TransformPtr>();
    if (!transform || transform->GetNumPrimitives() != 1 || transform->GetPrimitive(0).type != Rml::TransformPrimitive::TRANSLATE2D)
        return 1e9f; // not a stretch
    return transform->GetPrimitive(0).translate_2d.values[1].number;
}

bool Unstretched(Rml::Element *list)
{
    for (int i = 0; i < list->GetNumChildren(); ++i)
        if (!list->GetChild(i)->IsClassSet("scroll-fade") && list->GetChild(i)->GetLocalProperty(Rml::PropertyId::Transform))
            return false;
    return true;
}

// Past an edge the list stretches and springs back, by transforms (nothing laid out): a
// finger pulling down at the top spreads the rows from the bottom edge, the top row moving
// the most; released, every row is back without a transform within half a second. A fling
// that reaches the end stretches the rows by its momentum (at most 8% of the list) and they
// come back the same way, the list resting at its end.
void ScrollStretch(const std::string &pack)
{
    std::unique_ptr<Runtime> runtime = ListRuntime(pack);
    Rml::Element *list = List();
    double t = runtime->clock;
    runtime->host->Touch(TouchPhase::Down, 1, 150, 100, t);
    int layouts = 0;
    for (int i = 1; i <= 16; ++i) // 160 px down over 133 ms, a frame every second sample
    {
        t += 1.0 / 120.0;
        runtime->host->Touch(TouchPhase::Move, 1, 150, 100.0f + 10.0f * i, t);
        if (i % 2 == 0)
        {
            runtime->Frame(t - runtime->clock);
            layouts += i > 2 ? runtime->host->LastFrameStats().layout_passes : 0;
        }
    }
    const float top = Shift(list->GetChild(0)), lower = Shift(list->GetChild(5));
    Expect(list->GetScrollTop() == 0.0f, "a list pulled down at its top stays at its top");
    Expect(top > 5.0f && top <= 0.08f * 300.0f + 0.5f && lower > 0.0f && lower < top,
           "and stretches: the top row moves the most (" + std::to_string(top) + " px), a lower one less (" + std::to_string(lower) + " px)");
    runtime->host->Touch(TouchPhase::Up, 1, 150, 260, t);
    double delay = 0.0;
    int back = 0;
    float previous = top;
    bool shrinking = true;
    for (; back < 120 && runtime->host->Prepare(delay); ++back)
    {
        runtime->Frame();
        layouts += runtime->host->LastFrameStats().layout_passes;
        const float now = Shift(list->GetChild(0));
        shrinking = shrinking && now <= previous + 0.01f && now >= -0.01f;
        previous = now;
    }
    Expect(shrinking, "released, it springs back without overshooting");
    Expect(back <= 30 && Unstretched(list) && list->GetScrollTop() == 0.0f,
           "every row is back without a transform within half a second (" + std::to_string(back) + " frames)");
    Expect(layouts == 0, "a stretch lays nothing out (" + std::to_string(layouts) + " layout passes)");

    runtime.reset();
    runtime = ListRuntime(pack);
    list = List();
    const Gesture far = Swipe(*runtime, 8000.0f, 280.0f, 0.0, 1.0 / 60.0);
    Expect(!far.fling.empty() && far.fling.back() == 2160.0f, "a fast fling rests at the list's end (" + std::to_string(far.fling.back()) + ")");
    // One layout at most: at the end the edge fade goes (display: none), not one per frame.
    Expect(far.layouts <= 1, "and lays nothing out on the way but the end's fade (" + std::to_string(far.layouts) + " layout passes)");
    // Swipe ran until the runtime was idle: the stretch came and went. Run it again with a
    // look at the rows on the way.
    runtime.reset();
    runtime = ListRuntime(pack);
    list = List();
    t = runtime->clock;
    runtime->host->Touch(TouchPhase::Down, 1, 150, 290, t);
    for (int i = 1; i <= 14; ++i)
    {
        t += 1.0 / 120.0;
        runtime->host->Touch(TouchPhase::Move, 1, 150, 290.0f - 20.0f * i * 3.3f, t);
        if (i % 2 == 0)
            runtime->Frame(t - runtime->clock);
    }
    runtime->host->Touch(TouchPhase::Up, 1, 150, 290.0f - 20.0f * 14 * 3.3f, t);
    float most = 0.0f;
    int frames = 0;
    for (; frames < 300 && runtime->host->Prepare(delay); ++frames)
    {
        runtime->Frame();
        for (int i = 0; i < list->GetNumChildren(); ++i)
            most = std::min(most, Shift(list->GetChild(i)));
    }
    Expect(most < -1.0f && most >= -0.08f * 300.0f - 0.5f, "a fling into the end stretches the rows by its momentum (" + std::to_string(most) + " px)");
    Expect(frames < 300 && Unstretched(list) && list->GetScrollTop() == 2160.0f, "then they come back and the list rests at its end");
}

void ScrollFling(const std::string &pack)
{
    const float speed = 2400.0f; // px/s: a quick swipe
    double delay = 0.0;
    std::unique_ptr<Runtime> runtime = ListRuntime(pack);
    const Gesture fast = Swipe(*runtime, speed, 280.0f, 0.0, 1.0 / 60.0);
    // The drag: 280 px of travel, the first 10 px (the slop) move nothing.
    Expect(std::fabs(fast.released - 270.0f) <= 1.0f, "the drag follows the finger 1:1 beyond the slop (" + std::to_string(fast.released) + " px of 270)");
    bool steady = true;
    for (size_t i = 2; i < fast.drag.size(); ++i)
        steady = steady && std::fabs((fast.drag[i] - fast.drag[i - 1]) - speed / 60.0f) <= 1.0f;
    Expect(steady, "every frame of the drag moves the list by the finger's travel in that frame (40 px)");
    Expect(fast.fling.size() >= 30, "the fling lasts (" + std::to_string(fast.fling.size()) + " frames at 60 fps)");
    bool monotonic = true, slowing = true;
    float previous = fast.released, previous_step = speed / 60.0f + 2.0f;
    for (float offset : fast.fling)
    {
        const float step = offset - previous;
        monotonic = monotonic && step >= 0.0f;
        slowing = slowing && step <= previous_step + 1.0f; // whole pixels: one of rounding
        previous_step = step, previous = offset;
    }
    Expect(monotonic, "the fling moves one way");
    Expect(slowing, "and slows down every frame");
    const float first = fast.fling.empty() ? 0.0f : fast.fling.front() - fast.released;
    Expect(first >= 0.6f * speed / 60.0f && first <= 1.1f * speed / 60.0f,
           "it starts at the finger's speed (" + std::to_string(first) + " px in the first frame, the finger made 40)");
    Expect(fast.fling.size() >= 2 && fast.fling[fast.fling.size() - 1] - fast.fling[fast.fling.size() - 2] <= 1.0f,
           "and comes to rest gently");
    Expect(fast.layouts == 0, "scrolling lays nothing out (" + std::to_string(fast.layouts) + " layout passes)");
    Expect(fast.settled, "then the list is idle");

    runtime.reset();
    runtime = ListRuntime(pack);
    const Gesture slow_frames = Swipe(*runtime, speed, 280.0f, 0.0, 1.0 / 30.0);
    Expect(!slow_frames.fling.empty() && !fast.fling.empty() && std::fabs(slow_frames.fling.back() - fast.fling.back()) <= 1.0f,
           "at 30 fps the same gesture rests where it rests at 60 fps (" +
               (slow_frames.fling.empty() ? std::string("-") : std::to_string(slow_frames.fling.back())) + " / " +
               (fast.fling.empty() ? std::string("-") : std::to_string(fast.fling.back())) + ")");

    runtime.reset();
    runtime = ListRuntime(pack);
    const Gesture rested = Swipe(*runtime, speed, 280.0f, 0.1, 1.0 / 60.0);
    Expect(rested.fling.size() <= 1 && std::fabs(rested.released - 270.0f) <= 1.0f && (rested.fling.empty() || rested.fling.back() == rested.released),
           "a finger that rested before the release does not fling (" + std::to_string(rested.released) + " px, " + std::to_string(rested.fling.size()) + " frames after)");

    // The up repeats the last position (or steps back to an injected swipe's target): it is
    // not a sample of the motion, and the fling goes on at the finger's speed.
    runtime.reset();
    runtime = ListRuntime(pack);
    const Gesture stepped = Swipe(*runtime, speed, 280.0f, 0.02, 1.0 / 60.0, 3.0f);
    Expect(!stepped.fling.empty() && !fast.fling.empty() && std::fabs(stepped.fling.back() - fast.fling.back()) <= 2.0f,
           "an up 20 ms after the last move, 3 px back, flings as far (" + (stepped.fling.empty() ? std::string("-") : std::to_string(stepped.fling.back())) +
               " px)");

    // Caught: a second finger-down 5 frames into a fling holds the list where it is.
    runtime.reset();
    runtime = ListRuntime(pack);
    Rml::Element *list = List();
    runtime->host->Touch(TouchPhase::Down, 1, 150, 290, runtime->clock);
    for (int i = 1; i <= 14; ++i) // 20 px every 1/120 s, a frame after every second sample
    {
        runtime->host->Touch(TouchPhase::Move, 1, 150, 290.0f - 20.0f * i, runtime->clock + (i % 2 ? 1.0 / 120.0 : 1.0 / 60.0));
        if (i % 2 == 0)
            runtime->Frame(1.0 / 60.0);
    }
    runtime->host->Touch(TouchPhase::Up, 1, 150, 10, runtime->clock);
    for (int i = 0; i < 5; ++i)
        runtime->Frame();
    runtime->host->Touch(TouchPhase::Down, 1, 150, 150, runtime->clock);
    runtime->Frame();
    const float held = list->GetScrollTop();
    for (int i = 0; i < 10; ++i)
        runtime->Frame();
    Expect(list->GetScrollTop() == held, "a finger on a list in motion stops it");
    runtime->host->Touch(TouchPhase::Move, 1, 150, 145, runtime->clock);
    runtime->Frame();
    Expect(list->GetScrollTop() == held + 5.0f, "and drags it at once, without the slop");
    runtime->host->Touch(TouchPhase::Up, 1, 150, 145, runtime->clock + 0.1);
    runtime->Frame();
    Expect(!runtime->host->Prepare(delay), "then the list is idle");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: storm_ui_frame_cost_unit <tests/states> <tests/pack>\n");
        return 2;
    }
    states_directory = argv[1];
    FilterCache();
    SharedShadows();
    SteadyRuntime(argv[2]);
    TapLatency(argv[2]);
    ScrollFling(argv[2]);
    ScrollStretch(argv[2]);
    if (failures)
        std::fprintf(stderr, "%d frame cost check(s) failed\n", failures);
    else
        std::printf("frame cost checks passed\n");
    return failures ? 1 : 0;
}
