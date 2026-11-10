// The game's picture atlases: RESOURCE/INI/interfaces/pictures.ini of the player's game
// names every interface picture as a rectangle of a texture ("GOODS" / "Balls" →
// goods.tga, 128,256,256,384). Interface models refer to pictures that way, so the UI
// reads the same file at run time; nothing of it is bundled.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace storm::ui
{

struct AtlasPicture
{
    std::string texture; // as <ui-game-image texture>: "INTERFACES/goods"
    float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
};

class PictureAtlas
{
  public:
    // Parses the text of pictures.ini; returns the number of pictures read.
    size_t Load(std::string_view ini);
    // Group and picture names are case-insensitive, as in the engine.
    const AtlasPicture *Find(std::string_view group, std::string_view picture) const;

  private:
    std::unordered_map<std::string, AtlasPicture> pictures_; // "GROUP/PICTURE" upper case
};

} // namespace storm::ui
