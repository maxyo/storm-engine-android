#pragma once
#include "fog_pass.hpp"
#include "fog_snapshot.hpp"
#include <memory>
#include <vector>

namespace storm::render {
struct FogSurface {
    void* nativeDisplay=nullptr;
    void* nativeWindow=nullptr;
    uint32_t width=0,height=0;
    bool vsync=true;
};
// The caller keeps the native surface alive until this owner is destroyed.
// There is exactly one bgfx context; copying/moving the owner is disallowed.
class FogFrameOwner final {
public:
    FogFrameOwner(FogSurface surface,const FogSnapshot& snapshot,
                  const std::vector<uint8_t>& vertexShader,const std::vector<uint8_t>& fragmentShader);
    ~FogFrameOwner();
    FogFrameOwner(const FogFrameOwner&)=delete;
    FogFrameOwner& operator=(const FogFrameOwner&)=delete;
    void resize(uint32_t width,uint32_t height);
    void frame(bool enabled=true);
private:
    bgfx::SwapChain swapChain_;
    std::array<float,16> world_;
    std::unique_ptr<FogPass> pass_;
};
}
