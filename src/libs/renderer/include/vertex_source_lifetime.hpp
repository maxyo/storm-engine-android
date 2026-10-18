#pragma once
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace storm::render {
// CPU source allocations outlive individual draws, but their addresses are not
// identities. Explicit invalidation runs before freeing/replacing source bytes,
// even when a descriptor copy still holds this token. Render-thread only.
class VertexSourceLifetime {
public:
    struct Dependent {
        virtual ~Dependent() = default;
        virtual void invalidateSource() noexcept = 0;
    };
    VertexSourceLifetime() = default;
    VertexSourceLifetime(const VertexSourceLifetime&) = delete;
    VertexSourceLifetime& operator=(const VertexSourceLifetime&) = delete;
    ~VertexSourceLifetime() { invalidate(); }
    bool valid() const noexcept { return valid_; }
    void observe(const std::shared_ptr<Dependent>& dependent) {
        if (!valid_) throw std::invalid_argument("Released vertex source");
        std::erase_if(dependents_, [](const auto& item) { return item.expired(); });
        dependents_.push_back(dependent);
    }
    void invalidate() noexcept {
        if (!valid_) return;
        valid_ = false;
        for (const auto& item : dependents_)
            if (auto dependent = item.lock()) dependent->invalidateSource();
        dependents_.clear();
    }
private:
    bool valid_ = true;
    std::vector<std::weak_ptr<Dependent>> dependents_;
};
}
