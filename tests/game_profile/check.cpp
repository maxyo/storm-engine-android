#include "game_profile.hpp"
#include "script_version.hpp"

#include <cassert>
#include <iostream>
#include <set>
#include <string>

using storm::GameProfile;

static GameProfile detect(const std::set<std::string> &paths)
{
    return storm::detectGameProfile([&](const std::string &path) { return paths.count(path) != 0; });
}

static std::string error(const std::set<std::string> &paths)
{
    try
    {
        detect(paths);
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
    const std::set<std::string> base{"engine.ini", "program", "resource"};
    auto teho = base;
    teho.insert("program/_lsc_on_off.h");
    assert(detect(teho) == GameProfile::TEHO);

    auto cl = teho;
    cl.insert({"resource/texture_library.json", "resource/shaders"});
    assert(detect(cl) == GameProfile::CaribbeanLegend);

    auto partial = teho;
    partial.insert("resource/shaders");
    assert(error(partial).find("Incomplete Caribbean Legend") != std::string::npos);
    assert(error(partial).find("resource/texture_library.json is missing") != std::string::npos);

    assert(error(base).find("Unknown game") != std::string::npos);
    assert(error({"program", "resource"}) == "Not a game directory: missing engine.ini");
    assert(error({}) == "Not a game directory: missing engine.ini, program, resource");

    // The iScriptVersion each game's scripts declare (TEHO: 54128, CL: 2230981).
    static_assert(storm::engineScriptVersion(GameProfile::TEHO) == 54128);
    static_assert(storm::engineScriptVersion(GameProfile::CaribbeanLegend) == 2230981);
    std::cout << "game profile: TEHO, CL, incomplete CL, unknown and missing layouts, script versions PASS\n";
}
