#include "colored_mesh.hpp"
#include "textured_mesh.hpp"
#include "texture_pixels.hpp"
#include <limits>
#include <iostream>
#include <vector>
#include <functional>
using namespace storm::render;
void require(bool value) {if(!value) throw std::runtime_error("Resource contract failed");}
void rejects(const std::function<void()>& action) {
    bool rejected=false;try {action();}catch(const std::invalid_argument&) {rejected=true;}require(rejected);
}
int main() {
    std::vector<ColorVertex> vertices{{0,0,0,0},{1,0,0,0},{0,1,0,0}};
    std::array<uint16_t,3> indices{0,1,2};
    validateColorMesh(vertices,indices);
    rejects([&]{validateColorMesh({},indices);});
    rejects([&]{validateColorMesh(vertices,{indices.data(),2});});
    indices[2]=3;rejects([&]{validateColorMesh(vertices,indices);});indices[2]=2;
    vertices[0].x=std::numeric_limits<float>::infinity();rejects([&]{validateColorMesh(vertices,indices);});
    std::array<float,16> world{};
    rejects([&]{validateColorDraw({world,""});});
    world[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{validateColorDraw({world});});
    require(isSkyFogMaterial("sKyFoG")&&!isSkyFogMaterial("SkyFogUnknown"));
    const std::array<uint8_t,12> bytes{1,2,3,4,5,6,7,8,99,99,99,99};
    TexturePixels view{bytes,2,1,12,PixelOrder::bgra8};
    uint32_t color=0;require(sampleArgb(view,1,0,color)&&color==0x08070605u);
    require(sampleArgb(view,-2,2,color)&&color==0x04030201u);
    view.order=PixelOrder::rgba8;require(sampleArgb(view,1,1,color)&&color==0x08050607u);
    color=123;view.pitch=7;require(!sampleArgb(view,0,0,color)&&color==123);
    view.pitch=12;view.height=2;require(!sampleArgb(view,0,0,color)&&color==123);
    view.height=1;require(!sampleArgb(view,std::numeric_limits<float>::quiet_NaN(),0,color)&&color==123);
    std::array<TexturedVertex,3> textured{};
    validateTexturedMesh(textured,indices);
    textured[0].u1=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{validateTexturedMesh(textured,indices);});
    std::array<TextureDrawRange,1> ranges{{{UINT32_MAX,3,0,1}}};
    world[0]=1;
    rejects([&]{validateTexturedDraw({world,ranges},3);});
    ranges[0]={0,6,0,1};rejects([&]{validateTexturedDraw({world,ranges},3);});
    require(skyMaterial("SkyBlend_Alpha")==SkyMaterial::alphaMask&&skyMaterial("SkyBlendLand")==SkyMaterial::unsupported);
    std::cout<<"Neutral resource validation, formats, pitch and capability contract: PASS\n";
}
