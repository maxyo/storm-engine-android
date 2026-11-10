// Effect textures (PNG in Caribbean Legend) through the same pipeline as the game's DXT textures:
// storm::texture::Service transcodes to ETC2 on its workers and keeps the result in the on-disk
// cache, so an image is decoded once per machine and later loads upload prebuilt mips directly.
#include "textures.hpp"

#include "bgfxrenderer.h"
#include "cache.hpp"
#include "service.hpp"

#include <bgfx/bgfx.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <cstring>
#include <memory>
#include <vector>

namespace storm::render::effekseer
{
namespace
{
namespace tex = storm::texture;

struct Pending
{
    std::shared_ptr<tex::Job> job; // null while the service queue is full: submitted from PollTextures
    Effekseer::Backend::TextureRef texture; // keeps the backend alive until the swap
    std::string name;
    tex::Layout layout;
    uint64_t key = 0;
    tex::EtcFormat format = tex::EtcFormat::Rgba8;
    std::vector<uint8_t> payload; // RGBA8 mips, kept only until the job is submitted
};

struct State
{
    std::vector<Pending> pending;
    TextureStats stats;
    LogFn log = nullptr;
};

State &state()
{
    static State s;
    return s;
}

uint32_t read_be32(const uint8_t *p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

// PNG size from the IHDR chunk, so a cache hit needs no decode. False for other formats.
bool png_size(const std::vector<uint8_t> &file, uint32_t &width, uint32_t &height)
{
    static constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (file.size() < 24 || memcmp(file.data(), kSignature, 8) != 0 || memcmp(file.data() + 12, "IHDR", 4) != 0)
        return false;
    width = read_be32(file.data() + 16);
    height = read_be32(file.data() + 20);
    return width > 0 && height > 0;
}

bool etc2_supported()
{
    const bgfx::Caps *caps = bgfx::getCaps();
    const uint16_t needed = BGFX_CAPS_FORMAT_TEXTURE_2D;
    return (caps->formats[bgfx::TextureFormat::ETC2] & needed) && (caps->formats[bgfx::TextureFormat::ETC2A] & needed);
}

bgfx::TextureHandle create_from_ktx(const tex::Blob &ktx, const char *name)
{
    const bgfx::TextureHandle handle = bgfx::createTexture(bgfx::copy(ktx.data.get(), uint32_t(ktx.size)));
    if (bgfx::isValid(handle))
        bgfx::setName(handle, name);
    return handle;
}

// Hands the payload to the transcode service; while its queue is full the payload stays here and
// PollTextures tries again (the texture is shown as RGBA8 meanwhile).
void submit(Pending &pending)
{
    tex::Service &service = tex::Service::instance();
    if (!service.has_room())
        return;
    std::vector<uint8_t> payload = std::move(pending.payload);
    pending.payload.clear();
    pending.job = service.submit(pending.layout, pending.key, pending.format, std::move(payload));
    if (!pending.job)
        state().log("ERROR: the texture service refused %s with room in its queue; it stays RGBA8",
                    pending.name.c_str());
}

class CachedTextureLoader final : public Effekseer::TextureLoader
{
  public:
    CachedTextureLoader(EffekseerRenderer::RendererRef renderer, Effekseer::FileInterfaceRef files)
        : renderer_(std::move(renderer)), files_(std::move(files))
    {
    }

    Effekseer::TextureRef Load(const char16_t *path, Effekseer::TextureType) override
    {
        char name[1024];
        Effekseer::ConvertUtf16ToUtf8(name, sizeof(name), path);
        auto reader = files_->OpenRead(path);
        if (reader == nullptr)
            return nullptr; // EngineFiles logged it
        std::vector<uint8_t> file(reader->GetLength());
        if (reader->Read(file.data(), file.size()) != file.size())
        {
            state().log("ERROR: short read of texture %s", name);
            return nullptr;
        }
        return load(file, name, Effekseer::TextureLoaderHelper::GetIsMipmapEnabled(path));
    }

    Effekseer::TextureRef Load(const void *data, int32_t size, Effekseer::TextureType, bool mips) override
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        return load(std::vector<uint8_t>(bytes, bytes + size), "<embedded texture>", mips);
    }

  private:
    Effekseer::TextureRef wrap(bgfx::TextureHandle handle, const tex::Layout &layout)
    {
        auto texture = Effekseer::MakeRefPtr<Effekseer::Texture>();
        texture->SetBackend(EffekseerRendererBGFX::CreateTextureFromHandle(
            renderer_, {handle.idx}, int(layout.width), int(layout.height), int(layout.levels)));
        return texture;
    }

    Effekseer::TextureRef load(const std::vector<uint8_t> &file, const char *name, bool mips)
    {
        State &s = state();
        tex::Service &service = tex::Service::instance();
        // ETC2 is part of GLES 3.0; the RGBA override (texture_compression = rgba) keeps RGBA8.
        const bool etc2 = service.mode() != tex::Mode::Rgba && etc2_supported();
        tex::Layout layout{tex::DxtFormat::Rgba8, 0, 0, 1, 1};
        uint64_t key = 0;
        const bool sized = png_size(file, layout.width, layout.height);
        if (etc2 && sized)
        {
            layout.levels = mips ? tex::full_mip_count(layout.width, layout.height) : 1;
            tex::KeyBuilder builder(layout);
            builder.add(file.data(), file.size());
            key = builder.key();
            if (std::optional<tex::Blob> ktx = service.lookup(key, layout))
            {
                const bgfx::TextureHandle handle = create_from_ktx(*ktx, name);
                if (bgfx::isValid(handle))
                {
                    ++s.stats.cache_hits;
                    return wrap(handle, layout);
                }
                s.log("ERROR: cached ETC2 of %s was rejected by bgfx; decoding the image", name);
            }
        }

        bx::DefaultAllocator allocator;
        bx::Error error;
        bimg::ImageContainer *image =
            bimg::imageParse(&allocator, file.data(), uint32_t(file.size()), bimg::TextureFormat::RGBA8, &error);
        if (!image)
        {
            s.log("ERROR: cannot decode texture %s", name);
            return nullptr;
        }
        ++s.stats.decoded;
        if (sized && (image->m_width != layout.width || image->m_height != layout.height))
            s.log("ERROR: %s: PNG header says %ux%u, decoded %ux%u", name, layout.width, layout.height,
                  image->m_width, image->m_height);
        const bool key_valid = sized && image->m_width == layout.width && image->m_height == layout.height;
        layout.width = image->m_width;
        layout.height = image->m_height;
        layout.levels = mips ? tex::full_mip_count(layout.width, layout.height) : 1;
        std::vector<uint8_t> payload =
            tex::build_rgba_mips(static_cast<const uint8_t *>(image->m_data), layout.width, layout.height, layout.levels);
        bimg::imageFree(image);

        // Shown at once as RGBA8; the ETC2 version replaces it when the worker is done.
        const bgfx::TextureHandle handle =
            bgfx::createTexture2D(uint16_t(layout.width), uint16_t(layout.height), layout.levels > 1, 1,
                                  bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
                                  bgfx::copy(payload.data(), uint32_t(payload.size())));
        if (!bgfx::isValid(handle))
        {
            s.log("ERROR: cannot create texture %s (%ux%u)", name, layout.width, layout.height);
            return nullptr;
        }
        bgfx::setName(handle, name);
        Effekseer::TextureRef texture = wrap(handle, layout);
        if (!etc2)
            return texture;
        if (!key_valid)
        {
            tex::KeyBuilder builder(layout);
            builder.add(payload.data(), payload.size()); // non-PNG: the cache key covers the pixels
            key = builder.key();
            if (std::optional<tex::Blob> ktx = service.lookup(key, layout))
            {
                const bgfx::TextureHandle cached = create_from_ktx(*ktx, name);
                if (bgfx::isValid(cached))
                    EffekseerRendererBGFX::ReplaceTextureHandle(texture->GetBackend(), {cached.idx});
                return texture;
            }
        }
        Pending pending{nullptr, texture->GetBackend(), name, layout, key,
                        tex::choose_etc_format(layout.format, payload.data(), payload.size()), std::move(payload)};
        submit(pending);
        s.pending.push_back(std::move(pending));
        return texture;
    }

    EffekseerRenderer::RendererRef renderer_;
    Effekseer::FileInterfaceRef files_;
};
} // namespace

Effekseer::TextureLoaderRef CreateCachedTextureLoader(const EffekseerRenderer::RendererRef &renderer,
                                                      const Effekseer::FileInterfaceRef &files, LogFn log)
{
    state().log = log;
    return Effekseer::MakeRefPtr<CachedTextureLoader>(renderer, files);
}

void PollTextures()
{
    State &s = state();
    for (auto it = s.pending.begin(); it != s.pending.end();)
    {
        if (!it->job)
        {
            if (!it->payload.empty())
                submit(*it);
            if (!it->job)
            {
                it = it->payload.empty() ? s.pending.erase(it) : it + 1; // refused: stays RGBA8
                continue;
            }
        }
        const int job_state = it->job->state.load();
        if (job_state == tex::Job::Done)
        {
            const bgfx::TextureHandle handle = create_from_ktx(*it->job->result, it->name.c_str());
            if (bgfx::isValid(handle))
                EffekseerRendererBGFX::ReplaceTextureHandle(it->texture, {handle.idx});
            else
                s.log("ERROR: bgfx rejected the ETC2 version of %s; it stays RGBA8", it->name.c_str());
        }
        else if (job_state == tex::Job::Failed)
            s.log("ERROR: ETC2 transcode of %s failed; it stays RGBA8", it->name.c_str());
        else
        {
            ++it;
            continue;
        }
        tex::Service::instance().release(it->job);
        it = s.pending.erase(it);
    }
}

void ReleasePendingTextures()
{
    State &s = state();
    for (Pending &pending : s.pending)
        tex::Service::instance().release(pending.job);
    s.pending.clear();
}

TextureStats GetTextureStats()
{
    TextureStats stats = state().stats;
    stats.transcoding = uint32_t(state().pending.size());
    return stats;
}

} // namespace storm::render::effekseer
