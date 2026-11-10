// Host checks for src/libs/core/src/save_file.hpp: format compatibility with
// saves written by the original engine, corruption handling, and the
// invariant that an interrupted write never leaves the save name unloadable.
#include "save_file.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

namespace fs = std::filesystem;
using namespace storm::save;

#define REQUIRE(condition)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << ": failed: " #condition << std::endl;                          \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

static Bytes pseudoState(size_t size, unsigned seed)
{
    // Mixed entropy, roughly like serialized script variables.
    Bytes data(size);
    std::mt19937 rng(seed);
    static const char words[] = "Characters.id\0ship.cannons\0quest.main\0location\0items.blade1\0";
    for (size_t i = 0; i < size; ++i)
        data[i] = (rng() % 4 == 0) ? static_cast<char>(rng()) : words[(i + rng() % 3) % (sizeof(words) - 1)];
    return data;
}

static void put(const fs::path &path, const Bytes &data)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.close();
    REQUIRE(!file.fail());
}

static Bytes get(const fs::path &path)
{
    Bytes data;
    std::string error;
    REQUIRE(readFile(path, data, error));
    return data;
}

// Byte-for-byte what the original COMPILER::SaveState + SetSaveData produced.
static Bytes legacyImage(const Bytes &state, const Bytes &ext)
{
    Bytes image;
    Header header{};
    std::snprintf(header.sFileInfo, sizeof(header.sFileInfo), "legacy");
    append(image, &header, sizeof(header));
    const uint32_t rawSize = static_cast<uint32_t>(state.size());
    Bytes packed(state.size() * 2);
    uLongf packedLen = packed.size();
    REQUIRE(compress2(reinterpret_cast<Bytef *>(packed.data()), &packedLen,
                      reinterpret_cast<const Bytef *>(state.data()), rawSize, Z_BEST_COMPRESSION) == Z_OK);
    const uint32_t packed32 = static_cast<uint32_t>(packedLen);
    append(image, &rawSize, sizeof(rawSize));
    append(image, &packed32, sizeof(packed32));
    append(image, packed.data(), packedLen);
    if (!ext.empty())
    {
        header.dwExtDataOffset = static_cast<uint32_t>(image.size());
        header.dwExtDataSize = static_cast<uint32_t>(ext.size());
        Bytes extPacked(ext.size() * 2 + 64);
        uLongf extLen = extPacked.size();
        REQUIRE(compress2(reinterpret_cast<Bytef *>(extPacked.data()), &extLen,
                          reinterpret_cast<const Bytef *>(ext.data()), ext.size(), Z_BEST_COMPRESSION) == Z_OK);
        const uint32_t ext32 = static_cast<uint32_t>(extLen);
        append(image, &ext32, sizeof(ext32));
        append(image, extPacked.data(), extLen);
        std::memcpy(image.data(), &header, sizeof(header));
    }
    return image;
}

static bool validState(const Bytes &image, std::string &error)
{
    Bytes raw;
    return readState(image, raw, error);
}

static void formatChecks()
{
    const auto state = pseudoState(300000, 1);
    const auto ext = pseudoState(70000, 2);
    std::string error;
    Bytes raw, data;

    // Old files load.
    const auto legacy = legacyImage(state, ext);
    REQUIRE(readState(legacy, raw, error) && raw == state);
    REQUIRE(readExtData(legacy, data, error) && data == ext);
    const auto legacyPlain = legacyImage(state, {});
    REQUIRE(readState(legacyPlain, raw, error) && raw == state);
    REQUIRE(readExtData(legacyPlain, data, error) && data.empty());

    // New files have the legacy layout: header, raw size, packed size, stream.
    Bytes image;
    REQUIRE(buildState("slot", state.data(), static_cast<uint32_t>(state.size()), image, error));
    Header header;
    std::memcpy(&header, image.data(), sizeof(header));
    REQUIRE(std::string(header.sFileInfo) == "slot" && header.dwExtDataOffset == 0 && header.dwExtDataSize == 0);
    uint32_t rawSize, packedSize;
    std::memcpy(&rawSize, image.data() + 40, 4);
    std::memcpy(&packedSize, image.data() + 44, 4);
    REQUIRE(rawSize == state.size() && packedSize == image.size() - 48);
    // ...and the original loader's exact call accepts them.
    Bytes legacyOut(rawSize);
    uLongf legacyLen = rawSize;
    REQUIRE(uncompress(reinterpret_cast<Bytef *>(legacyOut.data()), &legacyLen,
                       reinterpret_cast<const Bytef *>(image.data() + 48), packedSize) == Z_OK);
    REQUIRE(legacyLen == rawSize && legacyOut == state);

    const auto stateOnlySize = image.size();
    REQUIRE(appendExtData(image, "slot2", ext.data(), static_cast<uint32_t>(ext.size()), error));
    std::memcpy(&header, image.data(), sizeof(header));
    REQUIRE(header.dwExtDataOffset == stateOnlySize && header.dwExtDataSize == ext.size());
    REQUIRE(std::string(header.sFileInfo) == "slot2");
    REQUIRE(readState(image, raw, error) && raw == state);
    REQUIRE(readExtData(image, data, error) && data == ext);

    // Long info strings are truncated, never overflow the 32 byte field.
    REQUIRE(buildState("0123456789012345678901234567890123456789", state.data(), 100, image, error));
    REQUIRE(image[31] == 0 && std::strlen(image.data()) == 31);
    REQUIRE(!buildState("x", state.data(), 0, image, error));
    REQUIRE(!buildState("x", nullptr, 10, image, error));
}

