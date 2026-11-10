#include "save_trash.hpp"

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <set>

namespace fs = std::filesystem;
using namespace storm::save;
using namespace std::chrono_literals;

#define REQUIRE(condition)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << ": failed: " #condition << std::endl;                          \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

static void put(const fs::path &path, const std::string &text)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

static std::string get(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

// What a save list shows: everything except the engine's reserved names.
static std::set<std::string> listed(const fs::path &dir)
{
    std::set<std::string> names;
    for (const auto &entry : fs::directory_iterator(dir))
        if (!isReservedName(entry.path().filename().string()))
            names.insert(entry.path().filename().string());
    return names;
}

static std::set<std::string> all(const fs::path &dir)
{
    std::set<std::string> names;
    for (const auto &entry : fs::directory_iterator(dir))
        names.insert(entry.path().filename().string());
    return names;
}

int main(int, char **argv)
{
    const fs::path dir = fs::path(argv[1]) / "Player One";
    fs::create_directories(dir);
    const auto t0 = Trash::Clock::now();
    using Names = std::set<std::string>;

    {   // Overwrite that completes: delete old slot, write a new name, old one is gone for good.
        Trash trash;
        put(dir / "Port Royal", "old");
        put(dir / "Port Royal.storm-tmp", "torn");
        REQUIRE(trash.discard(dir / "Port Royal", t0));
        REQUIRE(listed(dir).empty() && all(dir) == Names{"Port Royal.storm-deleted"});
        REQUIRE(trash.recover(dir, t0 + 1s) == 0 && listed(dir).empty()); // listing mid-overwrite does not resurrect it
        put(dir / "Port Royal 1", "new");
        trash.commit();
        REQUIRE(all(dir) == Names{"Port Royal 1"} && trash.pendingCount() == 0);
        fs::remove(dir / "Port Royal 1");
    }
    {   // Process dies between the delete and the write: the next session restores the slot.
        {
            Trash dying;
            put(dir / "Tortuga", "precious");
            REQUIRE(dying.discard(dir / "Tortuga", t0));
        } // no commit, no tick: the session ends here
        REQUIRE(all(dir) == Names{"Tortuga.storm-deleted"});
        Trash next;
        REQUIRE(next.recover(dir, t0 + 100s) == 1);
        REQUIRE(all(dir) == Names{"Tortuga"} && get(dir / "Tortuga") == "precious");
        REQUIRE(next.recover(dir, t0 + 101s) == 0); // idempotent
    }
    {   // ...also when the dying session had already started the new file under the same name.
        {
            Trash dying;
            REQUIRE(dying.discard(dir / "Tortuga", t0));
            put(dir / "Tortuga.storm-tmp", "half written"); // atomic write never got to rename
        }
        Trash next;
        REQUIRE(next.recover(dir) == 1 && get(dir / "Tortuga") == "precious");
        fs::remove(dir / "Tortuga.storm-tmp");
        // If the new file did land before the death, it wins and the old version is dropped.
        {
            Trash dying;
            REQUIRE(dying.discard(dir / "Tortuga", t0));
            put(dir / "Tortuga", "newer");
        }
        Trash later;
        REQUIRE(later.recover(dir) == 0 && all(dir) == Names{"Tortuga"} && get(dir / "Tortuga") == "newer");
    }
    {   // Plain delete by the player: final after the grace period, not before.
        Trash trash;
        REQUIRE(trash.discard(dir / "Tortuga", t0));
        trash.tick(t0 + 9s);
        REQUIRE(all(dir) == Names{"Tortuga.storm-deleted"} && trash.pendingCount() == 1);
        REQUIRE(trash.recover(dir, t0 + 9s) == 0 && listed(dir).empty());
        trash.tick(t0 + 10s);
        REQUIRE(all(dir).empty() && trash.pendingCount() == 0);
    }
    {   // The write after the delete fails: the deleted saves come back.
        Trash trash;
        put(dir / "A", "a");
        put(dir / "B", "b");
        REQUIRE(trash.discard(dir / "A", t0) && trash.discard(dir / "B", t0));
        trash.rollback();
        REQUIRE(all(dir) == (Names{"A", "B"}) && get(dir / "A") == "a" && trash.pendingCount() == 0);
        // Delete, recreate the name, delete again inside the window: the latest version is the one kept.
        REQUIRE(trash.discard(dir / "A", t0));
        put(dir / "A", "a2");
        REQUIRE(trash.discard(dir / "A", t0 + 1s) && trash.pendingCount() == 1);
        trash.rollback();
        REQUIRE(get(dir / "A") == "a2");
    }
    {   // Not a file / missing: nothing happens. Reserved names are recognised exactly.
        Trash trash;
        REQUIRE(!trash.discard(dir / "missing", t0) && !trash.discard(dir, t0) && trash.pendingCount() == 0);
        REQUIRE(isReservedName("x.storm-deleted") && isReservedName("x.storm-bak") && isReservedName("x.storm-tmp"));
        REQUIRE(!isReservedName("x") && !isReservedName(".storm-deleted") && !isReservedName("x.storm-deleted2"));
        REQUIRE(trash.recover(dir / "no such folder") == 0);
    }
    std::cout << "save trash: overwrite/crash/plain delete/failed write PASS\n";
}
