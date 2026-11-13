// The bone palettes of all GPU-skinned draws of a frame in one texture (perf audit finding 8): the model gives each
// animated character its own RGBA32F palette texture (model gpu_skinning.hpp) and re-locks it for every pose; uploaded
// one by one that was one glTexSubImage2D per character per frame (30 in a town), each into a texture the GPU may
// still read from the previous frame. Here the rows of every palette a draw binds are appended to the frame's
// staging copy, the draw reads them at a row offset (u_ffBoneBase, shaders/include/ff_skin.sh), and the frame's
// rows go up in one update right before bgfx::frame (Context::run_frame). Three atlas textures rotate, so the
// update never writes the one the GPU may still be reading.
#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace storm::shim
{

struct TexStore;

class BonePalettes
{
  public:
    static constexpr uint32_t kTexelsPerBone = 3;   // model skinning_palette.hpp
    static constexpr uint32_t kRingSize = 3;

    // The atlas row of `palette`'s first bone in this frame (its current contents), or UINT32_MAX when the frame's
    // atlas is full (logged once; the caller binds the palette's own texture).
    uint32_t place(TexStore &palette);
    bgfx::TextureHandle texture() const { return textures_[current_]; }
    // Uploads the frame's rows into the current atlas and moves to the next one. Before every bgfx::frame.
    void flush();
    void shutdown();

  private:
    struct Placed
    {
        const TexStore *store;
        uint32_t row;
    };
    uint32_t capacity_rows();

    bgfx::TextureHandle textures_[kRingSize] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    uint32_t current_ = 0;
    uint32_t capacity_ = 0;
    std::vector<float> rows_; // kTexelsPerBone RGBA32F texels per row
    std::vector<Placed> placed_;
    bool full_logged_ = false;
};

} // namespace storm::shim