static void corruptionChecks()
{
    const auto state = pseudoState(200000, 3);
    const auto ext = pseudoState(5000, 4);
    std::string error;
    Bytes good, raw, data;
    REQUIRE(buildState("s", state.data(), static_cast<uint32_t>(state.size()), good, error));
    REQUIRE(appendExtData(good, "s", ext.data(), static_cast<uint32_t>(ext.size()), error));
    uint32_t packedSize;
    std::memcpy(&packedSize, good.data() + 44, 4);

    REQUIRE(!readState({}, raw, error));
    REQUIRE(!readState(Bytes(47, 'x'), raw, error));
    // Every truncation point inside the state is rejected.
    for (size_t size : {size_t(48), size_t(49), size_t(48 + packedSize / 2), size_t(48 + packedSize - 1)})
    {
        Bytes cut(good.begin(), good.begin() + size);
        REQUIRE(!readState(cut, raw, error) && raw.empty() && !error.empty());
    }
    // Bit flips anywhere in the stream are caught by zlib (structure or Adler-32).
    std::mt19937 rng(5);
    for (int i = 0; i < 200; ++i)
    {
        auto damaged = good;
        damaged[48 + rng() % packedSize] ^= static_cast<char>(1 << (rng() % 8));
        REQUIRE(!readState(damaged, raw, error));
    }
    // Hostile size fields: no huge allocation, no out-of-bounds read.
    for (uint32_t bogus : {0u, 1u, 0x8000001u, 0xffffffffu})
    {
        auto damaged = good;
        std::memcpy(damaged.data() + 40, &bogus, 4);
        REQUIRE(!readState(damaged, raw, error));
        damaged = good;
        std::memcpy(damaged.data() + 44, &bogus, 4);
        REQUIRE(!readState(damaged, raw, error));
        damaged = good;
        std::memcpy(damaged.data() + 32, &bogus, 4); // ext offset
        if (bogus != 0x8000001u || damaged.size() < bogus)
            REQUIRE(!readExtData(damaged, data, error));
        damaged = good;
        std::memcpy(damaged.data() + 36, &bogus, 4); // ext size
        if (bogus != 0)
            REQUIRE(!readExtData(damaged, data, error));
    }
    // Damaged extended data does not make the state unloadable.
    auto damaged = good;
    damaged[damaged.size() - 3] ^= 0x40;
    REQUIRE(readState(damaged, raw, error) && raw == state);
    REQUIRE(!readExtData(damaged, data, error) && data.empty());
    Bytes cut(good.begin(), good.end() - 10);
    REQUIRE(readState(cut, raw, error) && !readExtData(cut, data, error));
}

static void noLeftovers(const fs::path &target)
{
    REQUIRE(!fs::exists(withSuffix(target, kTempSuffix)));
    REQUIRE(!fs::exists(withSuffix(target, kBackupSuffix)));
}

