// Host checks for platform/script_patches.hpp: apply, base mismatch, all-or-nothing
// fixes, set selection, damaged layers and cache invalidation.
#include "platform/path_fingerprint.hpp"
#include "platform/resource_overlay.hpp"
#include "platform/script_patches.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

namespace fs = std::filesystem;
using namespace storm::script_patches;
using Paths = std::unordered_map<std::string, std::string>;

#define CHECK(condition)                                                                                       \
    do                                                                                                         \
    {                                                                                                          \
        if (!(condition))                                                                                      \
        {                                                                                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #condition "\n";                    \
            std::exit(1);                                                                                      \
        }                                                                                                      \
    } while (0)

namespace
{
void write(const fs::path &path, std::string_view data)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << data;
}

std::string read(const fs::path &path)
{
    std::string data;
    CHECK(detail::readFile(path, data));
    return data;
}

bool throwsLayerError(const std::function<void()> &call, std::string_view expected)
{
    try
    {
        call();
    }
    catch (const LayerError &error)
    {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos)
            return true;
        std::cerr << "unexpected LayerError: " << error.what() << "\n";
        return false;
    }
    return false;
}

// The player's game: CRLF scripts, as the real games ship them.
const std::string kIdentity = "#define VERSION \"1.0\"\r\n";
const std::string kDialog = "void A()\r\n{\r\n\tSetTimeScale(0.0);\r\n\tB();\r\n}\r\n";
const std::string kDialogFixed = "void A()\r\n{\r\n\tSetTimeScale(0.0);\r\n\tResetCounter();\r\n\tB();\r\n}\r\n";
const std::string kDialogPatch = "--- a/PROGRAM/dialog.c\n+++ b/PROGRAM/dialog.c\n@@ -2,3 +2,4 @@\n {\r\n"
                                 " \tSetTimeScale(0.0);\r\n+\tResetCounter();\r\n \tB();\r\n";
const std::string kQuest = "int Q()\r\n{\r\n\treturn 1;\r\n}";
const std::string kQuestFixed = "int Q()\r\n{\r\n\treturn 2;\r\n}";
const std::string kQuestPatch = "--- a/PROGRAM/quests/q.c\n+++ b/PROGRAM/quests/q.c\n@@ -3,2 +3,2 @@\n-\treturn 1;\r\n"
                                "+\treturn 2;\r\n }\n\\ No newline at end of file\n";
const std::string kSea = "float S = 1.0;\n";
const std::string kSeaFixed = "float S = 2.0;\n";
const std::string kSeaPatch = "--- a/PROGRAM/sea_ai/s.c\n+++ b/PROGRAM/sea_ai/s.c\n@@ -1 +1 @@\n-float S = 1.0;\n+float S = 2.0;\n";

struct Fixture
{
    fs::path game, layer, derived;
    Paths paths;

    explicit Fixture(const fs::path &root) : game(root / "game"), layer(root / "layer"), derived(root / "derived")
    {
        fs::remove_all(root);
        write(game / "PROGRAM/version.h", kIdentity);
        write(game / "PROGRAM/dialog.c", kDialog);
        write(game / "PROGRAM/quests/q.c", kQuest);
        write(game / "PROGRAM/sea_ai/s.c", kSea);
        write(layer / "sets/test-1.0/dialog.patch", kDialogPatch);
        write(layer / "sets/test-1.0/q.patch", kQuestPatch);
        write(layer / "sets/test-1.0/s.patch", kSeaPatch);
        writeIndex();
        reindex();
    }

    // A fix that touches two scripts (time-scale: dialog.c + quests/q.c) and an
    // independent one (sea).
    void writeIndex(const std::string &extra = "")
    {
        write(layer / "sets/test-1.0/index.txt",
              "storm-script-patches\t1\n"
              "set\ttest-1.0\tTest Game\t1.0\n"
              "identify\tPROGRAM/version.h\t" + sha256(kIdentity) + "\n"
              "target\tPROGRAM/dialog.c\t" + sha256(kDialog) + "\t" + sha256(kDialogFixed) + "\tdialog.patch\n"
              "target\tPROGRAM/quests/q.c\t" + sha256(kQuest) + "\t" + sha256(kQuestFixed) + "\tq.patch\n"
              "target\tPROGRAM/sea_ai/s.c\t" + sha256(kSea) + "\t" + sha256(kSeaFixed) + "\ts.patch\n"
              "fix\ttime-scale\tPROGRAM/dialog.c,PROGRAM/quests/q.c\tGPL-3.0\thttps://example.org/src\tTime scale\n"
              "fix\tsea\tPROGRAM/sea_ai/s.c\tGPL-3.0\thttps://example.org/src\tSea\n" + extra);
    }

