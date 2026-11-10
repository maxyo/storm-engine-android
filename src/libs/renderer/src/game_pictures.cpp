#include "game_pictures.hpp"

#include "s_device.h"

#include <storm/render/d3d_interop.hpp>

#include <string>

namespace storm
{

bool GamePictures::Load(std::string_view path, Loaded &out)
{
    // The texture service resolves names relative to RESOURCE/Textures and swaps the extension for .tx.
    const std::string name = std::string(path) + ".tga";
    const int32_t id = renderer_.TextureCreate(name.c_str());
    if (id < 0)
        return false;
    uint16_t width = 0, height = 0;
    const uint16_t handle = render::BgfxTextureOf(renderer_.GetTextureFromID(id), &width, &height);
    if (handle == UINT16_MAX)
    {
        renderer_.TextureRelease(id);
        return false;
    }
    out.token = static_cast<uint32_t>(id);
    out.bgfx_texture = handle;
    out.width = width;
    out.height = height;
    return true;
}

void GamePictures::Unload(uint32_t token)
{
    renderer_.TextureRelease(static_cast<int32_t>(token));
}

} // namespace storm
