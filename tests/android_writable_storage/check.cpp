#include "platform/android_writable_storage.hpp"
#include "platform/checked_file_io.hpp"

#include <cassert>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;
using storm::android::writable::Storage;

static void put(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
    file.close();
    assert(!file.fail());
}

static std::string get(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file.is_open());
    return {std::istreambuf_iterator<char>(file), {}};
}

template <typename Operation> static void rejected(Operation operation)
{
    bool failed = false;
    try { operation(); }
    catch (const std::exception &) { failed = true; }
    assert(failed);
}

static void storageChecks(const fs::path &root)
{
    const auto game = root / "external/TEHO";
    const auto user = root / "private/files/storm";
    fs::create_directories(game);
    fs::create_directories(user.parent_path());
    Storage storage(game, user);
    const auto saves = user / "SaveData";
    assert(storage.resolve("SAVE") == saves);
    assert(storage.resolve(".\\sAvE\\Player One\\slot") == saves / "Player One/slot");
    assert(storage.resolve("save/p/../q/slot") == saves / "q/slot");
    assert(storage.resolve((game / "SAVE/p/slot").string()) == saves / "p/slot");
    assert(storage.resolve("OPTIONS") == user / "options");
    assert(storage.resolve((user / "options.storm-tmp").string()) == user / "options.storm-tmp");
    assert(storage.resolve((game / "options").string()) == user / "options");
    assert(!storage.resolve("savegame/a"));
    assert(!storage.resolve("RESOURCE/SAVE/a"));
    assert(!storage.resolve((root / "unrelated/save/a").string()));
    rejected([&] { storage.resolve("SAVE/../options"); });
    rejected([&] { storage.resolve("SAVE/p/../../outside"); });
    rejected([&] { storage.resolve((user / "../outside").string()); });

    put(game / "SAVE/Player One/Slot A", "legacy A");
    put(game / "SAVE/Player One/Slot B", "legacy B");
    put(game / "SAVE/Капитан/рейс", "unicode save");
    put(game / "options", "legacy options");
    put(saves / "player one/slot a", "private wins");
    storage.migrate();
    assert(get(saves / "player one/slot a") == "private wins");
    assert(get(saves / "player one/Slot B") == "legacy B");
    assert(fs::last_write_time(saves / "player one/Slot B") == fs::last_write_time(game / "SAVE/Player One/Slot B"));
    assert(get(saves / "Капитан/рейс") == "unicode save");
    assert(get(user / "options") == "legacy options");
    assert(get(game / "SAVE/Player One/Slot A") == "legacy A");
    assert(get(game / "options") == "legacy options");
    assert(storage.resolve("SaVe/PLAYER ONE/SLOT B") == saves / "player one/Slot B");
    assert(storage.resolve((saves / "player one/Slot B").string()) == saves / "player one/Slot B");
    assert(storage.resolve("save/player one/*") == saves / "player one/*");
    // Double resolution used by XI_FindFolders; listing and delete see the same files.
    const auto listing = storage.resolve(storage.resolve("SAVE/player one")->string()).value();
    assert(std::distance(fs::directory_iterator(listing), fs::directory_iterator{}) == 2);
    assert(fs::file_size(storage.resolve("save/player one/slot b").value()) == 8);
    assert(fs::last_write_time(storage.resolve("save/player one/slot b").value()) != fs::file_time_type{});
    fs::remove(storage.resolve("save/player one/slot b").value());
    storage.migrate();
    assert(!fs::exists(saves / "player one/Slot B")); // completed source cannot resurrect deletes
    assert(get(game / "SAVE/Player One/Slot B") == "legacy B");
    const auto fresh = storage.resolve("SAVE/New Profile/new slot").value();
    put(fresh, "new save");
    assert(get(storage.resolve("save/NEW PROFILE/NEW SLOT").value()) == "new save");
    assert(!fs::exists(game / "SAVE/New Profile"));
    fs::remove_all(storage.resolve("save/new profile").value());
    assert(!fs::exists(fresh));

    // Imported resources share private storage but are migrated once per source.
    const auto imported = root / "external/app/game";
    put(imported / "save/Imported/save", "imported save");
    put(imported / "options", "import options must not overwrite private");
    Storage importStorage(imported, user);
    importStorage.migrate();
    assert(importStorage.resolve("SAVE/player one/slot a") == storage.resolve("SAVE/player one/slot a"));
    assert(get(saves / "Imported/save") == "imported save");
    assert(get(user / "options") == "legacy options");
    fs::remove(saves / "Imported/save");
    importStorage.migrate();
    assert(!fs::exists(saves / "Imported/save"));

    fs::create_directory_symlink(game, saves / "linked");
    rejected([&] { storage.resolve("SAVE/linked/options"); });
    rejected([&] { storage.resolve("SAVE/linked/../player one"); });
    fs::remove(saves / "linked");
    put(saves / "player one/SLOT A", "ambiguous duplicate");
    rejected([&] { storage.resolve("save/player one/slot a"); });
}

