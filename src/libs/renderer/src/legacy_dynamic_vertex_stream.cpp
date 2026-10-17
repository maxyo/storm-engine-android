#include "legacy_dynamic_vertex_stream.hpp"
#include "dx9render.h"
#include <cstring>
namespace storm::render {
namespace {
class LegacyDynamicVertexStream final:public DynamicVertexStream {
    VDX9RENDER& device_;
    int32_t id_=-1;
    uint32_t count_=0;
public:
    explicit LegacyDynamicVertexStream(VDX9RENDER& device):device_(device) {}
    ~LegacyDynamicVertexStream() override {if(id_>=0) device_.ReleaseVertexBuffer(id_);}
    VertexBufferId id() const override {return {id_};}
    void upload(std::span<const std::byte> bytes) override {
        const auto count=skinnedVertexCount(bytes);
        const bool replace=id_<0||count!=count_;
        int32_t next=id_;
        if(replace) next=device_.CreateVertexBuffer(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1,
            bytes.size(),D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC,D3DPOOL_DEFAULT);
        if(next<0) throw std::runtime_error("Dynamic vertex allocation failed");
        auto* mapped=device_.LockVertexBuffer(next,D3DLOCK_DISCARD|D3DLOCK_NOSYSLOCK);
        if(!mapped) {
            if(replace) device_.ReleaseVertexBuffer(next);
            throw std::runtime_error("Dynamic vertex upload failed");
        }
        std::memcpy(mapped,bytes.data(),bytes.size());device_.UnLockVertexBuffer(next);
        if(replace&&id_>=0) device_.ReleaseVertexBuffer(id_);
        id_=next;count_=count;
    }
};
}
std::unique_ptr<DynamicVertexStream> makeLegacyDynamicVertexStream(VDX9RENDER& device) {
    return std::make_unique<LegacyDynamicVertexStream>(device);
}
}
