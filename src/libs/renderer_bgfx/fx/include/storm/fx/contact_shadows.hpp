// Soft contact shadows: a dark, soft-edged blob on the ground under each character and object. They anchor
// things to the floor where the sun shadow map does not reach or the sun is not out (interiors, overcast
// days, nights), for the price of one small quad each.
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/sky_model.hpp"

#include <bgfx/bgfx.h>

#include <vector>

namespace storm::fx
{

class ContactShadows
{
  public:
    ContactShadows() = default;
    ~ContactShadows();
    ContactShadows(const ContactShadows &) = delete;
    ContactShadows &operator=(const ContactShadows &) = delete;

    // `ground`: the point on the floor under the object (a character's feet). `radius` in metres (a
    // character: 0.45), `darkness` 0..1 at the centre (0.5 is a good default; lower it in direct sun where
    // the shadow map already draws a shadow).
    void Add(Vec3 ground, float radius, float darkness);
    // Draws and clears the list. `view` is the scene view (frame buffer with depth, view and projection set);
    // submit after the opaque geometry and before transparent effects. Does nothing unless
    // settings.contact_shadows is on.
    void Submit(bgfx::ViewId view, const Settings &settings);

    void Shutdown();
    void OnDeviceLost();

  private:
    struct Blob
    {
        Vec3 ground;
        float radius;
        float darkness;
    };
    std::vector<Blob> blobs_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