    // As the file service's resource index: lowercase relative key -> real path.
    void reindex()
    {
        paths.clear();
        for (const auto &entry : fs::recursive_directory_iterator(game))
            if (entry.is_regular_file())
                paths[storm::android::resourceKey(entry.path().lexically_relative(game))] = entry.path().string();
    }

    Report apply() { return applyLayer(layer, derived, paths); }
    std::string resolved(const std::string &key) { return read(paths.at(key)); }
};

void checkSha256()
{
    CHECK(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

void checkWhitelist()
{
    for (const char *path : {"PROGRAM/dialog.c", "program/quests/quests_reaction.c", "PROGRAM/dialogs/russian/x.c",
                             "program/dialogs/english/Quest/a.c", "program/Loc_ai/LAi_utilites.c",
                             "program/sea_ai/sea.c", "program/weather/whr.h", "program/battle_interface/bi.c"})
    {
        CHECK(isPatchTargetPath(path));
        CHECK(!storm::android::isUiOverlayPath(path)); // the two layers never claim the same file
    }
    for (const char *path : {"program/interface/mainmenu.c", "program/controls/init.c", "program/dialogs/x.c",
                             "program/seadogs.c", "program/quests/q.txt", "../program/dialog.c",
                             "/program/dialog.c", "program/quests/../seadogs.c", "resource/ini/x.ini"})
        CHECK(!isPatchTargetPath(path));
}

void checkUnifiedDiff()
{
    CHECK(applyUnifiedDiff(kDialog, kDialogPatch, "PROGRAM/dialog.c") == kDialogFixed);
    CHECK(applyUnifiedDiff(kQuest, kQuestPatch, "PROGRAM/quests/q.c") == kQuestFixed);
    // Insertion before the first line: empty old range at 0.
    CHECK(applyUnifiedDiff("b\n", "--- a/x.c\n+++ b/x.c\n@@ -0,0 +1 @@\n+a\n", "x.c") == "a\nb\n");
    // Exact context: the same hunk does not apply one line off or to LF text.
    CHECK(throwsLayerError([] { applyUnifiedDiff("\r\n" + kDialog, kDialogPatch, "PROGRAM/dialog.c"); }, "does not match"));
    std::string lf = kDialog;
    std::erase(lf, '\r');
    CHECK(throwsLayerError([&] { applyUnifiedDiff(lf, kDialogPatch, "PROGRAM/dialog.c"); }, "does not match"));
    CHECK(throwsLayerError([] { applyUnifiedDiff(kDialog, kDialogPatch, "PROGRAM/other.c"); }, "must start with"));
    CHECK(throwsLayerError([] { applyUnifiedDiff("a\n", "--- a/x.c\n+++ b/x.c\n@@ -1 +1,2 @@\n a\n", "x.c"); },
                           "line counts"));
    CHECK(throwsLayerError([] { applyUnifiedDiff("a\n", "--- a/x.c\n+++ b/x.c\n@@ -1 +1 @@\n-a\n+b", "x.c"); },
                           "line feed"));
}

void checkIndexErrors(const fs::path &root)
{
    const fs::path set = root / "bad-1";
    const auto parse = [&](const std::string &body) { return [=] { parseIndex("storm-script-patches\t1\n" + body, set); }; };
    const std::string head = "set\tbad-1\tG\t1\nidentify\tPROGRAM/v.h\t" + sha256("v") + "\n";
    const std::string target = "target\tPROGRAM/dialog.c\t" + sha256("a") + "\t" + sha256("b") + "\td.patch\n";
    const std::string fix = "fix\tf\tPROGRAM/dialog.c\tGPL-3.0\thttps://example.org\tF\n";
    parse(head + target + fix)();
    CHECK(throwsLayerError([&] { parseIndex("storm-script-patches\t2\n", set); }, "format 1"));
    CHECK(throwsLayerError(parse(target + fix), "no set record"));
    CHECK(throwsLayerError(parse("set\tbad-1\tG\t1\n" + target + fix), "does not identify"));
    CHECK(throwsLayerError(parse("set\tother\tG\t1\n"), "differs from its directory"));
    CHECK(throwsLayerError(parse(head + "target\tPROGRAM/interface/m.c\t" + sha256("a") + "\t" + sha256("b") +
                                 "\tm.patch\n"),
                           "not an allowed patch target"));
    CHECK(throwsLayerError(parse(head + "target\tPROGRAM/dialog.c\t" + sha256("a") + "\t" + sha256("b") +
                                 "\t../d.patch\n"),
                           "target needs"));
    CHECK(throwsLayerError(parse(head + target), "belongs to no fix"));
    CHECK(throwsLayerError(parse(head + target + target + fix), "duplicate target"));
    CHECK(throwsLayerError(parse(head + target + fix + fix), "duplicate fix"));
    CHECK(throwsLayerError(parse(head + fix + target), "unknown target"));
    CHECK(throwsLayerError(parse(head + target + "fix\tf\tPROGRAM/dialog.c\t\thttps://x\tF\n"), "fix needs"));
    CHECK(throwsLayerError([&] { parseIndex("storm-script-patches\t1\r\n", set); }, "CR in index"));
}

void checkApply(const fs::path &root)
{
    Fixture f(root / "apply");
    const auto original = f.paths.at("program/dialog.c");
    const auto report = f.apply();
    CHECK(report.set == "test-1.0" && report.game == "Test Game" && report.version == "1.0");
    CHECK((report.applied == std::vector<std::string>{"time-scale", "sea"}));
    CHECK(report.unapplied.empty() && report.errors.empty() && report.noticeKey().empty());
    CHECK(f.resolved("program/dialog.c") == kDialogFixed);
    CHECK(f.resolved("program/quests/q.c") == kQuestFixed);
    CHECK(f.resolved("program/sea_ai/s.c") == kSeaFixed);
    CHECK(read(original) == kDialog); // the player's file is never written
    CHECK(fs::path(f.paths.at("program/dialog.c")).string().starts_with(f.derived.string()));

    // A second start reuses the derived files without rewriting them, so the
    // compiled script cache is not invalidated by the patch layer itself.
    const auto derivedFile = f.paths.at("program/dialog.c");
    const auto written = fs::last_write_time(derivedFile);
    fs::last_write_time(derivedFile, written - std::chrono::hours(1));
    const auto earlier = fs::last_write_time(derivedFile);
    f.reindex();
    f.apply();
    CHECK(f.paths.at("program/dialog.c") == derivedFile);
    CHECK(fs::last_write_time(derivedFile) == earlier);

    // A damaged derived file is rewritten.
    write(derivedFile, "garbage");
    f.reindex();
    f.apply();
    CHECK(f.resolved("program/dialog.c") == kDialogFixed);
}

void checkBaseMismatch(const fs::path &root)
{
    Fixture f(root / "mismatch");
    write(f.game / "PROGRAM/quests/q.c", kQuest + "\r\n// modded");
    f.reindex();
    const auto report = f.apply();
    // The two-file fix is withheld entirely: dialog.c matches but is not patched.
    CHECK((report.applied == std::vector<std::string>{"sea"}));
    CHECK(report.unapplied.size() == 1 && report.unapplied[0].fix == "time-scale");
    CHECK(report.unapplied[0].reasons.size() == 1);
    CHECK(report.unapplied[0].reasons[0].find("PROGRAM/quests/q.c differs") != std::string::npos);
    CHECK(report.errors.size() == 1 && report.errors[0].find("NOT applied") != std::string::npos);
    CHECK(f.resolved("program/dialog.c") == kDialog);
    CHECK(f.resolved("program/quests/q.c") == kQuest + "\r\n// modded");
    CHECK(f.resolved("program/sea_ai/s.c") == kSeaFixed);
    CHECK(!report.noticeKey().empty());
    CHECK(noticeText(report, false).find("Time scale [time-scale]") != std::string::npos);
    CHECK(noticeText(report, true).find("Time scale") != std::string::npos);
    // Same mismatch, same notice key (shown once); a different one changes it.
    f.reindex();
    CHECK(f.apply().noticeKey() == report.noticeKey());
    write(f.game / "PROGRAM/quests/q.c", kQuest + "\r\n// other mod");
    f.reindex();
    CHECK(f.apply().noticeKey() != report.noticeKey());

    // A missing target is a mismatch too.
    fs::remove(f.game / "PROGRAM/sea_ai/s.c");
    f.reindex();
    const auto missing = f.apply();
    CHECK(missing.applied.empty() && missing.unapplied.size() == 2);
    CHECK(missing.unapplied[1].reasons[0].find("missing from the game") != std::string::npos);

    // A fix sharing a target with an unapplied fix is withheld as well.
    Fixture g(root / "shared");
    g.writeIndex("fix\talso-dialog\tPROGRAM/dialog.c\tGPL-3.0\thttps://example.org/src\tAlso dialog\n");
    write(g.game / "PROGRAM/quests/q.c", "changed");
    g.reindex();
    const auto shared = g.apply();
    CHECK(shared.unapplied.size() == 2 && shared.unapplied[1].fix == "also-dialog");
    CHECK(shared.unapplied[1].reasons[0].find("shares a script") != std::string::npos);
    CHECK(g.resolved("program/dialog.c") == kDialog);
}

void checkSetSelection(const fs::path &root)
{
    Fixture f(root / "select");
    write(f.game / "PROGRAM/version.h", "#define VERSION \"2.0\"\r\n");
    f.reindex();
    const auto none = f.apply();
    CHECK(none.layerPresent && none.set.empty() && none.applied.empty() && none.unapplied.empty());
    CHECK(f.resolved("program/dialog.c") == kDialog);
    CHECK(none.noticeKey().empty()); // other games are not told about fixes not meant for them
    write(f.derived / "0123/PROGRAM/dialog.c", "derived for the previous game");
    f.apply();
    CHECK(!fs::exists(f.derived / "0123")); // nor do they keep its derived files

    Fixture g(root / "ambiguous");
    fs::copy(g.layer / "sets/test-1.0", g.layer / "sets/test-copy");
    auto index = read(g.layer / "sets/test-copy/index.txt");
    index.replace(index.find("set\ttest-1.0"), 12, "set\ttest-copy");
    write(g.layer / "sets/test-copy/index.txt", index);
    CHECK(throwsLayerError([&] { g.apply(); }, "both identify this game"));

    Fixture h(root / "absent");
    fs::remove_all(h.layer);
    const auto absent = h.apply();
    CHECK(!absent.layerPresent && absent.applied.empty());
    CHECK(h.resolved("program/dialog.c") == kDialog);
}

void checkDamagedLayer(const fs::path &root)
{
    Fixture f(root / "damaged");
    // A patch that does not fit its own verified base.
    write(f.layer / "sets/test-1.0/s.patch", "--- a/PROGRAM/sea_ai/s.c\n+++ b/PROGRAM/sea_ai/s.c\n@@ -1 +1 @@\n-x\n+y\n");
    CHECK(throwsLayerError([&] { f.apply(); }, "does not match the base"));
    // A patch whose result is not the declared one.
    write(f.layer / "sets/test-1.0/s.patch", "--- a/PROGRAM/sea_ai/s.c\n+++ b/PROGRAM/sea_ai/s.c\n@@ -1 +1 @@\n"
                                             "-float S = 1.0;\n+float S = 3.0;\n");
    CHECK(throwsLayerError([&] { f.apply(); }, "expected " + sha256(kSeaFixed)));
    fs::remove(f.layer / "sets/test-1.0/s.patch");
    CHECK(throwsLayerError([&] { f.apply(); }, "is missing"));
    fs::remove_all(f.layer / "sets"); // a layer with no sets patches nothing
    const auto empty = f.apply();
    CHECK(empty.layerPresent && empty.set.empty() && empty.unapplied.empty());
    CHECK(f.resolved("program/dialog.c") == kDialog);
}

void checkCacheInvalidation(const fs::path &root)
{
    Fixture f(root / "cache");
    f.apply();
    const auto first = f.paths.at("program/sea_ai/s.c");
    const auto layerPrint = storm::fingerprint::tree(f.layer);

    // A new patch version for the same base: a new derived file, the old one is removed.
    const std::string fixed2 = "float S = 4.0;\n";
    write(f.layer / "sets/test-1.0/s.patch",
          "--- a/PROGRAM/sea_ai/s.c\n+++ b/PROGRAM/sea_ai/s.c\n@@ -1 +1 @@\n-float S = 1.0;\n+float S = 4.0;\n");
    auto index = read(f.layer / "sets/test-1.0/index.txt");
    index.replace(index.find(sha256(kSeaFixed)), 64, sha256(fixed2));
    write(f.layer / "sets/test-1.0/index.txt", index);
    f.reindex();
    f.apply();
    CHECK(f.resolved("program/sea_ai/s.c") == fixed2);
    CHECK(f.paths.at("program/sea_ai/s.c") != first);
    CHECK(!fs::exists(first));
    // The compiled script cache key covers the patch layer (GetPathFingerprint).
    CHECK(storm::fingerprint::tree(f.layer) != layerPrint);
    const auto layers = storm::android::programLayers("/internal");
    CHECK(std::find(layers.begin(), layers.end(), fs::path("/internal/script-patches/current")) != layers.end());

    // Stale derived files of bases the player no longer has are dropped.
    write(f.derived / "0123/PROGRAM/old.c", "old");
    f.reindex();
    f.apply();
    CHECK(!fs::exists(f.derived / "0123"));
}
} // namespace

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const fs::path root(argv[1]);
    checkSha256();
    checkWhitelist();
    checkUnifiedDiff();
    checkIndexErrors(root);
    checkApply(root);
    checkBaseMismatch(root);
    checkSetSelection(root);
    checkDamagedLayer(root);
    checkCacheInvalidation(root);
    std::puts("script patches: all checks passed");
}
