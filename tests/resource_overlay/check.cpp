#include "platform/resource_overlay.hpp"
#include <cassert>
#include <fstream>
int main(int argc, char **argv)
{
    assert(argc == 2);
    namespace fs = std::filesystem;
    using namespace storm::android;
    const fs::path root(argv[1]);
    fs::create_directories(root / "PROGRAM/interface");
    fs::create_directories(root / "RESOURCE/MODELS");
    std::ofstream(root / "PROGRAM/interface/mainmenu.c") << "// mobile";
    std::ofstream(root / "RESOURCE/MODELS/excluded.gm") << "not eligible";
    std::unordered_map<std::string, std::string> paths{{"program/interface/mainmenu.c", "original"},
                                                     {"resource/models/original.gm", "user asset"}};
    indexUiOverlay(root, paths);
    assert(paths.at("program/interface/mainmenu.c") == (root / "PROGRAM/interface/mainmenu.c").string());
    assert(paths.at("resource/models/original.gm") == "user asset");
    assert(!paths.contains("resource/models/excluded.gm"));
    assert(!isUiOverlayPath("../program/interface/mainmenu.c"));
    assert(!isUiOverlayPath("/program/interface/mainmenu.c"));
    assert(!isUiOverlayPath("PROGRAM/interface/setup.exe"));
    assert(isUiOverlayPath("RESOURCE/INI/INTERFACES/mainmenu.ini"));
    fs::create_symlink(root / "RESOURCE/MODELS/excluded.gm", root / "PROGRAM/interface/link.c");
    indexUiOverlay(root, paths);
    assert(!paths.contains("program/interface/link.c"));
}
