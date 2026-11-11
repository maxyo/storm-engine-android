#pragma once

// Interface picture lists: named groups, each one texture atlas with named rectangles.
//
// Where the lists live depends on the game, decided here only (pictureIniFiles):
// - To Each His Own: one file, resource\ini\interfaces\pictures.ini;
// - Caribbean Legend: every *.ini under resource\ini\interfaces\pictures\, subfolders included
//   (the game splits its lists into pictures.ini, icons.ini, portraits.ini, ... and mods add
//   their own pictures.ini under pictures\mods\<mod>\). The files are read in case-insensitive
//   path order so picture indices do not depend on the file system.
// The section format is the same in both games:
//   [GROUP]  sTextureName = <path under resource\textures\interfaces>,
//            wTextureWidth / wTextureHeight (atlas size, default 1024),
//            picture = <name>,<left>,<top>,<right>,<bottom>  (repeated; text after the
//            fourth number is ignored, as the original engine's scanf did).
// A group name may appear only once across all files, and every picture line must carry a name
// and four numbers: both are data errors and throw, naming the file and the group.

#include "game_profile.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace storm::xinterface
{
struct PictureIniSection
{
    std::string name;
    std::string texture;
    int32_t width = 1024;
    int32_t height = 1024;
    std::vector<std::string> pictures; // raw "picture" values in file order
};

struct Picture
{
    std::string name;
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

struct PictureGroup
{
    std::string name;
    std::string texture;
    int32_t width = 0;
    int32_t height = 0;
    int32_t pictureStart = 0; // index of the group's first picture in PictureCatalog::pictures
    int32_t pictureCount = 0;
};

// Parses one "picture" value; false when it lacks a name or one of the four numbers.
bool parsePicture(std::string_view value, Picture &picture);

class PictureCatalog
{
  public:
    // Appends the groups of one file; `source` names the file in errors.
    void add(std::string_view source, const std::vector<PictureIniSection> &sections);

    const std::vector<PictureGroup> &groups() const
    {
        return groups_;
    }
    const std::vector<Picture> &pictures() const
    {
        return pictures_;
    }

  private:
    std::vector<PictureGroup> groups_;
    std::vector<Picture> pictures_;
};

// The picture list files of the game, as game-relative paths with backslashes.
// `listIni` returns the *.ini files under a game-relative directory, recursively, as paths
// relative to that directory (any separator); it is only called for Caribbean Legend.
std::vector<std::string> pictureIniFiles(GameProfile profile,
                                         const std::function<std::vector<std::string>(const std::string &)> &listIni);
} // namespace storm::xinterface
