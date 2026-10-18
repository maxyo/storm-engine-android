#include "fog_pass.hpp"
#include "rgba_texture.hpp"
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
                storm::render::ColoredMesh& neutral=pass;
                std::vector<storm::render::ColorVertex> submitted(mesh.vertices.begin(),mesh.vertices.end());
                if(scenario==3) submitted.push_back(submitted.back()); // Resize without replacing topology.
                neutral.update(submitted,scenario==0?std::span<const uint16_t>(mesh.indices):std::span<const uint16_t>{});
                if(!neutral.supportsMaterial("SkyFog")||neutral.supportsMaterial("Sea"))
                    throw std::runtime_error("Material capability mismatch");
                bool rejected=false;
                try {neutral.submit({std::span<const float,16>(world),"Sea",0});}
                catch(const std::invalid_argument&) {rejected=true;}
                if(!rejected) throw std::runtime_error("Unsupported material silently accepted");
                bgfx::touch(0);
                if(scenario!=2) neutral.submit({std::span<const float,16>(world),"SkyFog",0});
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
        // Padded BGRA CPU source -> retained canonical CPU pixels and GPU RGBA8.
        {
            const std::array<uint8_t,24> sourcePixels{30,20,10,255,60,50,40,128,9,9,9,9,
                                                    90,80,70,64,120,110,100,32,8,8,8,8};
            storm::render::RgbaTexture texture({sourcePixels,2,2,12,storm::render::PixelOrder::bgra8});
            uint32_t sampled=0;
            if(!texture.sample(1,1,sampled)||sampled!=0x20646e78u) throw std::runtime_error("CPU texture sampling mismatch");
            const auto destinationTexture=bgfx::createTexture2D(2,2,false,1,bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
            bgfx::TextureRegion source,destination;
            source.init(texture.handle()); destination.init(destinationTexture);
            bgfx::blit(2,destination,source);
            std::array<uint8_t,16> pixels{};
            const auto ready=bgfx::read(destination,pixels.data());
            unsigned frames=0;
            while(bgfx::frame()<ready) if(++frames>20) throw std::runtime_error("Texture readback timeout");
            const std::array<uint8_t,16> expected{10,20,30,255,40,50,60,128,70,80,90,64,100,110,120,32};
            if(pixels!=expected) throw std::runtime_error("CPU/GPU texture byte parity failed");
            bgfx::destroy(destinationTexture);
            std::cout<<"Padded BGRA -> CPU/GPU RGBA texture parity passed\n";
        }
        bgfx::destroy(readback); bgfx::destroy(target);
        bgfx::frame();
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';result=1; }
    bgfx::shutdown();
    return result;
}
