#include "fog_snapshot.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace storm::render;
void require(bool value) { if(!value) throw std::runtime_error("Snapshot contract failed"); }
int main() {
    const auto path=(std::filesystem::temp_directory_path()/"storm-fog-contract.bin").string();
    try {
        FogSnapshot original{};
        original.mesh=storm::weather::makeFogSphere([](float,float,float){return 0x80604020u;});
        for(auto* matrix:{&original.world,&original.view,&original.projection})
            for(int i=0;i<4;++i) (*matrix)[i*5]=1;
        writeFogSnapshot(path,original);
        auto copy=readFogSnapshot(path);
        require(std::memcmp(original.mesh.vertices.data(),copy.mesh.vertices.data(),sizeof(original.mesh.vertices))==0);
        require(original.mesh.indices==copy.mesh.indices && original.world==copy.world && original.view==copy.view && original.projection==copy.projection);
        std::string result;
        configureFogCapture(""); requestFogCapture();
        require(!captureFogFrame(original.world.data(),original.view.data(),original.projection.data(),result));
        configureFogCapture(path); requestFogCapture();
        require(!captureFogFrame(original.world.data(),original.view.data(),original.projection.data(),result));
        stageFogMesh(original.mesh,true);
        auto updated=original.mesh; updated.vertices[0].argb=0xffffffffu;
        stageFogMesh(updated,false);
        require(captureFogFrame(original.world.data(),original.view.data(),original.projection.data(),result) && result.empty());
        require(!captureFogFrame(original.world.data(),original.view.data(),original.projection.data(),result));
        copy=readFogSnapshot(path);
        require(copy.mesh.indices==original.mesh.indices && copy.mesh.vertices[0].argb==0xffffffffu);
        {std::ofstream file(path,std::ios::binary|std::ios::app); file.put('x');}
        bool rejected=false; try {readFogSnapshot(path);} catch(const std::exception&) {rejected=true;} require(rejected);
        {std::ofstream file(path,std::ios::binary|std::ios::trunc); file<<"STFOG001";}
        rejected=false; try {readFogSnapshot(path);} catch(const std::exception&) {rejected=true;} require(rejected);
        std::filesystem::remove(path);
        std::cout<<"Fog snapshot roundtrip, bounded reads, capture lifecycle: PASS\n";
    } catch(const std::exception& e) {std::filesystem::remove(path); std::cerr<<e.what()<<'\n';return 1;}
}
