#pragma once
// GPU skinning of GEOS::AVERTEX0 sources (one blend weight, two bone indices).
//
// Each source becomes a static vertex buffer in the AVERTEX0 layout
// (D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4 | normal | diffuse | uv), uploaded once per source
// lifetime; the pose is a float bone palette bound to D3DVERTEXTEXTURESAMPLER0. The renderer's
// skinned fixed-function vertex shader (renderer_bgfx/shaders/include/ff_vertex.sh, FF_SKIN)
// blends the bones exactly like the CPU kernel in src/libs/model/src/skinning.hpp, which stays for
// the caustic pass only (its vs_1_1 port has no skinned variant).
// Vertex and palette layout: skinning_palette.hpp.
#include "dx9render.h"
#include "skinning_palette.hpp"
#include "vertex_source_lifetime.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace storm::model
{

class GpuSkinning
{
    struct Source final : render::VertexSourceLifetime::Dependent
    {
        VDX9RENDER *rs = nullptr;
        int32_t vb = -1;
        void invalidateSource() noexcept override
        {
            if (vb >= 0)
                rs->ReleaseVertexBuffer(vb);
            vb = -1;
        }
        ~Source() override { invalidateSource(); }
    };

    VDX9RENDER *rs_ = nullptr;
    IDirect3DTexture9 *palette_ = nullptr;
    uint32_t paletteBones_ = 0;
    bool poseUploaded_ = false;
    std::unordered_map<const void *, std::shared_ptr<Source>> sources_;

  public:
    explicit GpuSkinning(VDX9RENDER *rs = nullptr) : rs_(rs) {}
    GpuSkinning(const GpuSkinning &) = delete;
    GpuSkinning &operator=(const GpuSkinning &) = delete;
    ~GpuSkinning() { clear(); }

    void setRenderer(VDX9RENDER *rs) { rs_ = rs; }

    // Releases the vertex buffers and the palette (device lost, new animation, destruction).
    void clear()
    {
        sources_.clear();
        if (palette_)
            palette_->Release();
        palette_ = nullptr;
        paletteBones_ = 0;
        poseUploaded_ = false;
    }

    // A new pose: the palette is uploaded again by the next bind().
    void beginPose()
    {
        poseUploaded_ = false;
        std::erase_if(sources_, [](const auto &item) { return item.second->vb < 0; });
    }

    // The static vertex buffer of a source, created and filled on first use.
    int32_t vertexBuffer(const void *source, const std::shared_ptr<render::VertexSourceLifetime> &lifetime,
                         uint32_t count)
    {
        if (!source || !lifetime || !lifetime->valid() || !count)
            throw std::invalid_argument("Invalid skinning source");
        auto &slot = sources_[source];
        if (slot && slot->vb >= 0)
            return slot->vb;
        slot = std::make_shared<Source>();
        slot->rs = rs_;
        lifetime->observe(slot);
        const size_t bytes = size_t(count) * kSkinnedSourceStride;
        slot->vb = rs_->CreateVertexBuffer(kSkinnedSourceFvf, bytes, D3DUSAGE_WRITEONLY);
        auto *data = slot->vb >= 0 ? rs_->LockVertexBuffer(slot->vb) : nullptr;
        if (!data)
            throw std::runtime_error("Skinned source vertex buffer allocation failed");
        std::memcpy(data, source, bytes);
        rs_->UnLockVertexBuffer(slot->vb);
        return slot->vb;
    }

    // Uploads the pose once per beginPose() and binds the palette for the skinned draws that follow.
    void bind(const glm::mat4 *bones, uint32_t count)
    {
        if (!count)
            throw std::invalid_argument("Skinned draw without bones");
        if (!palette_ || paletteBones_ != count)
        {
            if (palette_)
                palette_->Release();
            palette_ = nullptr;
            if (rs_->CreateTexture(kPaletteTexelsPerBone, count, 1, D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F,
                                   D3DPOOL_DEFAULT, &palette_) != D3D_OK || !palette_)
                throw std::runtime_error("Bone palette allocation failed");
            paletteBones_ = count;
            poseUploaded_ = false;
        }
        if (!poseUploaded_)
        {
            D3DLOCKED_RECT locked{};
            if (palette_->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD) != D3D_OK || !locked.pBits)
                throw std::runtime_error("Bone palette upload failed");
            for (uint32_t i = 0; i < count; ++i)
                writePaletteRow(reinterpret_cast<const float *>(&bones[i]),
                                reinterpret_cast<float *>(static_cast<uint8_t *>(locked.pBits) + size_t(i) * locked.Pitch));
            palette_->UnlockRect(0);
            poseUploaded_ = true;
        }
        rs_->SetTexture(D3DVERTEXTEXTURESAMPLER0, palette_);
    }

    void unbind() { rs_->SetTexture(D3DVERTEXTEXTURESAMPLER0, nullptr); }
};

} // namespace storm::model
