// Host check of the Effects entity and EFFECTS_LIBRARY through the real core.
//
//   effects_check              synthetic: writes its own Resource/Effects/effects.json into a
//                              temporary game directory; checks the id table, the particle system
//                              path and every failure path.
//   effects_check <CL game>    plays real Caribbean Legend effects (read-only) on bgfx's Noop
//                              backend; exit code 77 when the directory does not exist.
//
// The file service scans the working directory once per process, hence one mode per run.
#include "effect_catalog.hpp"
#include "effects.hpp"
#include "effects_natives.hpp"

#include "core_private.h"
#include "data.h"
#include "game_profile.hpp"
#include "message.h"
#include "shared/layers.h"
#include "shared/messages.h"
#include "storm/render/effekseer_device.hpp"
#include "v_s_stack.h"
#include "vma.hpp"

#include <bgfx/bgfx.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace efk = storm::render::effekseer;
namespace natives = storm::effects::natives;

namespace
{
int failures = 0;

void Check(bool condition, const char *what)
{
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
    failures += !condition;
}

// What the scripts' "particles" entity receives.
struct ParticleRequest
{
    int32_t code;
    std::string name;
    float values[6];
    int32_t lifetime;
};
std::vector<ParticleRequest> particle_requests;

// Script stack for the natives: arguments are pushed in declaration order, results popped.
class Stack final : public VS_STACK
{
  public:
    VDATA *Push(DATA * = nullptr) override
    {
        items_.push_back(std::make_unique<DATA>());
        return items_.back().get();
    }
    VDATA *Pop() override
    {
        popped_.push_back(std::move(items_.back()));
        items_.pop_back();
        return popped_.back().get();
    }
    VDATA *Read(uint32_t, uint32_t) override { return nullptr; }
    VDATA *Read() override { return items_.empty() ? nullptr : items_.back().get(); }
    uint32_t GetDataNum() override { return static_cast<uint32_t>(items_.size()); }
    void InvalidateFrom(uint32_t) override {}

