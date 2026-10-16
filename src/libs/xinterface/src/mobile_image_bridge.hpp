#pragma once
#include "mobile_image_cache.hpp"
class VXSERVICE;
namespace storm::mobile
{
ImageRef Reference(Descriptor descriptor, uint32_t tint = 0xffffffff);
ImageRef AtlasReference(VXSERVICE *service, const char *group, const char *image, uint32_t tint = 0xffffffff);
void PumpImages();
void ResetImages();
} // namespace storm::mobile
