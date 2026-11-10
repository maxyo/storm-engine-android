// What the HUD scene checks share: expectations, the synthetic states, a recording mount for
// a scene alone (Bench) and the whole runtime with a renderer that draws nothing and the
// small synthetic pack of tests/pack (Runtime). No GL context, no game data.
#pragma once

#include "../src/hud/hud.h"
#include "storm_ui/render_backend.h"

#include <RmlUi/Core/RenderInterface.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>

using namespace storm::ui;
using namespace storm::ui::hud;

inline int failures = 0;
inline std::string states_directory;

inline void Expect(bool ok, const std::string &what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

inline void ExpectEqual(const std::string &actual, const std::string &expected, const std::string &what)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", what.c_str(), expected.c_str(), actual.c_str());
        ++failures;
    }
}

inline std::string Text(const std::string &name)
{
    std::ifstream file(states_directory + "/" + name + ".json", std::ios::binary);
    Expect(static_cast<bool>(file), "state file " + name + ".json exists");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

inline Json State(const std::string &name)
{
    return Json::parse(Text(name), nullptr, false);
}

// --- the scene alone --------------------------------------------------------------------

struct FakeMount final : Mount
{
    View shown;
    bool visible = false;
    std::vector<std::string> commands, slots, logs, errors;
    double now = 0.0;

    bool ShowHud(const View &view) override
    {
        shown = view;
        visible = true;
        return true;
    }
    void HideHud() override
    {
        visible = false;
    }
    void Send(const Action &action) override
    {
        commands.push_back(EngineCommand(action));
        slots.push_back(action.node);
    }
    void SetLanguage(const std::string &) override
    {
    }
    std::string Text(const std::string &key) override
    {
        return key;
    }
    void Log(const std::string &line) override
    {
        logs.push_back(line);
    }
    void Error(const std::string &line) override
    {
        errors.push_back(line);
    }
    double Now() const override
    {
        return now;
    }
    bool InterfaceActive() const override
    {
        return false;
    }
    // What a test says is on the screen, by "<id> <selector>".
    bool Measure(const std::string &id, const std::string &selector, Placement &out) const override
    {
        const auto found = boxes.find(id + " " + selector);
        if (found == boxes.end())
            return false;
        out = found->second;
        return true;
    }
    std::map<std::string, Placement> boxes;
    std::string Last() const
    {
        return commands.empty() ? std::string() : commands.back();
    }
    bool Has(const char *id) const
    {
        return shown.elements.contains(id);
    }
};

struct QuietSurface final : screens::Surface
{
    void Show(const std::string &, const Json &) override
    {
    }
    void Hide() override
    {
    }
    void Send(const Action &) override
    {
    }
    void RequestText(TextRequest, const std::string &) override
    {
    }
    void Toast(const std::string &) override
    {
    }
    void SetLanguage(const std::string &) override
    {
    }
    std::string Text(const std::string &key) override
    {
        return key;
    }
    void Log(const std::string &) override
    {
    }
    void Error(const std::string &) override
    {
    }
    double Now() const override
    {
        return 0.0;
    }
};

struct Bench
{
    FakeMount mount;
    Hud hud{mount};
    QuietSurface surface;
    screens::Router router{surface};

    Bench()
    {
        router.AttachHud(&hud);
        Feed("presentation", "presentation-game");
    }
    void Feed(const std::string &channel, const std::string &file)
    {
        router.SetState(channel, State(file));
        hud.Tick();
    }
    void Set(const std::string &channel, const Json &state)
    {
        router.SetState(channel, state);
        hud.Tick();
    }
    void Explore()
    {
        Feed("gameplay", "gameplay-explore");
        Feed("combat", "combat-explore");
        Feed("vitals", "vitals");
        Feed("worldInfo", "world-info");
        Feed("landCommands", "land-commands-idle");
    }
    void Melee()
    {
        Feed("gameplay", "gameplay-melee");
        Feed("combat", "combat-melee");
        Feed("vitals", "vitals");
        Feed("landCommands", "land-commands-idle");
    }
    // A frame: timed work, then the drawn-frame notice.
    void Frame(double seconds = 0.0)
    {
        mount.now += seconds;
        hud.Tick();
        hud.Drawn();
    }
    void Press(const std::string &id, const std::string &verb = "click", const std::string &value = {})
    {
        hud.OnEvent({id, verb, value});
        hud.Tick();
    }
};

// --- the whole runtime ------------------------------------------------------------------

class NoRenderer final : public Rml::RenderInterface
{
  public:
    // Distinct handles, as a real renderer gives (the runtime identifies meshes by handle).
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    {
        return ++next_;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
    {
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
        return ++next_;
    }
    void ReleaseTexture(Rml::TextureHandle) override
    {
    }
    Rml::TextureHandle SaveLayerAsTexture() override
    {
        return ++next_;
    }
    void EnableScissorRegion(bool) override
    {
    }
    void SetScissorRegion(Rml::Rectanglei) override
    {
    }

  private:
    uintptr_t next_ = 0;
};

class NoBackend final : public RenderBackend
{
  public:
    Rml::RenderInterface &Interface() override
    {
        return renderer_;
    }
    void SetViewport(int, int) override
    {
    }
    void BeginFrame(bool) override
    {
    }
    void EndFrame() override
    {
    }

  private:
    NoRenderer renderer_;
};

struct Runtime
{
    std::unique_ptr<FileSource> files;
    NoBackend backend;
    std::unique_ptr<Host> host;
    std::vector<std::string> commands;
    double clock = 0.0;

    explicit Runtime(const std::string &pack)
    {
        files = DirectoryFiles(pack);
        Config config;
        config.files = files.get();
        config.backend = &backend;
        config.width = 800; // density 1: pixels are dp
        config.height = 400;
        config.clock = [this] { return clock; };
        config.verify_unchanged = true; // Frame reports a frame Prepare wrongly called unchanged (FrameStats::mispredicted)
        config.on_action = [this](const Action &action) { commands.push_back(EngineCommand(action)); };
        std::string error;
        host = Host::Create(config, &error);
        Expect(host != nullptr, "the runtime starts with the test pack: " + error);
    }
    void Feed(const std::string &channel, const std::string &file)
    {
        Expect(host->SetState(channel, Text(file)), "state " + file + " is accepted");
    }
    void Frame(double seconds = 1.0 / 60.0)
    {
        clock += seconds;
        host->Frame();
        Expect(!host->LastFrameStats().mispredicted, "a frame Prepare called unchanged draws the same as the frame shown");
    }
    void Explore()
    {
        Feed("presentation", "presentation-game");
        Feed("gameplay", "gameplay-explore");
        Feed("combat", "combat-explore");
        Feed("vitals", "vitals");
        Feed("worldInfo", "world-info");
        Feed("contextAction", "context-door");
        Feed("landCommands", "land-commands-idle");
        Frame();
        Frame();
    }
    bool Sent(const std::string &command) const
    {
        for (const std::string &sent : commands)
            if (sent == command)
                return true;
        return false;
    }
    std::string Last() const
    {
        return commands.empty() ? std::string() : commands.back();
    }
};
