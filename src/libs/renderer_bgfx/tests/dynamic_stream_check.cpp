#include "dynamic_vertex_stream_bgfx.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <vector>
struct Vertex {float pos[3],normal[3];uint32_t color;float uv[2];};
static_assert(sizeof(Vertex)==36&&offsetof(Vertex,color)==24);
static const bgfx::Memory* shader(const char* path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
    if(!file||size<=0) throw std::runtime_error("Missing probe shader");
    auto* bytes=bgfx::alloc(uint32_t(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes->data),size);return bytes;
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    bgfx::Init init;init.type=bgfx::RendererType::Vulkan;init.fallback=false;init.swapChain.width=init.swapChain.height=0;
    if(!bgfx::init(init)) return 3;
    int result=0;
    try {
        auto program=bgfx::createProgram(bgfx::createShader(shader(argv[1])),bgfx::createShader(shader(argv[2])),true);
        auto color=bgfx::createTexture2D(32,32,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        auto target=bgfx::createFrameBuffer(1,&color,true);
        auto readback=bgfx::createTexture2D(32,32,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
        {
            storm::render::BgfxDynamicVertexStream stream;
            storm::render::DynamicVertexStream& neutral=stream;
            bool rejected=false;try {stream.bind();}catch(const std::logic_error&){rejected=true;}
            if(!rejected||neutral.id()) throw std::runtime_error("Uninitialized stream was usable");
            bgfx::setViewFrameBuffer(0,target);bgfx::setViewRect(0,0,0,32,32);bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x102030ff);
            const int expected[][3]={{192,64,191},{16,32,48},{32,204,26}};
            for(unsigned pose=0;pose<3;++pose) {
                const float shift=pose==1?4.f:0.f;
                std::vector<Vertex> vertices{{{-1+shift,-1,0},{.25f,0,1},0xffc00000,{.75f,0}},
                    {{3+shift,-1,0},{.25f,0,1},0xffc00000,{.75f,0}},{{-1+shift,3,0},{.25f,0,1},0xffc00000,{.75f,0}}};
                if(pose==1) vertices.push_back(vertices[0]);
                if(pose==2) for(auto& v:vertices) {v.normal[0]=.8f;v.color=0xff200000;v.uv[0]=.1f;}
                neutral.upload(std::as_bytes(std::span<const Vertex>(vertices)));
                if(!neutral.id()) throw std::runtime_error("Uploaded stream has no ID");
                rejected=false;try {stream.bind(0,0,uint32_t(vertices.size()+1));}catch(const std::invalid_argument&){rejected=true;}
                if(!rejected) throw std::runtime_error("Out-of-range dynamic binding accepted");
                stream.bind(0,0,3);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(0,program);
                bgfx::TextureRegion source,destination;source.init(color);destination.init(readback);bgfx::blit(1,destination,source);
                std::array<uint8_t,32*32*4> pixels{};const auto ready=bgfx::read(destination,pixels.data());
                unsigned frames=0;while(bgfx::frame()<ready) if(++frames>20) throw std::runtime_error("Dynamic readback timeout");
                for(unsigned c=0;c<3;++c)
                    if(std::abs(int(pixels[(16*32+16)*4+c])-expected[pose][c])>2)
                        throw std::runtime_error("Dynamic pose pixel mismatch "+std::to_string(pose));
                std::cout<<"Dynamic stream pose "<<pose<<" passed\n";
            }
        }
        bgfx::destroy(program);bgfx::destroy(readback);bgfx::destroy(target);bgfx::frame();
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    bgfx::shutdown();return result;
}
