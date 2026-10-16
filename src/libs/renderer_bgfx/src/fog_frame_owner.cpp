#include "fog_frame_owner.hpp"
#include <stdexcept>

namespace storm::render {
FogFrameOwner::FogFrameOwner(FogSurface surface,const FogSnapshot& snapshot,
        const std::vector<uint8_t>& vs,const std::vector<uint8_t>& fs) : world_(snapshot.world) {
    if(!surface.nativeWindow || !surface.width || !surface.height || surface.width>UINT16_MAX || surface.height>UINT16_MAX || vs.empty() || fs.empty())
        throw std::invalid_argument("Fog preview needs a native surface, size and compiled shaders");
    bgfx::Init init;
    init.type=bgfx::RendererType::Vulkan;
    init.fallback=false;
    init.swapChain.nwh=surface.nativeWindow;
    init.swapChain.ndt=surface.nativeDisplay;
    init.swapChain.width=surface.width;
    init.swapChain.height=surface.height;
    init.swapChain.flags=surface.vsync ? BGFX_RESET_VSYNC : 0;
    if(!bgfx::init(init)) throw std::runtime_error("bgfx fog preview Vulkan initialization failed");
    swapChain_=init.swapChain;
    try {
        pass_=std::make_unique<FogPass>(bgfx::copy(vs.data(),uint32_t(vs.size())),bgfx::copy(fs.data(),uint32_t(fs.size())));
        pass_->update(snapshot.mesh);
        bgfx::setViewTransform(0,snapshot.view.data(),snapshot.projection.data());
        bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x102030ff);
        bgfx::setViewRect(0,0,0,uint16_t(surface.width),uint16_t(surface.height));
    } catch(...) {
        pass_.reset(); bgfx::shutdown(); throw;
    }
}
FogFrameOwner::~FogFrameOwner() { pass_.reset(); bgfx::shutdown(); }
void FogFrameOwner::resize(uint32_t width,uint32_t height) {
    if(!width||!height||width>UINT16_MAX||height>UINT16_MAX) return;
    swapChain_.width=width; swapChain_.height=height;
    bgfx::reset(BGFX_RESET_NONE,&swapChain_);
    bgfx::setViewRect(0,0,0,uint16_t(width),uint16_t(height));
}
void FogFrameOwner::frame(bool enabled) {
    bgfx::touch(0); pass_->submit(0,world_.data(),enabled); bgfx::frame();
}
}
