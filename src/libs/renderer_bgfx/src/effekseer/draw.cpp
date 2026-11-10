// The game-side entry of the Effekseer system: draws into the shim's current render target.
#include "../shim/device.hpp"
#include "storm/render/effekseer.hpp"
#include "storm/render/effekseer_device.hpp"

namespace storm::render::effekseer
{
void Draw(IDirect3DDevice9 *device, const float view[16], const float projection[16])
{
    if (!device || !shim::Context::instance().ready() || GetStats().instances == 0)
        return;
    // Effects are blended over the scene in its own view, after everything the game submitted so
    // far (the opaque scene) and before the interface, which opens later views.
    const uint16_t pass = static_cast<shim::Device *>(device)->begin_external_pass();
    DrawToView(pass, view, projection);
}
} // namespace storm::render::effekseer
