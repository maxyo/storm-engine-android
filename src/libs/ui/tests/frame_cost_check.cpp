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
#include "../src/frame_renderer.h"
#include "hud_bench.h"

#include <RmlUi/Core.h>

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
    Expect(!runtime.host->Prepare(delay), "the screen is complete in that one frame");
}

// A fling: after the finger lifts, the list keeps moving (RmlUi's inertia) and every frame of
// that motion is drawn, until it stops; then the runtime is idle again.
void ScrollFling(const std::string &pack)
{
    Runtime runtime(pack);
    Expect(runtime.host->LoadDocument("scroll.rml"), "the scroll test document loads");
    for (int i = 0; i < 3; ++i)
        runtime.Frame();
    double delay = 0.0;
    Expect(!runtime.host->Prepare(delay), "a still list needs no frame");
    runtime.host->Touch(TouchPhase::Down, 1, 150, 280);
    runtime.Frame();
    for (int y = 260; y >= 60; y -= 40) // a quick upward swipe, one move per frame
    {
        runtime.host->Touch(TouchPhase::Move, 1, 150, static_cast<float>(y));
        runtime.Frame();
        if (y < 260) // the first move may add the edge fade (a new element)
            Expect(runtime.host->LastFrameStats().laid_out == 0, "scrolling lays nothing out (the edge fade moves by transform)");
    }
    runtime.host->Touch(TouchPhase::Up, 1, 150, 60);
    int moving = 0;
    for (int i = 0; i < 600 && runtime.host->Prepare(delay); ++i)
    {
        Expect(delay <= 0.0, "while the list moves the next frame is wanted at once");
        runtime.Frame();
        ++moving;
    }
    Expect(moving >= 5, "the motion after the finger lifts is drawn frame by frame (" + std::to_string(moving) + " frames)");
    Expect(moving < 600, "and it settles");
    Expect(!runtime.host->Prepare(delay), "then the list is idle");
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
    SteadyRuntime(argv[2]);
    TapLatency(argv[2]);
    ScrollFling(argv[2]);
    if (failures)
        std::fprintf(stderr, "%d frame cost check(s) failed\n", failures);
    else
        std::printf("frame cost checks passed\n");
    return failures ? 1 : 0;
}
