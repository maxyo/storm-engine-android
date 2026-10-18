#pragma once
#include "mobile_image_cache.hpp"
class VXSERVICE;
namespace storm::mobile
{
ImageRef Reference(Descriptor descriptor, uint32_t tint = 0xffffffff);
ImageRef AtlasReference(VXSERVICE *service, const char *group, const char *image, uint32_t tint = 0xffffffff);
// atlas is the resolved battle texture identity, never the local texNum slot.
inline ImageRef WithBattleIconSource(ImageRef reference, const char *mode, const char *atlas, int pic)
{
    if (reference.handle && mode && *mode && atlas && *atlas && pic >= 0)
        reference.source = {"battle_icon", {}, {}, mode, atlas, pic};
    return reference;
}
void PumpImages();
void ResetImages();
} // namespace storm::mobile
