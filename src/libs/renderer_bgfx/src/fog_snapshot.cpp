#include "fog_snapshot.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace storm::render {
namespace {
static_assert(std::endian::native == std::endian::little, "Snapshot v1 uses little-endian words");
constexpr std::array<char,8> magic{'S','T','F','O','G','0','0','1'};
struct Header { std::array<char,8> magic; uint32_t vertices, indices; };
static_assert(sizeof(Header)==16);
void validate(const FogSnapshot& value) {
    for (const auto& v:value.mesh.vertices)
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
            throw std::runtime_error("Non-finite fog vertex");
    for(auto index:value.mesh.indices)
        if(index>=weather::fogVertexCount) throw std::runtime_error("Invalid fog index");
    for(const auto* matrix:{&value.world,&value.view,&value.projection})
        for(float element:*matrix) if(!std::isfinite(element)) throw std::runtime_error("Non-finite fog matrix");
}
template<class T> void read(std::istream& file,T& data) {
    if(!file.read(reinterpret_cast<char*>(&data),sizeof(data))) throw std::runtime_error("Truncated fog snapshot");
}
template<class T> void write(std::ostream& file,const T& data) {
    file.write(reinterpret_cast<const char*>(&data),sizeof(data));
}
std::string capturePath;
FogSnapshot staged{};
bool haveMesh=false, requested=false;
}
void writeFogSnapshot(const std::string& path,const FogSnapshot& snapshot) {
    validate(snapshot);
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    if(!file) throw std::runtime_error("Cannot open fog snapshot for writing");
    write(file,Header{magic,weather::fogVertexCount,weather::fogIndexCount});
    write(file,snapshot.mesh.vertices); write(file,snapshot.mesh.indices);
    write(file,snapshot.world); write(file,snapshot.view); write(file,snapshot.projection);
    file.flush();
    if(!file) throw std::runtime_error("Cannot write fog snapshot");
}
FogSnapshot readFogSnapshot(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Cannot open fog snapshot");
    Header header; read(file,header);
    if(header.magic!=magic || header.vertices!=weather::fogVertexCount || header.indices!=weather::fogIndexCount)
        throw std::runtime_error("Unsupported fog snapshot format");
    FogSnapshot snapshot;
    read(file,snapshot.mesh.vertices); read(file,snapshot.mesh.indices);
    read(file,snapshot.world); read(file,snapshot.view); read(file,snapshot.projection);
    if(file.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Unexpected fog snapshot trailing data");
    validate(snapshot);
    return snapshot;
}
void configureFogCapture(std::string path) {
    capturePath=std::move(path); haveMesh=false; requested=false;
}
void requestFogCapture() { requested=!capturePath.empty(); }
void stageFogMesh(const weather::FogMesh& mesh,bool topologyInitialized) {
    if(capturePath.empty()) return;
    staged.mesh.vertices=mesh.vertices;
    if(topologyInitialized) { staged.mesh.indices=mesh.indices; haveMesh=true; }
}
bool captureFogFrame(const float* world,const float* view,const float* projection,std::string& result) {
    if(!requested||!haveMesh) return false;
    requested=false;
    std::copy_n(world,16,staged.world.begin());
    std::copy_n(view,16,staged.view.begin());
    std::copy_n(projection,16,staged.projection.begin());
    try { writeFogSnapshot(capturePath,staged); result.clear(); }
    catch(const std::exception& error) { result=error.what(); }
    return true;
}
}