  private:
    std::vector<std::unique_ptr<DATA>> items_, popped_;
};

int32_t CheckAndCreateEffect(const char *id, float x, float y, float z, float rx, float ry, float rz,
                             int32_t lifetime, bool direct)
{
    Stack stack;
    stack.Push()->Set(id);
    for (float v : {x, y, z, rx, ry, rz})
        stack.Push()->Set(v);
    stack.Push()->Set(lifetime);
    stack.Push()->Set(static_cast<int32_t>(direct));
    natives::CheckAndCreateEffect(&stack);
    Check(stack.GetDataNum() == 1, "CheckAndCreateEffect consumes 9 arguments and returns one value");
    return stack.Pop()->GetInt();
}

int32_t ById(uint32_t (*native)(VS_STACK *), const char *id)
{
    Stack stack;
    stack.Push()->Set(id);
    native(&stack);
    return stack.Pop()->GetInt();
}

int32_t Handle(uint64_t result)
{
    return static_cast<int32_t>(result);
}

void InitCore()
{
    auto *core_private = static_cast<CorePrivate *>(&core);
    core_private->Init();
    core_private->InitBase();
}

bool InitBgfx()
{
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    init.swapChain.width = 0;
    init.swapChain.height = 0;
    init.swapChain.nwh = nullptr;
    init.reset = BGFX_RESET_NONE;
    if (!bgfx::init(init))
        return false;
    const bgfx::Caps *caps = bgfx::getCaps();
    efk::AttachDevice({caps->originBottomLeft, caps->homogeneousDepth});
    return true;
}

void Frame(entid_t effects, uint32_t ms)
{
    core.GetEntityPointer(effects)->ProcessStage(Entity::Stage::execute, ms);
    bgfx::frame();
}

bool InLayer(layer_index_t layer, entid_t id)
{
    for (const auto entity : core.GetEntityIds(layer))
        if (entity == id)
            return true;
    return false;
}

int Synthetic()
{
    const fs::path game = fs::temp_directory_path() / ("storm-effects-check-" + std::to_string(::getpid()));
    fs::create_directories(game / "Resource" / "Effects");
    std::ofstream(game / "Resource" / "Effects" / "effects.json") << R"({
        "___SECTION___": "",
        "test_flame": {"file": "TestFlame.xps", "speed": 1.0},
        "Test_Glow":  {"file": "TestGlowMissing.efkefc", "speed": 0.5},
        "test_bad":   {"file": "picture.png"}
    })";
    fs::current_path(game);

    // Before the entities exist: nothing to create with, handles are -1 and nothing crashes.
    Check(CheckAndCreateEffect("test_flame", 0, 0, 0, 0, 0, 0, 0, false) == -1,
          ".xps id without a particles entity: -1");
    Check(CheckAndCreateEffect("test_glow", 0, 0, 0, 0, 0, 0, 0, false) == -1,
          ".efkefc id without an Effects entity: -1");

    const auto particles = core.CreateEntity("particles");
    const auto effects = core.CreateEntity("Effects");
    Check(effects != invalid_entity, "core creates 'Effects'");
    Check(InLayer(EXECUTE, effects) && InLayer(SEA_EXECUTE, effects) && InLayer(REALIZE, effects) &&
              InLayer(SEA_REALIZE, effects),
          "Effects adds itself to the land and sea execute/realize layers");

    // Ids are case-insensitive; .xps ids go to the particles entity with the file name.
    particle_requests.clear();
    Check(CheckAndCreateEffect("TEST_FLAME", 1, 2, 3, 0.1f, 0.2f, 0.3f, 500, false) == -1, ".xps id returns -1");
    Check(particle_requests.size() == 1 && particle_requests[0].code == PS_CREATE &&
              particle_requests[0].name == "TestFlame.xps" && particle_requests[0].values[0] == 1.0f &&
              particle_requests[0].values[5] == 0.3f && particle_requests[0].lifetime == 500,
          ".xps id -> PS_CREATE TestFlame.xps with position, angles and lifetime");
    CheckAndCreateEffect("test_flame", 1, 2, 3, 0, 0, 1, 0, true);
    Check(particle_requests.size() == 2 && particle_requests[1].code == PS_CREATEX &&
              particle_requests[1].values[5] == 1.0f,
          "isDirect .xps id -> PS_CREATEX with the direction vector");
    // Names outside the table are legacy particle systems (TEHO scripts, CL quests).
    CheckAndCreateEffect("torch", 0, 0, 0, 0, 0, 0, 0, false);
    Check(particle_requests.size() == 3 && particle_requests[2].name == "torch",
          "id outside effects.json -> particle system of that name");
    CheckAndCreateEffect("test_bad", 0, 0, 0, 0, 0, 0, 0, false);
    Check(particle_requests.size() == 4 && particle_requests[3].name == "test_bad",
          "entry with an unknown file type is dropped from the table (logged)");

    // A missing .efkefc: logged, -1, through the native and through MSG_EFFECT_PLAY.
    Check(CheckAndCreateEffect("test_glow", 0, 0, 0, 0, 0, 0, 0, false) == -1, "missing .efkefc file: -1");
    Check(Handle(core.Send_Message(effects, "lsfff", MSG_EFFECT_PLAY, "test_glow", 0.f, 0.f, 0.f)) == -1,
          "MSG_EFFECT_PLAY of a missing file: -1");
    Check(Handle(core.Send_Message(effects, "lsfff", MSG_EFFECT_PLAY, "nope", 0.f, 0.f, 0.f)) == -1,
          "MSG_EFFECT_PLAY of an unknown id: -1");
    Check(Handle(core.Send_Message(effects, "lsfff", MSG_EFFECT_PLAY, "test_flame", 0.f, 0.f, 0.f)) == -1,
          "MSG_EFFECT_PLAY of a particle system id: -1");
    Check(Handle(core.Send_Message(effects, "lsfff", MSG_EFFECT_PLAY_FILE, "resource/effects/none.efkefc", 0.f, 0.f,
                                   0.f)) == -1,
          "MSG_EFFECT_PLAY_FILE of a missing file: -1");
    // Messages on an invalid handle are harmless; the query ones answer neutral values.
    Check(core.Send_Message(effects, "ll", MSG_EFFECT_IS_PLAYING, -1) == 0, "IS_PLAYING(-1) == 0");
    DATA speed;
    speed.Set(5.0f);
    core.Send_Message(effects, "lle", MSG_EFFECT_GET_SPEED, -1, &speed);
    core.Send_Message(effects, "ll", MSG_EFFECT_DELETE, -1);
    core.Send_Message(effects, "l", MSG_EFFECT_DELETE_ALL);
    Check(ById(natives::ReloadEffect, "test_flame") == 0, "ReloadEffect of a particle system id: 0");
    Check(ById(natives::UnloadEffect, "test_glow") == 0, "UnloadEffect of an effect that is not loaded: 0");
    Stack none;
    natives::ReloadAllEffects(&none);
    natives::UnloadAllEffects(&none);
    Check(storm::effects::Catalog().Records().size() == 2, "ReloadAllEffects re-reads the table (2 valid ids)");

    core.EraseEntity(effects);
    core.EraseEntity(particles);
    fs::current_path(fs::temp_directory_path());
    fs::remove_all(game);
    return 0;
}

