// libbgfx.so (Android): bgfx, bx and bimg are linked in whole (CMakeLists.txt, storm_bgfx_runtime). Of bimg_encode
// only the encoder entry point the engine calls (src/texture/core/formats.cpp) is kept: this reference pulls it
// and what they reach out of the static library, and keeps it exported for libengine.so.
#include <bimg/encode.h>

namespace
{
[[gnu::used]] const void *const kBimgEncodeEntryPoints[] = {
    reinterpret_cast<const void *>(&bimg::imageEncodeFromRgba8),
};
} // namespace
