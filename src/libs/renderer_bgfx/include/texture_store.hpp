#pragma once
#include "rgba_texture.hpp"
#include <memory>
#include <stdexcept>
#include <unordered_map>
namespace storm::render {
class TextureStore final: public TextureColorReader {
public:
    int32_t create(const TexturePixels& pixels) {
        if(next_==INT32_MAX) throw std::overflow_error("Texture ID space exhausted");
        auto texture=std::make_unique<RgbaTexture>(pixels);
        const int32_t id=next_++;
        textures_.emplace(id,std::move(texture));
        return id;
    }
    void release(int32_t id) {textures_.erase(id);}
    bool sampleTextureArgb(int32_t id,float u,float v,uint32_t& color) override {
        const auto it=textures_.find(id);
        return it!=textures_.end()&&it->second->sample(u,v,color);
    }
    bgfx::TextureHandle resolve(int32_t id) const {
        const auto it=textures_.find(id);
        if(it==textures_.end()) throw std::invalid_argument("Missing bgfx texture binding");
        return it->second->handle();
    }
private:
    int32_t next_=0; // IDs are never reused, so released bindings cannot alias.
    std::unordered_map<int32_t,std::unique_ptr<RgbaTexture>> textures_;
};
}
