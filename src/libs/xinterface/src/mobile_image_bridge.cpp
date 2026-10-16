#include "mobile_image_bridge.hpp"
#include "vx_service.h"
#include "xdefines.h"
#ifdef __ANDROID__
#include "platform/android_ui.hpp"
#include "v_file_service.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <map>

namespace storm::mobile
{
namespace
{
using Request = storm::android::MobileImageRequest;
struct Completed
{
    Request request{};
    Descriptor descriptor;
    std::shared_ptr<const Pixels> source, output;
    std::string error;
};
struct Bridge
{
    ImageCache cache;
    std::shared_ptr<std::atomic<uint64_t>> epoch = std::make_shared<std::atomic<uint64_t>>(0);
    std::deque<Request> pending;
    std::future<Completed> work;
    Request reading{};
    Descriptor descriptor;
    std::fstream file;
    std::vector<uint8_t> encoded;
    size_t offset = 0;
    std::string extension;
    std::map<uint64_t, std::string> failures;
    void close()
    {
        if (file.is_open())
            file.close();
        file.clear();
        std::vector<uint8_t>().swap(encoded);
        offset = 0;
        reading = {};
    }
    void sync()
    {
        if (cache.screenRevision() != storm::android::mobileScreenRevision)
        {
            close();
            pending.clear();
            failures.clear();
            cache.screen(storm::android::mobileScreenRevision);
            epoch->store(cache.screenRevision());
        }
    }
    void reply(const Request &r, const std::shared_ptr<const Pixels> &p, const std::string &error = {})
    {
        if (r.screenRevision != int64_t(cache.screenRevision()))
            return;
        static const std::vector<uint8_t> empty;
        storm::android::publishMobileImage(r, p ? int(p->width) : 0, p ? int(p->height) : 0, p ? p->rgba : empty,
                                           error);
    }
    void launch(Request r, Descriptor d, std::shared_ptr<const Pixels> source, std::vector<uint8_t> bytes = {},
                std::string ext = {})
    {
        work =
            std::async(std::launch::async, [r, d = std::move(d), source = std::move(source), bytes = std::move(bytes),
                                            ext = std::move(ext), epoch = epoch]() mutable {
                Completed result;
                result.request = r;
                result.descriptor = d;
                try
                {
                    if (!source)
                    {
                        auto decoded = Decode(bytes, ext);
                        if (!decoded)
                            result.error = decoded.error;
                        else
                            source = std::make_shared<Pixels>(std::move(decoded.pixels));
                    }
                    if (source)
                    {
                        result.source = source;
                        auto crop = CropResize(*source, d.uv, uint32_t(r.maxEdge));
                        if (!crop)
                            result.error = crop.error;
                        else
                            result.output = std::make_shared<Pixels>(std::move(crop.pixels));
                    }
                }
                catch (const std::exception &)
                {
                    result.error = "image worker allocation failed";
                }
                // A future implementation may retain the callable until get(); do not
                // leave large buffers in its captures while the interface is closed.
                source.reset();
                std::vector<uint8_t>().swap(bytes);
                if (epoch->load() != uint64_t(r.screenRevision))
                {
                    result.source.reset();
                    result.output.reset();
                }
                return result;
            });
    }
    void pump()
    {
        sync();
        if (work.valid() && work.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto result = work.get();
            const auto &r = result.request;
            if (r.screenRevision == int64_t(cache.screenRevision()))
            {
                cache.putPreview(ImageCache::sourceKey(result.descriptor), result.source,
                                 ImageCache::outputKey(r.handle, uint32_t(r.maxEdge)), result.output);
                if (!result.error.empty())
                    failures[r.handle] = result.error;
                reply(r, result.output, result.error);
            }
        }
        if (storm::android::mobileScreen.empty())
            return;
        // Never drain Java into an unbounded native queue. At most four polls per frame.
        for (const auto &r : storm::android::pollMobileImageRequests(16 - pending.size()))
        {
            if (r.screenRevision != int64_t(cache.screenRevision()))
                continue;
            if (r.requestId <= 0 || r.handle <= 0 || r.imageRevision <= 0 || r.maxEdge < 1 || r.maxEdge > 512 ||
                !cache.descriptor(r.handle, r.imageRevision))
            {
                reply(r, {}, "invalid image request");
                continue;
            }
            pending.push_back(r);
        }
        if (work.valid())
            return;
        if (!file.is_open() && !pending.empty())
        {
            const auto r = pending.front();
            pending.pop_front();
            if (r.activityRevision != storm::android::imageActivityRevision)
                return;
            const auto *d = cache.descriptor(r.handle, r.imageRevision);
            if (!d)
                return;
            if (auto p = cache.get(ImageCache::outputKey(r.handle, uint32_t(r.maxEdge))))
            {
                reply(r, p);
                return;
            }
            if (auto it = failures.find(r.handle); it != failures.end())
            {
                reply(r, {}, it->second);
                return;
            }
            if (auto source = cache.get(ImageCache::sourceKey(*d)))
            {
                launch(r, *d, std::move(source));
                return;
            }
            reading = r;
            descriptor = *d;
            // Match the engine's TX-first resource convention, using its VFS only.
            auto path = d->path;
            const bool tx = path.size() >= 3 && path.substr(path.size() - 3) == ".tx";
            auto txPath = tx ? path : path + ".tx";
            file = fio->_CreateFile(txPath.c_str(), std::ios::binary | std::ios::in);
            extension = "tx";
            if (!file.is_open() && !tx)
            {
                file.clear();
                file = fio->_CreateFile(path.c_str(), std::ios::binary | std::ios::in);
                extension = "raw";
            }
            if (!file.is_open())
            {
                failures[r.handle] = "texture unavailable";
                reply(r, {}, failures[r.handle]);
                close();
                return;
            }
            file.seekg(0, std::ios::end);
            const auto size = file.tellg();
            file.seekg(0, std::ios::beg);
            if (size <= 0 || uint64_t(size) > MaxEncodedBytes || !file)
            {
                failures[r.handle] = "invalid texture size";
                reply(r, {}, failures[r.handle]);
                close();
                return;
            }
            encoded.resize(size_t(size));
            offset = 0;
        }
        if (file.is_open())
        {
            // One bounded read/frame. No worker touches FILE_SERVICE or its path indexes.
            const size_t count = std::min(size_t(128 * 1024), encoded.size() - offset);
            if (!fio->_ReadFile(file, encoded.data() + offset, std::streamsize(count)))
            {
                failures[reading.handle] = "texture read failed";
                reply(reading, {}, failures[reading.handle]);
                close();
                return;
            }
            offset += count;
            if (offset == encoded.size())
            {
                const auto r = reading;
                auto d = descriptor;
                auto bytes = std::move(encoded);
                auto ext = extension;
                close();
                launch(r, std::move(d), {}, std::move(bytes), std::move(ext));
            }
        }
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
    return Reference({std::move(path), {uv.left, uv.top, uv.right, uv.bottom}, uint32_t(width), uint32_t(height)},
                     tint);
}
void ResetImages()
{
    bridge().sync();
}
void PumpImages()
{
    auto &b = bridge();
    try
    {
        b.pump();
    }
    catch (const std::exception &)
    {
        if (b.reading.requestId)
            b.reply(b.reading, {}, "image IO allocation failed");
        b.close();
    }
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
void PumpImages()
{
}
void ResetImages()
{
}
} // namespace storm::mobile
#endif
