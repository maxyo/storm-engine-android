// One bounce of indirect light in the current location (fx/bounce.hpp), on Средняя and Высокая (fx_bounce).
//
// The place's drawn still geometry (render::SetEffectsLocationGeometry: its triangles, texture coordinates and textures)
// and the visibility volume's grid make the scene: surfels with their textures' colours and the rays of a lattice over
// the cells, read from the texture cache directory (the app's cache: an entry that is gone is baked again, and said)
// or baked on the bake thread (fx::PostBake). Then, there too:
//   * the sun's light on the surfels whenever the hour's sun has moved by kSunStepDegrees (in a town our own town
//     light bakes it with its lightmaps' sun, TownLight::sun_bounce, so both step together), for a white sun: the
//     weather's colour scales it when it is written;
//   * the lamps' light on the surfels (LampShadows' lamps, their falloff and shadows) whenever they are laid out anew;
//   * the field gathered from each and written into the visibility texture's second block (Visibility::set_bounce).
// The field's lamp share (lamps over all) goes with it: baked surfaces take only the bounce of the lamp light the
// relight adds beyond their bake (ff_fragment.sh ffBounce).
#pragma once

#include "effects/visibility.hpp"

#include "storm/fx/bakes.hpp"
#include "storm/fx/bounce.hpp"
#include "storm/render/effects.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace storm::effects
{

class Bounce
{
  public:
    struct Scene
    {
        fx::BounceScene scene;
        std::shared_ptr<const fx::Occluders> occluders; // the drawn geometry, its textures' holes cut
    };

    void set_enabled(bool enabled);
    // Main thread: the place's drawn still geometry (no triangles: no place).
    void set_geometry(const render::EffectsLocationGeometry &geometry);
    // Main thread, once per frame with what Visibility::update gave (null: no volume yet). `sun`: towards the hour's sun, `sun_color` the weather's,
    // `sun_present` false at night and indoors; `town_sun`: our town light bakes the sun's field (sun_field()).
    struct Light
    {
        std::array<float, 3> sun{0.0f, 1.0f, 0.0f};
        std::array<float, 3> sun_color{0.0f, 0.0f, 0.0f};
        bool sun_present = false;
        const std::vector<fx::Lamp> *lamps = nullptr;
        const std::vector<float> *holders = nullptr;
        uint32_t lamp_layout = 0; // LampShadows::layout(): changes when the lamps are laid out anew
        bool town_sun = false;
        std::shared_ptr<const fx::BounceField> town_field; // with town_sun: the sun's field (null: not yet)
    };
    void update(Visibility &visibility, const Visibility::Bound *bound, const Light &light);
    // The scene (null until it is ready): our town light bakes the sun's field from it with its own sun steps.
    std::shared_ptr<const Scene> scene() const { return scene_; }

    void shutdown();

  private:
    struct SceneJob
    {
        std::atomic<int> state{0}; // 0 running, 1 ready, 2 failed
        std::shared_ptr<const Scene> scene;
        std::string how;
    };
    struct LightJob
    {
        std::atomic<int> state{0};
        std::shared_ptr<const fx::BounceField> sun, lamps;
        std::vector<uint8_t> directions, block; // the visibility block's B / A, the bounce block
        float lamp_share = 0.0f;
        float mean = 0.0f, p99 = 0.0f; // c's luminance over the cells
        double sun_ms = 0.0, lamps_ms = 0.0, write_ms = 0.0;
    };
    void restart();
    void start_scene(const fx::BounceGrid &grid);

    bool enabled_ = false;
    std::string name_;
    std::vector<fx::Vec3> positions_;
    std::vector<std::array<float, 2>> uvs_;
    std::vector<uint32_t> indices_, triangle_texture_;
    std::vector<std::shared_ptr<const fx::BounceTexture>> textures_; // TexStore::albedo, null: unreadable
    std::vector<fx::Occluders::Cut> cut_;
    uint32_t unreadable_textures_ = 0;
    fx::BounceGrid grid_{};
    fx::Bake<SceneJob> scene_job_;
    std::shared_ptr<const Scene> scene_;
    bool failed_ = false;
    fx::Bake<LightJob> light_job_;
    std::shared_ptr<const fx::BounceField> sun_field_, lamp_field_;
    std::array<float, 3> sun_baked_{0.0f, 0.0f, 0.0f}; // the direction sun_field_ is for (0: none)
    std::array<float, 3> color_written_{-1.0f, -1.0f, -1.0f};
    uint32_t lamps_baked_ = 0xffffffffu; // the lamps' layout lamp_field_ is for (LampShadows::layout)
    unsigned generation_ = 0;
};

} // namespace storm::effects
