#pragma once
#include "dynamic_vertex_stream.hpp"
#include "vertex_source_lifetime.hpp"
#include <unordered_map>
#include <vector>
namespace storm::model {
// One upload per source per draw. Player positions cannot identify a bone pose:
// head controls, blend weights and action selection change it independently.
// beginPose() therefore invalidates uploads conservatively on every Realize.
template<class Vertex> class SkinningStreamCache {
    struct Entry final : render::VertexSourceLifetime::Dependent {
        std::weak_ptr<render::VertexSourceLifetime> source;
        std::vector<Vertex> vertices;
        std::unique_ptr<render::DynamicVertexStream> stream;
        bool uploaded=false;
        void invalidateSource() noexcept override {
            stream.reset();
            std::vector<Vertex>().swap(vertices);
            uploaded=false;
        }
    };
    std::unordered_map<const void*,std::shared_ptr<Entry>> entries_;
public:
    void clear() {entries_.clear();}
    void beginPose() {
        std::erase_if(entries_, [](auto& item) {
            auto lifetime=item.second->source.lock();
            if (!lifetime || !lifetime->valid()) return true;
            item.second->uploaded=false;
            return false;
        });
    }
    template<class Create,class Skin>
    render::VertexBufferId transform(const void* source,
        const std::shared_ptr<render::VertexSourceLifetime>& lifetime,
        uint32_t count,Create create,Skin skin) {
        if(!source||!lifetime||!lifetime->valid()||!count||count>UINT32_MAX/sizeof(Vertex))
            throw std::invalid_argument("Invalid skinning source");
        auto& slot=entries_[source];
        if(!slot || slot->source.lock()!=lifetime) {
            slot=std::make_shared<Entry>();
            slot->source=lifetime;
            lifetime->observe(slot);
        }
        auto& entry=*slot;
        if(!entry.uploaded||entry.vertices.size()!=count) {
            entry.uploaded=false;
            entry.vertices.resize(count);
            skin(entry.vertices.data());
            if(!entry.stream) entry.stream=create();
            entry.stream->upload(std::as_bytes(std::span<const Vertex>(entry.vertices)));
            entry.uploaded=true;
        }
        return entry.stream->id();
    }
};
}
