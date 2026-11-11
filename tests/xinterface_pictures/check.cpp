#include "picture_catalog.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using storm::GameProfile;
using storm::xinterface::Picture;
using storm::xinterface::PictureCatalog;
using storm::xinterface::PictureIniSection;

static std::string addError(PictureCatalog &catalog, const std::string &source,
                            const std::vector<PictureIniSection> &sections)
{
    try
    {
        catalog.add(source, sections);
    }
    catch (const std::runtime_error &e)
    {
        return e.what();
    }
    assert(!"expected an error");
    return {};
}

int main()
{
    // Picture values: name and four numbers; text after the last number is ignored.
    Picture picture;
    assert(storm::xinterface::parsePicture("knob,1,2,30,40", picture));
    assert(picture.name == "knob" && picture.left == 1 && picture.top == 2 && picture.right == 30 &&
           picture.bottom == 40);
    assert(storm::xinterface::parsePicture("dial, 5, 6, 7, 8 trailing note", picture));
    assert(picture.name == "dial" && picture.left == 5 && picture.bottom == 8);
    assert(!storm::xinterface::parsePicture("knob,1,2,30", picture));
    assert(!storm::xinterface::parsePicture(",1,2,3,4", picture));
    assert(!storm::xinterface::parsePicture("", picture));

    // Groups from several files share one picture index space, in file order.
    PictureCatalog catalog;
    catalog.add("first.ini", {{"PANEL", "ui\\panel.tga", 512, 256, {"a,0,0,10,10", "b,10,0,20,10"}},
                              {"EMPTY", "", 1024, 1024, {}}});
    catalog.add("second.ini", {{"ICONS", "ui\\icons.dds", 64, 64, {"c,0,0,32,32"}}});
    assert(catalog.groups().size() == 3 && catalog.pictures().size() == 3);
    assert(catalog.groups()[0].pictureStart == 0 && catalog.groups()[0].pictureCount == 2);
    assert(catalog.groups()[1].pictureCount == 0);
    assert(catalog.groups()[2].name == "ICONS" && catalog.groups()[2].pictureStart == 2);
    assert(catalog.groups()[2].width == 64 && catalog.groups()[2].texture == "ui\\icons.dds");
    assert(catalog.pictures()[2].name == "c" && catalog.pictures()[2].right == 32);

    // A group name may appear once across all files (case-insensitive); malformed lines name their place.
    auto duplicate = addError(catalog, "third.ini", {{"panel", "x.tga", 1024, 1024, {}}});
    assert(duplicate == "picture list third.ini: group [panel] is already defined");
    auto malformed = addError(catalog, "fourth.ini", {{"BROKEN", "x.tga", 1024, 1024, {"bad,1,2"}}});
    assert(malformed.find("fourth.ini: group [BROKEN]: malformed picture 'bad,1,2'") != std::string::npos);

    // Which files each game reads.
    bool listed = false;
    const auto teho = storm::xinterface::pictureIniFiles(GameProfile::TEHO, [&](const std::string &) {
        listed = true;
        return std::vector<std::string>{};
    });
    assert(!listed && teho == std::vector<std::string>{"resource\\ini\\interfaces\\pictures.ini"});

    std::string listedDirectory;
    const auto cl = storm::xinterface::pictureIniFiles(GameProfile::CaribbeanLegend, [&](const std::string &dir) {
        listedDirectory = dir;
        return std::vector<std::string>{"pictures.ini", "mods/Extra/pictures.ini", "Icons.ini", "battle.ini"};
    });
    assert(listedDirectory == "resource\\ini\\interfaces\\pictures");
    const std::vector<std::string> expected{
        "resource\\ini\\interfaces\\pictures\\battle.ini", "resource\\ini\\interfaces\\pictures\\Icons.ini",
        "resource\\ini\\interfaces\\pictures\\mods\\Extra\\pictures.ini",
        "resource\\ini\\interfaces\\pictures\\pictures.ini"};
    assert(cl == expected);

    try
    {
        storm::xinterface::pictureIniFiles(GameProfile::CaribbeanLegend,
                                           [](const std::string &) { return std::vector<std::string>{}; });
        assert(!"expected an error");
    }
    catch (const std::runtime_error &e)
    {
        assert(std::string(e.what()) == "no picture lists (*.ini) under resource\\ini\\interfaces\\pictures");
    }

    std::cout << "xinterface pictures: parsing, catalog, duplicate/malformed errors, TEHO and CL file lists PASS\n";
}
