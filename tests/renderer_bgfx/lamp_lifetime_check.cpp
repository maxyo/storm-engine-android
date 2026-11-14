// CPU lifetime regression: the game releases texture wrappers after reporting geometry, while lamp caches still
// need their alpha textures. No window/GPU: Noop only supplies buffer handles for reported ship geometry.
#include "effects/lamp_shadows.hpp"
#include "shim/device.hpp"
#include "shim/shim.hpp"
#include "storm/fx/cutout.hpp"

#include <cstdio>
#include <memory>

using namespace storm;

namespace
{
int failures = 0;
void expect(bool pass, const char *what)
{
    std::printf("%s: %s\n", pass ? "PASS" : "FAIL", what);
    failures += !pass;
}
shim::Texture2D *make_cutout_texture()
{
    auto store = shim::make_store(nullptr, shim::TexKind::Tex2D, 2, 2, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
    store->cutout = std::make_shared<fx::AlphaMask>();
    return new shim::Texture2D(std::move(store));
}
}

int main()
{
    if (!shim::Context::instance().init_headless(64, 64))
        return 2;
    D3DPRESENT_PARAMETERS params{};
    params.BackBufferWidth = params.BackBufferHeight = 64;
    auto *device = new shim::Device(nullptr, params);
    {
        effects::LampShadows lamps;
        effects::Casters casters;
        const std::array<fx::Vec3, 4> positions{{{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}}};
        const std::array<std::array<float, 2>, 4> uvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        const std::array<uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        auto *first = make_cutout_texture();
        std::weak_ptr<shim::TexStore> old = first->store;
        render::EffectsLocationGeometry::Surface surface{0, 3, first};
        render::EffectsLocationGeometry location;
        location.positions = positions;
        location.uvs = uvs;
        location.indices = indices;
        location.surfaces = std::span(&surface, 1);
        lamps.set_geometry(location);
        first->Release();
        expect(!old.expired(), "location keeps storage after game releases texture wrapper");

        // Same vertex/index arrays and same number of cutout indices, but a new texture. A count-only comparison
        // silently retained the old surface (and its dangling wrapper) here.
        auto *second = make_cutout_texture();
        std::weak_ptr<shim::TexStore> current = second->store;
        surface.texture = second;
        lamps.set_geometry(location);
        expect(old.expired(), "same-geometry texture replacement releases old storage");
        lamps.set_lamps({fx::Lamp{{0, 3, 0}}});
        const auto layout = lamps.layout();
        surface.first_index = 3;
        lamps.set_geometry(location);
        lamps.set_lamps({fx::Lamp{{0, 3, 0}}});
        expect(lamps.layout() != layout, "moving a surface run with the same count rebuilds lamp geometry");
        second->Release();
        expect(!current.expired(), "replacement storage survives its wrapper");
        lamps.set_geometry({});
        expect(current.expired(), "clearing location geometry releases storage");

        auto *ship_texture = make_cutout_texture();
        std::weak_ptr<shim::TexStore> ship_old = ship_texture->store;
        surface = {0, 6, ship_texture};
        lamps.set_ship_geometry(7, 1, {positions, uvs, indices, std::span(&surface, 1)});
        ship_texture->Release();
        expect(!ship_old.expired(), "ship keeps storage after game releases texture wrapper");
        ship_texture = make_cutout_texture();
        std::weak_ptr<shim::TexStore> ship_current = ship_texture->store;
        surface.texture = ship_texture;
        lamps.set_ship_geometry(7, 2, {positions, uvs, indices, std::span(&surface, 1)});
        ship_texture->Release();
        expect(ship_old.expired() && !ship_current.expired(), "ship signature replacement drops old storage and keeps new");
        lamps.set_ship_geometry(7, 3, {});
        expect(ship_current.expired(), "clearing ship geometry releases storage");

        // Populate the per-lantern slot as well as the ship cache. Replacing it with empty geometry must drop
        // both owners immediately; otherwise the obsolete alpha texture survives until that lantern leaves.
        lamps.set_lamps({});
        fx::Settings settings;
        settings.lamp_shadows = 2;
        settings.lamp_moving = 1;
        lamps.configure(settings);
        render::EffectsShip ship;
        ship.id = 7;
        ship.signature = 4;
        ship.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const fx::Lamp lantern{{0, 3, 0}};
        lamps.set_ship_lights({ship}, {{lantern, ship.id, lantern.position}});
        auto *slotted = make_cutout_texture();
        std::weak_ptr<shim::TexStore> in_slot = slotted->store;
        surface = {0, 6, slotted};
        lamps.set_ship_geometry(7, 4, {positions, uvs, indices, std::span(&surface, 1)});
        lamps.prepare(*device, {0, 3, 0}, casters);
        lamps.end_frame();
        expect(lamps.last_slots() > 0 && lamps.last_static_faces() == 6, "ship lantern submitted its six cached faces on Noop");
        slotted->Release();
        expect(!in_slot.expired(), "drawn ship storage survives its texture wrapper");
        lamps.set_ship_geometry(7, 5, {});
        expect(in_slot.expired(), "empty ship report releases storage held by its drawn slot");

        auto *last = make_cutout_texture();
        std::weak_ptr<shim::TexStore> ending = last->store;
        surface = {0, 6, last};
        lamps.set_geometry(location);
        lamps.set_ship_geometry(7, 4, {positions, uvs, indices, std::span(&surface, 1)});
        last->Release();
        expect(!ending.expired(), "both geometry caches retain their shared storage");
        lamps.shutdown();
        expect(ending.expired(), "renderer shutdown releases all retained storage");
        casters.shutdown();
    }
    device->Release();
    return failures ? 1 : 0;
}
