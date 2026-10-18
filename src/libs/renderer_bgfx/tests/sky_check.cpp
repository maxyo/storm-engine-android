#include "sky_pass.hpp"
#include "sky_mesh.hpp"
#include <bx/math.h>
#include <fstream>
#include <iostream>
static const bgfx::Memory* shader(const char* path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate); const auto size=file.tellg();
    if(!file||size<=0||size>16*1024*1024) throw std::runtime_error("Invalid shader file");
    auto* bytes=bgfx::alloc(uint32_t(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes->data),size);return bytes;
}
static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    bgfx::Init init;init.type=bgfx::RendererType::Vulkan;init.fallback=false;init.swapChain.width=init.swapChain.height=0;
    if(!bgfx::init(init)) return 3;
    int result=0;
    try {
        const auto color=bgfx::createTexture2D(64,64,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        const auto target=bgfx::createFrameBuffer(1,&color,true);
        const auto readback=bgfx::createTexture2D(64,64,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
        {
            storm::render::TextureStore textures;
            const std::array<uint8_t,4> base{200,100,50,64};
            const std::array<uint8_t,16> next{255,0,0,0,0,255,0,0,0,0,255,0,20,160,240,192};
            const int32_t first=textures.create({base,1,1,4});
            const int32_t second=textures.create({next,2,2,8});
            storm::render::SkyPass pass(textures,shader(argv[1]),shader(argv[2]));
            storm::render::TexturedMesh& neutral=pass;
            auto mesh=storm::weather::makeSkyMesh(2000.f);
            storm::render::validateTexturedMesh(mesh.vertices,mesh.indices);
            for(size_t i=0;i<20;++i) {
                const auto& a=mesh.vertices[i];const auto& b=mesh.vertices[i+20];
                require(a.x==b.x&&a.y==-b.y&&a.z==b.z&&a.u==b.u&&a.v==b.v&&a.u1==b.u1&&a.v1==b.v1,"Reflection UV/position mismatch");
            }
            for(auto& vertex:mesh.vertices) {vertex.argb=0xff804020;vertex.u=vertex.v=0;vertex.u1=vertex.v1=1;}
            neutral.update(mesh.vertices,mesh.indices);
            float world[16],view[16],projection[16];bx::mtxIdentity(world);
            bx::mtxLookAt(view,bx::Vec3{0,100,0},bx::Vec3{0,200,2000});
            bx::mtxProj(projection,40.f,1.f,0.1f,8000.f,bgfx::getCaps()->homogeneousDepth);
            bgfx::setViewFrameBuffer(0,target);bgfx::setViewRect(0,0,0,64,64);bgfx::setViewTransform(0,view,projection);
            bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x102030ff);
            std::array<storm::render::TextureDrawRange,10> ranges;
            for(uint32_t i=0;i<10;++i) ranges[i]={i*6,6,first,second};
            require(neutral.supportsMaterial("sKyBlEnD_AlPhA")&&!neutral.supportsMaterial("SkyLand"),"Material capability mismatch");
            const char* names[]{"Sky","SkyBlend","SkyBlend","SkyBlend","SkyBlend_alpha","SkyBlend_alpha"};
            const uint32_t factors[]{0,0,128,255,0,255};
            for(size_t scenario=0;scenario<6;++scenario) {
                neutral.submit({std::span<const float,16>(world),ranges,names[scenario],factors[scenario]<<24,0});
                bgfx::TextureRegion src,dst;src.init(color);dst.init(readback);bgfx::blit(1,dst,src);
                std::array<uint8_t,64*64*4> pixels{};const auto ready=bgfx::read(dst,pixels.data());
                unsigned frames=0;while(bgfx::frame()<ready) if(++frames>20) throw std::runtime_error("Readback timeout");
                const float factor=scenario==0?0.f:float(factors[scenario])/255.f;
                const int diffuse[]{128,64,32},destination[]{16,32,48};
                const float alpha=scenario<4?0.f:((1-factor)*base[3]+factor*next[15])/255.f;
                for(size_t c=0;c<3;++c) {
                    const float source=(1-factor)*base[c]*diffuse[c]/255.f+factor*next[12+c];
                    const float expected=source*(1-alpha)+destination[c]*alpha;
                    const int actual=pixels[(32*64+32)*4+c];
                    if(std::abs(float(actual)-expected)>3.f) throw std::runtime_error("Sky shader parity mismatch scenario "+std::to_string(scenario)+" channel "+std::to_string(c)+" got "+std::to_string(actual)+" expected "+std::to_string(expected));
                }
                std::cout<<"Sky material scenario "<<scenario<<" passed\n";
            }
            bool rejected=false;
            try {neutral.submit({std::span<const float,16>(world),ranges,"SkyLand"});}catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Unported depth material accepted");
            textures.release(second);rejected=false;
            try {neutral.submit({std::span<const float,16>(world),ranges,"SkyBlend"});}catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Released texture binding accepted");
            const auto third=textures.create({next,2,2,8});require(third!=second,"Released texture ID reused");
        }
        bgfx::destroy(readback);bgfx::destroy(target);bgfx::frame();
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';result=1;}
    bgfx::shutdown();return result;
}