int Game(const fs::path &game)
{
    fs::current_path(game);
    const auto effects = core.CreateEntity("Effects");
    // Ids of the shipped table with small textures (big PNG atlases take seconds to decode):
    // rain splashes (speed 1.0) and a lightning hit (speed 0.1).
    const int32_t splash = CheckAndCreateEffect("land_splash", 0, 0, 0, 0, 0, 0, 0, false);
    const int32_t fire = CheckAndCreateEffect("lighting_hit", 5, 0, 0, 0, 1, 0, 300, false);
    Check(splash >= 0 && fire >= 0 && splash != fire, "CheckAndCreateEffect plays .efkefc ids and returns handles");
    DATA value;
    core.Send_Message(effects, "lle", MSG_EFFECT_GET_SPEED, fire, &value);
    Check(std::abs(value.GetFloat() - 0.1f) < 1e-6f, "speed of effects.json is applied (lighting_hit 0.1)");
    core.Send_Message(effects, "llf", MSG_EFFECT_SET_SPEED, fire, 2.0f);
    core.Send_Message(effects, "lle", MSG_EFFECT_GET_SPEED, fire, &value);
    Check(value.GetFloat() == 2.0f, "SET_SPEED / GET_SPEED");

    core.Send_Message(effects, "llfff", MSG_EFFECT_UPDATE_POS, splash, 1.f, 2.f, 3.f);
    core.Send_Message(effects, "llfff", MSG_EFFECT_ADD_SHIFT, splash, 1.f, 1.f, 1.f);
    DATA x, y, z;
    core.Send_Message(effects, "lleee", MSG_EFFECT_GET_POS, splash, &x, &y, &z);
    Check(x.GetFloat() == 2.f && y.GetFloat() == 3.f && z.GetFloat() == 4.f, "UPDATE_POS + ADD_SHIFT + GET_POS");
    core.Send_Message(effects, "llfff", MSG_EFFECT_UPDATE_ROT, splash, 0.f, 1.f, 0.f);
    core.Send_Message(effects, "llfff", MSG_EFFECT_SET_SCALE, splash, 2.f, 2.f, 2.f);
    core.Send_Message(effects, "llfff", MSG_EFFECT_SET_TARGET_POS, splash, 0.f, 0.f, 10.f);
    core.Send_Message(effects, "lll", MSG_EFFECT_SET_COLOR, splash, static_cast<int32_t>(0x80ff8040));
    core.Send_Message(effects, "lllf", MSG_EFFECT_SET_PARAMETER, splash, 0, 0.25f);
    core.Send_Message(effects, "llle", MSG_EFFECT_GET_PARAMETER, splash, 0, &value);
    Check(value.GetFloat() == 0.25f, "SET_PARAMETER / GET_PARAMETER (dynamic input)");
    core.Send_Message(effects, "lll", MSG_EFFECT_SEND_TRIGGER, splash, 0);
    core.Send_Message(effects, "lll", MSG_EFFECT_PAUSE, splash, 1);
    core.Send_Message(effects, "lll", MSG_EFFECT_PAUSE, splash, 0);

    // MSG_EFFECT_PLAY with angles, scale and start frame 0; PLAY_FILE with a game path.
    const auto played = Handle(core.Send_Message(effects, "lsfffffffffl", MSG_EFFECT_PLAY, "lighting_hit", 0.f, 0.f, 0.f,
                                                 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 0));
    const auto by_file = Handle(core.Send_Message(effects, "lsfff", MSG_EFFECT_PLAY_FILE,
                                                  "resource/effects/CLDropsRain.efkefc", 0.f, 0.f, 0.f));
    Check(played >= 0 && by_file >= 0, "MSG_EFFECT_PLAY (12 params) and MSG_EFFECT_PLAY_FILE return handles");

    for (int i = 0; i < 5; ++i)
        Frame(effects, 100);
    Check(core.Send_Message(effects, "ll", MSG_EFFECT_IS_PLAYING, splash) == 1, "splash plays after 0.5 s");
    core.Send_Message(effects, "ll", MSG_EFFECT_DELETE, splash);
    Check(core.Send_Message(effects, "ll", MSG_EFFECT_IS_PLAYING, splash) == 0, "DELETE removes the instance");
    core.Send_Message(effects, "ll", MSG_EFFECT_STOP_ROOT, played);

    Check(ById(natives::ReloadEffect, "land_splash") == 1, "ReloadEffect of a loaded effect: 1");
    Check(ById(natives::UnloadEffect, "land_splash") == 1, "UnloadEffect of a loaded effect: 1");
    Check(ById(natives::UnloadEffect, "land_splash") == 0, "UnloadEffect again: 0");
    core.Send_Message(effects, "l", MSG_EFFECT_DELETE_ALL);
    Frame(effects, 16); // Effekseer removes stopped instances on its next update
    Check(efk::GetStats().instances == 0, "DELETE_ALL stops every instance");
    Stack none;
    natives::UnloadAllEffects(&none);
    Check(efk::GetStats().effects == 0, "UnloadAllEffects releases every effect");
    core.EraseEntity(effects);
    return 0;
}
} // namespace

