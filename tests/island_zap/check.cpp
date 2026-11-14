// storm::island::ZapCachePath (src/libs/island/include/island_zap.hpp): the zipped island maps made when the game does not
// ship them go under the cache root, never into the game folder.
#include "island_zap.hpp"

#include <cstdio>
#include <string>

namespace
{
int failures = 0;
void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    failures += ok ? 0 : 1;
}
bool Inside(const std::filesystem::path &root, const std::filesystem::path &path)
{
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}
} // namespace

int main()
{
    namespace fsys = std::filesystem;
    const fsys::path cache = "/data/cache", game = "/data/game";
    const std::string file = "resource/foam/Martinique/Martinique.tga";
    const fsys::path zap = storm::island::ZapCachePath(cache, file);
    expect(Inside(cache, zap), "the zap is under the cache root");
    expect(!Inside(game, zap) && !Inside(game / file, zap), "the zap is not under the game folder");
    expect(zap.extension() == ".zap" && zap.filename() == "Martinique.tga.zap", "named after the game file");
    expect(storm::island::ZapCachePath(cache, "resource/foam/./a/../b/c.tga") == cache / "Island/resource/foam/b/c.tga.zap",
           "a path is normalised");
    for (const char *bad : {"../x.tga", "/etc/x.tga", ""})
    {
        bool threw = false;
        try
        {
            storm::island::ZapCachePath(cache, bad);
        }
        catch (const std::invalid_argument &)
        {
            threw = true;
        }
        expect(threw, "a path outside the game folder is refused");
    }
    if (failures)
        return 1;
    std::printf("PASS: island_zap\n");
    return 0;
}
