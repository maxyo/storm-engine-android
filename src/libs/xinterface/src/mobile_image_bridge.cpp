#include "mobile_image_bridge.hpp"
#include "vx_service.h"
#include "xdefines.h"
#ifdef __ANDROID__
#include "platform/android_ui.hpp"

namespace storm::mobile
{
namespace
{
struct Bridge
{
    ImageCache cache;
    void sync()
    {
        if (cache.screenRevision() != storm::android::mobileScreenRevision)
            cache.screen(storm::android::mobileScreenRevision);
    }
};
Bridge &bridge()
{
    static Bridge value;
    return value;
}
} // namespace
ImageRef Reference(Descriptor descriptor, uint32_t tint)
{
    auto &b = bridge();
    b.sync();
    return b.cache.reference(std::move(descriptor), tint);
}
ImageRef AtlasReference(VXSERVICE *service, const char *group, const char *image, uint32_t tint)
{
    std::string path;
    FXYRECT uv;
    int32_t width = 0, height = 0;
    if (!service || !service->ResolveImageSource(group, image, path, uv, width, height))
        return {};
    auto reference = Reference({std::move(path), {uv.left, uv.top, uv.right, uv.bottom}, uint32_t(width), uint32_t(height)},
                               tint);
    if (reference.handle && group && image)
        reference.source = {"atlas", group, image, {}, {}, -1};
    return reference;
}
void ResetImages()
{
    bridge().sync();
}
} // namespace storm::mobile
#else
namespace storm::mobile
{
ImageRef Reference(Descriptor, uint32_t)
{
    return {};
}
ImageRef AtlasReference(VXSERVICE *, const char *, const char *, uint32_t)
{
    return {};
}
void ResetImages()
{
}
} // namespace storm::mobile
#endif