// The scripts' particles entity, reduced to recording what it is asked to create.
class particles final : public Entity
{
  public:
    bool Init() override { return true; }
    void ProcessStage(Stage, uint32_t) override {}
    uint64_t ProcessMessage(MESSAGE &message) override
    {
        ParticleRequest request{};
        request.code = message.Long();
        request.name = message.String();
        for (float &v : request.values)
            v = message.Float();
        request.lifetime = message.Long();
        particle_requests.push_back(request);
        return 0;
    }
};
CREATE_CLASS(particles)

int main(int argc, char **argv)
{
    const auto started = std::chrono::steady_clock::now();
    std::setvbuf(stdout, nullptr, _IONBF, 0); // in order with the engine's log lines
    // The core's natives read the game profile; the EFFECTS_LIBRARY contract checked here is Caribbean Legend's.
    storm::setGameProfile(storm::GameProfile::CaribbeanLegend);
    if (argc > 1 && !fs::is_directory(argv[1]))
    {
        std::printf("SKIP: no game directory %s\n", argv[1]);
        return 77;
    }
    InitCore();
    if (!InitBgfx())
    {
        std::printf("FAIL: bgfx Noop init\n");
        return 1;
    }
    try
    {
        argc > 1 ? Game(fs::absolute(argv[1])) : Synthetic();
    }
    catch (const std::exception &error)
    {
        std::printf("FAIL: exception: %s\n", error.what());
        ++failures;
    }
    efk::DetachDevice();
    bgfx::shutdown();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("%s: %d failure(s), %.2f s\n", failures ? "FAIL" : "PASS", failures, seconds);
    return failures ? 1 : 0;
}