static void failureChecks(const fs::path &root)
{
    const auto game = root / "failure/game";
    const auto user = root / "failure/private/storm";
    put(game / "SAVE/player/slot", "original");
    put(game / "outside", "must not follow");
    fs::create_directories(user.parent_path());
    fs::create_symlink(game / "outside", game / "SAVE/link");
    Storage storage(game, user);
    rejected([&] { storage.migrate(); });
    assert(fs::is_empty(user / ".save-migrations"));
    assert(!fs::exists(user / "SaveData/link"));
    fs::remove(game / "SAVE/link");
    storage.migrate();
    assert(!fs::is_empty(user / ".save-migrations"));
    assert(get(user / "SaveData/player/slot") == "original");
    // Completion files themselves cannot be symlinks or corrupted successes.
    const auto record = fs::directory_iterator(user / ".save-migrations")->path();
    put(record, "invalid");
    rejected([&] { storage.migrate(); });
    fs::remove(record);
    fs::create_symlink(game / "outside", record);
    rejected([&] { storage.migrate(); });
    fs::remove(record);
    storage.migrate();
    for (const auto &entry : fs::recursive_directory_iterator(user))
        assert(entry.path().filename().string().find(".storm-migrate-") != 0);

    const auto conflict = root / "conflict/private/storm";
    put(conflict / "SaveData", "file blocks directory");
    Storage conflicted(game, conflict);
    rejected([&] { conflicted.migrate(); });
    assert(fs::is_empty(conflict / ".save-migrations"));
    assert(get(conflict / "SaveData") == "file blocks directory");
}

static void streamChecks(const fs::path &root)
{
    using storm::io::checked;
    const auto path = root / "stream";
    std::fstream file(path, std::ios::binary | std::ios::out);
    assert(file.is_open());
    const auto mask = std::ios::failbit | std::ios::badbit;
    file.exceptions(mask);
    assert(checked(file, [&] { file.write("saved", 5); }).ok);
    assert(file.exceptions() == mask);
    assert(checked(file, [&] { file.close(); }).ok);
    assert(get(path) == "saved");
    file.open(path, std::ios::binary | std::ios::in);
    char buffer[32];
    assert(!checked(file, [&] { file.read(buffer, sizeof(buffer)); }).ok);
    assert(file.gcount() == 5 && file.fail());
    assert(file.exceptions() == mask);
    assert(!checked(file, [&] { file.read(buffer, 1); }).ok); // already bad with throwing mask
    assert(!checked(file, [&] { file.close(); }).ok); // retains earlier error
    assert(!file.is_open());

    // A buffered small write may succeed and only close reports ENOSPC.
    // This fixture verifies the mechanism, not the Android incident's errno.
    std::fstream full("/dev/full", std::ios::binary | std::ios::out);
    assert(full.is_open());
    assert(checked(full, [&] { full.write("x", 1); }).ok);
    const auto close = checked(full, [&] { full.close(); });
    assert(!close.ok && close.error == ENOSPC && full.fail());
    assert(full.exceptions() == std::ios::goodbit);
    assert(!full.is_open());

    std::fstream large("/dev/full", std::ios::binary | std::ios::out);
    large.exceptions(mask);
    const std::string many(1024 * 1024, 'x');
    assert(!checked(large, [&] { large.write(many.data(), many.size()); }).ok);
    assert(large.exceptions() == mask);
    assert(!checked(large, [&] { large.close(); }).ok);
    std::fstream missing(root / "missing/file", std::ios::out);
    assert(!missing.is_open());
    assert(!checked(missing, [&] { missing.close(); }).ok);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const fs::path root = argv[1];
    storageChecks(root);
    failureChecks(root);
    streamChecks(root);
    std::cout << "PASS: private writable paths, migration, no resurrection, symlink rejection, checked I/O\n";
}
