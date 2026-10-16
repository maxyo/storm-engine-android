#include "fog_pass.hpp"
#include <bx/math.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cmath>

static const bgfx::Memory* shader(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("shader file missing");
    const auto size = file.tellg();
    if (size <= 0) throw std::runtime_error("empty shader");
    auto* memory = bgfx::alloc(uint32_t(size));
    file.seekg(0); file.read(reinterpret_cast<char*>(memory->data), size);
    return memory;
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    bgfx::Init init;
    init.type = bgfx::RendererType::Vulkan;
    init.fallback = false; // Never let a Noop fallback pass a pixel test.
    init.swapChain.width = init.swapChain.height = 0; // Headless: only offscreen targets.
    if (!bgfx::init(init)) { std::cerr << "Headless Vulkan initialization failed\n"; return 3; }
    int result = 0;
    try {
        if (!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA8,
                                 BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST))
            throw std::runtime_error("Vulkan texture readback unsupported");
        const auto color = bgfx::createTexture2D(64,64,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        const auto target = bgfx::createFrameBuffer(1,&color,true);
        const auto readback = bgfx::createTexture2D(64,64,false,1,bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        if (!bgfx::isValid(target) || !bgfx::isValid(readback)) throw std::runtime_error("render target allocation failed");
        {
            storm::render::FogPass pass(shader(argv[1]),shader(argv[2]));
            float view[16], projection[16], world[16];
            bx::mtxLookAt(view, bx::Vec3{0,0,0}, bx::Vec3{0,1200,2000});
            bx::mtxProj(projection, 40.f, 1.f, 0.1f, 8000.f, bgfx::getCaps()->homogeneousDepth);
            bx::mtxIdentity(world);
            bgfx::setViewFrameBuffer(0,target);
            bgfx::setViewRect(0,0,0,64,64);
            bgfx::setViewTransform(0,view,projection);
            bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x102030ff);
            // Blend, alpha-zero discard, opt-out, and updated night color.
            const uint32_t argb[] = {0x80804020,0x00804020,0xff804020,0xff102840};
            const int expected[][3] = {{72,48,40},{16,32,48},{16,32,48},{16,40,64}};
            for (int scenario=0; scenario<4; ++scenario) {
                unsigned calls=0;
                auto mesh=storm::weather::makeFogSphere([&](float,float,float){++calls;return argb[scenario];});
                if(calls!=513 || mesh.indices.size()!=3048) throw std::runtime_error("fog topology count changed");
                for(auto index:mesh.indices) if(index>=mesh.vertices.size()) throw std::runtime_error("fog index out of bounds");
                pass.update(mesh);
                bgfx::touch(0);
                pass.submit(0,world,scenario!=2);
                bgfx::TextureRegion source,destination;
                source.init(color); destination.init(readback);
                bgfx::blit(1,destination,source);
                std::vector<uint8_t> pixels(64*64*4);
                const auto ready=bgfx::read(destination,pixels.data());
                unsigned frames=0;
                while(bgfx::frame()<ready) if(++frames>20) throw std::runtime_error("readback timeout");
                const auto* center=&pixels[(32*64+32)*4];
                for(int channel=0;channel<3;++channel)
                    if(std::abs(int(center[channel])-expected[scenario][channel])>3)
                        throw std::runtime_error("fog pixel/color/blend mismatch in scenario "+std::to_string(scenario)+
                            " got "+std::to_string(center[0])+","+std::to_string(center[1])+","+std::to_string(center[2]));
                std::cout << "fog scenario " << scenario << " passed, center=" << int(center[0]) << ',' << int(center[1]) << ',' << int(center[2]) << '\n';
            }
        }
        bgfx::destroy(readback); bgfx::destroy(target);
        bgfx::frame();
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';result=1; }
    bgfx::shutdown();
    return result;
}