static void atomicChecks(const fs::path &dir)
{
    std::string error;
    const auto target = dir / "Player One" / "quick save";
    fs::create_directories(target.parent_path());
    const auto stateA = pseudoState(120000, 10), stateB = pseudoState(150000, 11);
    Bytes imageA, imageB, image, raw;
    REQUIRE(buildState("A", stateA.data(), static_cast<uint32_t>(stateA.size()), imageA, error));
    REQUIRE(buildState("B", stateB.data(), static_cast<uint32_t>(stateB.size()), imageB, error));

    // First write, then replacement.
    REQUIRE(writeAtomic(target, imageA, validState, error));
    REQUIRE(get(target) == imageA);
    noLeftovers(target);
    REQUIRE(writeAtomic(target, imageB, validState, error));
    REQUIRE(get(target) == imageB);
    noLeftovers(target);
    REQUIRE(loadWithRecovery(target, image, raw, error) == Source::Primary && raw == stateB);

    // A rejected image never reaches the target.
    Bytes garbage(1000, 'g');
    REQUIRE(!writeAtomic(target, garbage, validState, error) && !error.empty());
    REQUIRE(get(target) == imageB);
    noLeftovers(target);

    // Unwritable location: reported, nothing created.
    REQUIRE(!writeAtomic(dir / "missing-directory" / "save", imageA, validState, error));
    REQUIRE(!fs::exists(dir / "missing-directory"));

    // Process death after each step: the save name always loads, as the old
    // or the new version, and the next save cleans up.
    for (const char *step : {"temp-written", "backup-made", "renamed"})
    {
        put(target, imageA);
        fs::remove(withSuffix(target, kBackupSuffix));
        fs::remove(withSuffix(target, kTempSuffix));
        const std::string at = step;
        REQUIRE(!writeAtomic(target, imageB, validState, error, [&](const char *s) { return at == s; }));
        REQUIRE(loadWithRecovery(target, image, raw, error) == Source::Primary);
        REQUIRE(raw == (at == "renamed" ? stateB : stateA));
        if (at != "temp-written")
        {
            // The previous version is still on disk under the backup name.
            Bytes backupRaw;
            REQUIRE(readState(get(withSuffix(target, kBackupSuffix)), backupRaw, error) && backupRaw == stateA);
        }
        REQUIRE(writeAtomic(target, imageB, validState, error));
        REQUIRE(get(target) == imageB);
        noLeftovers(target);
    }

    // Torn primary (e.g. written by an older build that died mid-write) with a
    // backup next to it: the backup is promoted.
    put(target, Bytes(imageB.begin(), imageB.begin() + imageB.size() / 2));
    put(withSuffix(target, kBackupSuffix), imageA);
    REQUIRE(loadWithRecovery(target, image, raw, error) == Source::Backup && raw == stateA);
    REQUIRE(get(target) == imageA && !fs::exists(withSuffix(target, kBackupSuffix)));
    // Missing primary, intact backup.
    fs::rename(target, withSuffix(target, kBackupSuffix));
    REQUIRE(loadWithRecovery(target, image, raw, error) == Source::Backup && raw == stateA);
    REQUIRE(fs::exists(target));
    // Nothing usable: clean failure, no exception, outputs empty.
    put(target, Bytes(60, '\0'));
    put(withSuffix(target, kBackupSuffix), Bytes(10, 'x'));
    REQUIRE(loadWithRecovery(target, image, raw, error) == Source::None && raw.empty() && !error.empty());
    fs::remove(withSuffix(target, kBackupSuffix));
    REQUIRE(loadWithRecovery(dir / "absent", image, raw, error) == Source::None);

    REQUIRE(isAuxiliaryName("slot.storm-tmp") && isAuxiliaryName("a b.storm-bak"));
    REQUIRE(!isAuxiliaryName("slot") && !isAuxiliaryName("storm-bak") && !isAuxiliaryName("slot.storm-bak2"));
}

template <typename Operation> static double milliseconds(Operation operation, int repeat)
{
    double best = 1e30;
    for (int i = 0; i < repeat; ++i)
    {
        const auto begin = std::chrono::steady_clock::now();
        operation();
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
    }
    return best;
}

static void timing(const Bytes &state, const fs::path &dir, const char *label)
{
    std::string error;
    for (int level : {Z_BEST_COMPRESSION, kCompressionLevel})
    {
        Bytes image;
        const double ms = milliseconds(
            [&] { REQUIRE(buildState("t", state.data(), static_cast<uint32_t>(state.size()), image, error, level)); }, 3);
        Bytes raw;
        REQUIRE(readState(image, raw, error) && raw == state);
        std::cout << label << ": level " << level << " -> " << image.size() << " bytes, compress " << ms << " ms\n";
        if (level == kCompressionLevel)
        {
            const double write = milliseconds([&] { REQUIRE(writeAtomic(dir / "timing", image, validState, error)); }, 3);
            std::cout << label << ": atomic write (temp+fsync+verify+rename) " << write << " ms\n";
        }
    }
}

int main(int argc, char **argv)
{
    REQUIRE(argc >= 2);
    const fs::path dir = argv[1];
    formatChecks();
    corruptionChecks();
    atomicChecks(dir);
    timing(pseudoState(5 * 1024 * 1024, 42), dir, "synthetic 5 MiB state");
    if (argc >= 3)
    {
        // A real save written by an earlier build: must load, and re-saving its
        // state in the new container must reproduce the same state.
        std::string error;
        Bytes image, raw, ext, rebuilt, raw2, ext2;
        REQUIRE(readFile(argv[2], image, error));
        REQUIRE(readState(image, raw, error));
        REQUIRE(readExtData(image, ext, error));
        REQUIRE(buildState(image.data(), raw.data(), static_cast<uint32_t>(raw.size()), rebuilt, error));
        if (!ext.empty())
            REQUIRE(appendExtData(rebuilt, image.data(), ext.data(), static_cast<uint32_t>(ext.size()), error));
        REQUIRE(readState(rebuilt, raw2, error) && raw2 == raw);
        REQUIRE(readExtData(rebuilt, ext2, error) && ext2 == ext);
        std::cout << "legacy save: " << image.size() << " bytes, state " << raw.size() << ", ext " << ext.size()
                  << "; rebuilt " << rebuilt.size() << " bytes\n";
        timing(raw, dir, "legacy save state");
    }
    std::cout << "atomic save: format/corruption/crash-window/recovery PASS\n";
    return 0;
}
