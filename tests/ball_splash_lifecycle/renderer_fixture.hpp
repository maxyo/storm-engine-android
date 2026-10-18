#pragma once
#include <cassert>
#include <cstdint>
#include <map>
#include <vector>
struct IDirect3DDevice9 {};
struct VDX9RENDER {
    int next=1, created=0, released=0;
    std::map<int,std::vector<uint16_t>> indices;
    std::map<int,int> vertices,textures;
    ~VDX9RENDER(){assert(indices.empty()&&vertices.empty()&&textures.empty());}
    int CreateIndexBuffer(int bytes,int){int id=next++;++created;indices[id].resize(bytes/2);return id;}
    int CreateVertexBuffer(int,int bytes,int){int id=next++;++created;vertices[id]=bytes;return id;}
    int TextureCreate(const char *){int id=next++;++created;textures[id]=1;return id;}
    void *LockIndexBuffer(int id){assert(indices.contains(id));return indices.at(id).data();}
    void UnLockIndexBuffer(int id){assert(indices.contains(id));}
    void ReleaseIndexBuffer(int id){assert(indices.erase(id)==1);++released;}
    void ReleaseVertexBuffer(int id){assert(vertices.erase(id)==1);++released;}
    void TextureRelease(int id){assert(textures.erase(id)==1);++released;}
};
