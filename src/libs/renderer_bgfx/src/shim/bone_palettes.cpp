#include "bone_palettes.hpp"

#include "perf.hpp"
#include "shim.hpp"

#include <algorithm>
#include <cstring>

namespace storm::shim
{

uint32_t BonePalettes::capacity_rows()
{
    if (capacity_ == 0)
    {
        // 4096 bone rows (about 60 characters of 64 bones), within the GLES 3.0 minimum texture size the caps allow.
        capacity_ = std::min<uint32_t>(4096, bgfx::getCaps()->limits.maxTextureSize);
        for (auto &texture : textures_)
            texture = bgfx::createTexture2D(kTexelsPerBone, static_cast<uint16_t>(capacity_), false, 1,
                                            bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
        rows_.reserve(static_cast<size_t>(capacity_) * kTexelsPerBone * 4);
    }
    return capacity_;
}

uint32_t BonePalettes::place(TexStore &palette)
{
    // A palette not locked since it was placed in this frame is the same pose: its rows are already there.
    if (!palette.any_dirty)
        for (const Placed &placed : placed_)
            if (placed.store == &palette)
                return placed.row;
    TexStore::Sub &level = palette.sub(0, 0);
    const uint32_t bones = palette.height;
    const uint32_t row = static_cast<uint32_t>(rows_.size() / (kTexelsPerBone * 4));
    if (level.data.empty() || palette.width != kTexelsPerBone || row + bones > capacity_rows())
    {
        if (!full_logged_)
            logf("ERROR: bone palettes: %u rows of %u used in this frame, a palette of %u bones (%ux%u) does not fit; "
                 "such draws upload their own palette texture",
                 row, capacity_rows(), bones, palette.width, palette.height);
        full_logged_ = true;
        return UINT32_MAX;
    }
    const uint32_t pitch = level_pitch(palette.format, palette.width);
    const size_t row_floats = kTexelsPerBone * 4;
    const size_t start = rows_.size();
    rows_.resize(start + static_cast<size_t>(bones) * row_floats);
    for (uint32_t bone = 0; bone < bones; ++bone)
        std::memcpy(rows_.data() + start + bone * row_floats, level.data.data() + static_cast<size_t>(bone) * pitch,
                    row_floats * sizeof(float));
    // The palette's own texture is not uploaded for these contents (TexStore::sync is not called for it).
    palette.any_dirty = false;
    level.dirty = false;
    std::erase_if(placed_, [&palette](const Placed &placed) { return placed.store == &palette; });
    placed_.push_back({&palette, row});
    return row;
}

void BonePalettes::flush()
{
    if (!rows_.empty())
    {
        const uint32_t rows = static_cast<uint32_t>(rows_.size() / (kTexelsPerBone * 4));
        const bgfx::Memory *memory = bgfx::copy(rows_.data(), static_cast<uint32_t>(rows_.size() * sizeof(float)));
        counted_texture_upload(memory);
        bgfx::updateTexture2D(textures_[current_], 0, 0, 0, 0, kTexelsPerBone, static_cast<uint16_t>(rows), memory);
        current_ = (current_ + 1) % kRingSize;
    }
    rows_.clear();
    placed_.clear();
}

void BonePalettes::shutdown()
{
    for (auto &texture : textures_)
    {
        if (bgfx::isValid(texture))
            bgfx::destroy(texture);
        texture = BGFX_INVALID_HANDLE;
    }
    capacity_ = 0;
    rows_.clear();
    placed_.clear();
}

} // namespace storm::shim
