#include "script_cache_files.hpp"

#include <cassert>
#include <iostream>

namespace fs = std::filesystem;
using namespace storm::script_cache;

static void put(const fs::path &path, const std::string &bytes)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}

static std::string slurp(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

int main(int, char **argv)
{
    const fs::path work = argv[1];
    const fs::path cache = work / "Cache";
    uint64_t key = 0;

    // State: first start, damaged state file, reset stamps the key and empties the folder.
    assert(readState(cache, key) == StateFile::Missing);
    fs::create_directories(cache / "PROGRAM");
    put(cache / kStateFile, "abc");
    put(cache / "PROGRAM" / "seadogs.b", "old");
    assert(readState(cache, key) == StateFile::Damaged);
    assert(resetFolder(cache, 0x1122334455667788ull).empty());
    assert(readState(cache, key) == StateFile::Read && key == 0x1122334455667788ull);
    assert(!fs::exists(cache / "PROGRAM"));

    // A changed key (scripts edited, other engine build) is a plain reset too.
    put(cache / "stale.b", "x");
    assert(resetFolder(cache, 42).empty());
    assert(readState(cache, key) == StateFile::Read && key == 42 && !fs::exists(cache / "stale.b"));

    // Reset that cannot create the folder reports why.
    put(work / "file", "");
    assert(!resetFolder(work / "file" / "Cache", 1).empty());

    // Entries: round trip, ordinary miss, and every kind of damage.
    const auto entry = cache / "PROGRAM\\interface\\save_load.b"; // segment names keep backslashes on Android
    std::vector<char> payload;
    assert(readEntry(entry, payload) == Entry::Missing);
    const std::string body = std::string("bytecode\0with\0zeros", 19);
    assert(writeEntry(entry, body).empty());
    assert(!fs::exists(entry.string() + ".tmp"));
    assert(readEntry(entry, payload) == Entry::Read && std::string(payload.begin(), payload.end()) == body);

    const auto good = slurp(entry);
    auto flipped = good;
    flipped.back() ^= 1;
    put(entry, flipped);
    assert(readEntry(entry, payload) == Entry::Damaged);
    put(entry, good.substr(0, good.size() - 3)); // torn write
    assert(readEntry(entry, payload) == Entry::Damaged);
    put(entry, good.substr(0, sizeof(EntryHeader))); // header only
    assert(readEntry(entry, payload) == Entry::Damaged);
    put(entry, ""); // empty file
    assert(readEntry(entry, payload) == Entry::Damaged);
    auto magic = good;
    magic[7] = '1'; // previous layout
    put(entry, magic);
    assert(readEntry(entry, payload) == Entry::Damaged);

    // Rewrite over a damaged entry recovers it.
    assert(writeEntry(entry, body).empty());
    assert(readEntry(entry, payload) == Entry::Read);

    // Write into an unwritable place reports why and leaves nothing behind.
    assert(!writeEntry(work / "file" / "x.b", body).empty());

    std::cout << "script cache files: state missing/damaged/reset, entry round trip, torn/flipped/empty/old-magic "
                 "entries detected PASS\n";
}
