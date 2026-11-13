#pragma once

// Script patch layer: small, reviewed fixes to the PLAYER's own game scripts.
//
// The application never ships game files. When a game's scripts may be
// redistributed in modified form (each fix states its licence and source), the
// APK can carry unified diffs against one exact version of a script, and the
// engine applies them to the player's copy at start-up.
//
// Layout of a layer directory (Android: <filesDir>/script-patches/current):
//
//   sets/<set>/index.txt              one patch set for one game version
//                                     (no sets/: the layer has no patches)
//   sets/<set>/<patch files>          unified diffs named by index.txt
//
// index.txt is UTF-8, LF line ends, tab-separated records; the first record is
// the format line. Every set identifies the game it is for by the sha256 of one
// or more of the game's files; a set applies only when all of them match.
//
//   storm-script-patches  1
//   set       <set id>  <game>  <version>
//   identify  <game file>  <sha256>
//   target    <game file>  <base sha256>  <result sha256>  <patch file>
//   fix       <fix id>  <target>[,<target>...]  <licence>  <source>  <title>
//
// Decisions (tools/script_patches/README.md has the full description):
// - Patches are applied into derived files, not in memory. The compiler and the
//   rest of the engine open scripts by path through the resource index, so a
//   derived file under <cache>/<sha256(base sha256 ":" patch sha256)>/ is mapped
//   in place of the player's file. The file is only written when missing or
//   damaged: its mtime stays stable, so the compiled-script cache (keyed by the
//   program tree and this layer) is not invalidated on every start.
// - Hunks apply at their exact line numbers with exact context. The base sha256
//   is checked first and the result sha256 after, so there is no fuzz and no
//   "close enough".
// - A fix is all-or-nothing: if any of its targets is missing or differs from
//   the expected base, none of its targets is patched, and neither are the other
//   fixes sharing those targets. The player's files are then used unchanged and
//   the report lists the unapplied fixes (the engine shows a one-time notice).
// - A damaged layer (bad index or patch, a patch that does not apply to its own
//   verified base, a result with the wrong sha256, two sets matching one game)
//   is an application bug: LayerError, the game does not start.
// - When no set identifies the game, nothing is patched; that is reported, not
//   an error: patch sets exist only for the game versions they were made for.

#include "platform/resource_overlay.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace storm::script_patches
{
namespace fs = std::filesystem;

// The layer itself is broken: never load it partially.
struct LayerError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------- sha256 ----

inline std::string sha256(std::string_view data)
{
    static constexpr std::array<uint32_t, 64> k{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    std::string message(data);
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64 != 56)
        message.push_back('\0');
    for (int i = 7; i >= 0; --i)
        message.push_back(static_cast<char>((bits >> (i * 8)) & 0xff));
    for (size_t chunk = 0; chunk < message.size(); chunk += 64)
    {
        std::array<uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i)
            for (int b = 0; b < 4; ++b)
                w[i] = (w[i] << 8) | static_cast<unsigned char>(message[chunk + i * 4 + b]);
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto v = h;
        for (int i = 0; i < 64; ++i)
        {
            const uint32_t t1 = v[7] + (rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25)) +
                                ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
            const uint32_t t2 = (rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22)) +
                                ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (int i = 0; i < 8; ++i)
            h[i] += v[i];
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (const uint32_t word : h)
        for (int shift = 28; shift >= 0; shift -= 4)
            out.push_back(hex[(word >> shift) & 0xf]);
    return out;
}

// ------------------------------------------------------------ whitelist ----

// Game scripts a patch may target. program/interface/ shares its directory with the UI
// overlay (whole files the application replaces); a file is never both overlaid and patched:
// applyLayer refuses a target the overlay replaces.
inline bool isPatchTargetPath(const fs::path &relative)
{
    if (relative.empty() || relative.is_absolute())
        return false;
    for (const auto &part : relative)
        if (part == ".." || part == ".")
            return false;
    const auto key = android::resourceKey(relative);
    const auto extension = android::resourceKey(relative.extension());
    if (extension != ".c" && extension != ".h")
        return false;
    if (key == "program/dialog.c")
        return true;
    // Dialog texts are per language: program/dialogs/<language>/..., each
    // language file is a target of its own.
    constexpr std::string_view dialogs = "program/dialogs/";
    if (key.starts_with(dialogs))
    {
        const auto slash = key.find('/', dialogs.size());
        return slash != std::string::npos && slash > dialogs.size();
    }
    for (const std::string_view directory : {"program/quests/", "program/sea_ai/", "program/loc_ai/",
                                             "program/weather/", "program/battle_interface/",
                                             "program/locations/", "program/interface/", "program/characters/"})
        if (key.starts_with(directory))
            return true;
    return false;
}

// ---------------------------------------------------------------- index ----

struct Target
{
    std::string path;   // as written in the index (game-relative, '/')
    std::string key;    // resourceKey(path)
    std::string base;   // sha256 of the player's original
    std::string result; // sha256 after the patch
    fs::path patch;     // absolute path of the patch file
};

struct Fix
{
    std::string id;
    std::vector<std::string> targets; // keys
    std::string license;
    std::string source;
    std::string title;
};

struct Set
{
    std::string id, game, version;
    std::vector<std::pair<std::string, std::string>> identify; // key, sha256
    std::vector<Target> targets;
    std::vector<Fix> fixes;
};

namespace detail
{
inline std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true)
    {
        const auto end = text.find(separator, start);
        parts.emplace_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            return parts;
        start = end + 1;
    }
}

inline bool isSha256(std::string_view text)
{
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

inline bool isId(std::string_view text)
{
    return !text.empty() && text.size() <= 64 && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
           });
}

inline bool readFile(const fs::path &path, std::string &out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad())
        return false;
    out = std::move(buffer).str();
    return true;
}

// A game-relative path: '/' separated, no '\', no '.'/'..', not absolute.
inline bool isRelativePath(std::string_view text)
{
    if (text.empty() || text.find('\\') != std::string_view::npos || text.front() == '/')
        return false;
    for (const auto &part : split(text, '/'))
        if (part.empty() || part == "." || part == "..")
            return false;
    return true;
}
} // namespace detail

inline Set parseIndex(std::string_view text, const fs::path &setDirectory)
{
    const auto where = [&](size_t line) { return setDirectory.generic_string() + "/index.txt:" + std::to_string(line); };
    if (text.find('\r') != std::string_view::npos)
        throw LayerError(where(1) + ": CR in index (must be LF only)");
    auto lines = detail::split(text, '\n');
    if (!lines.empty() && lines.back().empty())
        lines.pop_back();
    if (lines.empty() || lines[0] != "storm-script-patches\t1")
        throw LayerError(where(1) + ": not a format 1 script patch index");
    Set set;
    std::set<std::string> fixIds, identified;
    std::map<std::string, size_t> targetIndex;
    for (size_t n = 1; n < lines.size(); ++n)
    {
        const auto fields = detail::split(lines[n], '\t');
        const auto &kind = fields[0];
        const auto fail = [&](const std::string &why) { throw LayerError(where(n + 1) + ": " + why); };
        if (kind == "set")
        {
            if (fields.size() != 4 || !detail::isId(fields[1]) || fields[2].empty() || fields[3].empty())
                fail("set needs <id> <game> <version>");
            if (!set.id.empty())
                fail("second set record");
            if (fields[1] != setDirectory.filename().string())
                fail("set id '" + fields[1] + "' differs from its directory");
            set.id = fields[1];
            set.game = fields[2];
            set.version = fields[3];
        }
        else if (kind == "identify")
        {
            if (fields.size() != 3 || !detail::isRelativePath(fields[1]) || !detail::isSha256(fields[2]))
                fail("identify needs <game file> <sha256>");
            const auto key = android::resourceKey(fields[1]);
            if (!identified.insert(key).second)
                fail("duplicate identify " + fields[1]);
            set.identify.emplace_back(key, fields[2]);
        }
        else if (kind == "target")
        {
            if (fields.size() != 5 || !detail::isSha256(fields[2]) || !detail::isSha256(fields[3]) ||
                !detail::isRelativePath(fields[4]))
                fail("target needs <game file> <base sha256> <result sha256> <patch file>");
            if (!detail::isRelativePath(fields[1]) || !isPatchTargetPath(fields[1]))
                fail("'" + fields[1] + "' is not an allowed patch target");
            if (fields[2] == fields[3])
                fail("target " + fields[1] + " does not change the file");
            Target target{fields[1], android::resourceKey(fields[1]), fields[2], fields[3], setDirectory / fields[4]};
            if (!targetIndex.emplace(target.key, set.targets.size()).second)
                fail("duplicate target " + fields[1]);
            set.targets.push_back(std::move(target));
        }
        else if (kind == "fix")
        {
            if (fields.size() != 6 || !detail::isId(fields[1]) || fields[3].empty() || fields[4].empty() ||
                fields[5].empty())
                fail("fix needs <id> <targets> <licence> <source> <title>");
            if (!fixIds.insert(fields[1]).second)
                fail("duplicate fix " + fields[1]);
            Fix fix{fields[1], {}, fields[3], fields[4], fields[5]};
            for (const auto &path : detail::split(fields[2], ','))
            {
                const auto key = android::resourceKey(path);
                if (!targetIndex.contains(key))
                    fail("fix " + fix.id + " names unknown target '" + path + "' (targets come first)");
                if (std::find(fix.targets.begin(), fix.targets.end(), key) != fix.targets.end())
                    fail("fix " + fix.id + " names " + path + " twice");
                fix.targets.push_back(key);
            }
            set.fixes.push_back(std::move(fix));
        }
        else
            fail("unknown record '" + kind + "'");
    }
    if (set.id.empty())
        throw LayerError(where(1) + ": no set record");
    if (set.identify.empty())
        throw LayerError(where(1) + ": set " + set.id + " does not identify its game");
    for (const auto &target : set.targets)
        if (std::none_of(set.fixes.begin(), set.fixes.end(), [&](const Fix &fix) {
                return std::find(fix.targets.begin(), fix.targets.end(), target.key) != fix.targets.end();
            }))
            throw LayerError(where(1) + ": target " + target.path + " belongs to no fix");
    return set;
}

// ---------------------------------------------------------------- apply ----

// Applies a unified diff with exact line numbers and exact context. Lines keep
// their terminators, so CRLF files patch byte for byte. Throws LayerError when
// the patch is malformed or does not fit `base`.
inline std::string applyUnifiedDiff(std::string_view base, std::string_view patch, std::string_view target)
{
    const auto fail = [&](const std::string &why) { throw LayerError("patch for " + std::string(target) + ": " + why); };
    std::vector<std::string_view> lines; // base lines with terminators
    for (size_t start = 0; start < base.size();)
    {
        const auto end = base.find('\n', start);
        const auto next = end == std::string_view::npos ? base.size() : end + 1;
        lines.push_back(base.substr(start, next - start));
        start = next;
    }
    auto patchLines = detail::split(patch, '\n');
    if (!patchLines.empty() && patchLines.back().empty())
        patchLines.pop_back();
    else
        fail("does not end with a line feed");
    const auto expectedHeader = [&](std::string_view prefix) {
        return std::string(prefix) + std::string(target);
    };
    if (patchLines.size() < 3 || android::resourceKey(patchLines[0]) != android::resourceKey(expectedHeader("--- a/")) ||
        android::resourceKey(patchLines[1]) != android::resourceKey(expectedHeader("+++ b/")))
        fail("must start with '--- a/<target>' and '+++ b/<target>'");

    std::string out;
    size_t cursor = 0; // next base line to copy
    size_t n = 2;
    while (n < patchLines.size())
    {
        const auto &header = patchLines[n];
        long oldStart = 0, oldCount = 1, newStart = 0, newCount = 1;
        {
            // @@ -a[,b] +c[,d] @@[ text]
            if (!header.starts_with("@@ -"))
                fail("expected a hunk header at patch line " + std::to_string(n + 1));
            const auto close = header.find(" @@", 4);
            if (close == std::string::npos)
                fail("bad hunk header '" + header + "'");
            const auto ranges = detail::split(std::string_view(header).substr(4, close - 4), ' ');
            if (ranges.size() != 2 || !ranges[1].starts_with("+"))
                fail("bad hunk header '" + header + "'");
            const auto range = [&](std::string_view text, long &start, long &count) {
                const auto parts = detail::split(text, ',');
                if (parts.size() > 2)
                    fail("bad hunk range '" + std::string(text) + "'");
                try
                {
                    size_t used = 0;
                    start = std::stol(parts[0], &used);
                    if (used != parts[0].size() || start < 0)
                        fail("bad hunk range '" + std::string(text) + "'");
                    if (parts.size() == 2)
                    {
                        count = std::stol(parts[1], &used);
                        if (used != parts[1].size() || count < 0)
                            fail("bad hunk range '" + std::string(text) + "'");
                    }
                }
                catch (const std::logic_error &)
                {
                    fail("bad hunk range '" + std::string(text) + "'");
                }
            };
            range(ranges[0], oldStart, oldCount);
            range(std::string_view(ranges[1]).substr(1), newStart, newCount);
        }
        ++n;
        // With an empty old range the start names the line *after which* to insert.
        const size_t position = oldCount == 0 ? static_cast<size_t>(oldStart) : static_cast<size_t>(oldStart - 1);
        if ((oldCount > 0 && oldStart == 0) || position < cursor || position > lines.size())
            fail("hunk '" + header + "' is out of order or outside the file");
        for (; cursor < position; ++cursor)
            out.append(lines[cursor]);
        long seenOld = 0, seenNew = 0;
        while (n < patchLines.size() && !patchLines[n].starts_with("@@"))
        {
            const auto &line = patchLines[n];
            if (line.empty())
                fail("empty line in hunk '" + header + "' (context lines start with a space)");
            const char op = line[0];
            if (op == '\\')
                fail("misplaced '\\ No newline at end of file'");
            std::string content = line.substr(1) + "\n";
            if (n + 1 < patchLines.size() && patchLines[n + 1].starts_with("\\ "))
            {
                content.pop_back();
                ++n;
            }
            if (op == ' ' || op == '-')
            {
                if (cursor >= lines.size() || lines[cursor] != content)
                    fail("hunk '" + header + "' does not match the base at line " + std::to_string(cursor + 1));
                if (op == ' ')
                    out.append(content);
                ++cursor;
                ++seenOld;
                seenNew += op == ' ';
            }
            else if (op == '+')
            {
                out.append(content);
                ++seenNew;
            }
            else
                fail("bad line in hunk '" + header + "'");
            ++n;
        }
        if (seenOld != oldCount || seenNew != newCount)
            fail("hunk '" + header + "' line counts do not match its header");
    }
    for (; cursor < lines.size(); ++cursor)
        out.append(lines[cursor]);
    return out;
}

// --------------------------------------------------------------- report ----

struct Unapplied
{
    std::string set, fix, title;
    std::vector<std::string> reasons; // why, per target
};

struct Report
{
    bool layerPresent = false;
    std::string set;                   // selected set id, empty if none matched
    std::string game, version;         // of the selected set
    std::vector<std::string> applied;  // fix ids
    std::vector<Unapplied> unapplied;  // fixes left out (game files differ)
    std::vector<std::string> info;     // log lines
    std::vector<std::string> errors;   // log lines (non-fatal)

    // Stable for the same unapplied fixes against the same player files: the
    // one-time notice is shown again only when this changes.
    std::string noticeKey() const
    {
        if (unapplied.empty())
            return {};
        std::string text;
        for (const auto &fix : unapplied)
        {
            text += fix.set + "/" + fix.fix + "\n";
            for (const auto &reason : fix.reasons)
                text += reason + "\n";
        }
        return sha256(text);
    }
};

// Process-wide result of the last applyLayer() made by the file service, for
// the start-up log and notice (the scan runs before logging is ready).
inline Report &lastReport()
{
    static Report report;
    return report;
}

// ---------------------------------------------------------- application ----

namespace detail
{
// Writes `data` to `path` through a temporary file in the same directory.
inline bool writeAtomically(const fs::path &path, std::string_view data, std::string &error)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "cannot create " + path.parent_path().string() + ": " + ec.message();
        return false;
    }
    const auto temporary = fs::path(path).concat(".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file)
        {
            fs::remove(temporary, ec);
            error = "cannot write " + temporary.string();
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    if (ec)
    {
        fs::remove(temporary, ec);
        error = "cannot move " + temporary.string() + " into place";
        return false;
    }
    return true;
}
} // namespace detail

inline std::vector<Set> loadLayer(const fs::path &layer)
{
    std::vector<Set> sets;
    const auto root = layer / "sets";
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return sets; // a layer without patch sets (nothing to patch for any game yet)
    std::vector<fs::path> directories;
    for (const auto &entry : fs::directory_iterator(root))
    {
        if (entry.is_symlink() || !entry.is_directory())
            throw LayerError(entry.path().string() + ": unexpected entry in sets/");
        directories.push_back(entry.path());
    }
    std::sort(directories.begin(), directories.end());
    for (const auto &directory : directories)
    {
        std::string text;
        if (!detail::readFile(directory / "index.txt", text))
            throw LayerError(directory.string() + ": cannot read index.txt");
        auto set = parseIndex(text, directory);
        for (const auto &target : set.targets)
            if (fs::is_symlink(target.patch, ec) || !fs::is_regular_file(target.patch, ec))
                throw LayerError("patch " + target.patch.string() + " of " + target.path + " is missing");
        sets.push_back(std::move(set));
    }
    return sets;
}

// `paths` is the resource index (lowercase game-relative key -> real path);
// `overlaid` the keys of it the UI overlay replaces. Selects the set that
// identifies the player's game, patches what it can into `derivedRoot` and
// redirects `paths` there. Returns what happened; throws LayerError when the
// layer is broken, a target the UI overlay replaces included. A missing layer
// directory means "no patches" (desktop builds, tests).
inline Report applyLayer(const fs::path &layer, const fs::path &derivedRoot,
                         std::unordered_map<std::string, std::string> &paths,
                         const std::set<std::string> &overlaid = {})
{
    Report report;
    std::error_code ec;
    if (!fs::is_directory(layer, ec))
    {
        report.info.push_back("no script patch layer at " + layer.string());
        return report;
    }
    report.layerPresent = true;
    const auto sets = loadLayer(layer);

    std::map<std::string, std::string> hashes; // key -> sha256 of the player's file ("" if missing)
    const auto playerHash = [&](const std::string &key) -> const std::string & {
        auto found = hashes.find(key);
        if (found != hashes.end())
            return found->second;
        std::string hash;
        std::string data;
        if (const auto path = paths.find(key); path != paths.end() && detail::readFile(path->second, data))
            hash = sha256(data);
        return hashes.emplace(key, std::move(hash)).first->second;
    };

    const Set *selected = nullptr;
    for (const auto &set : sets)
    {
        const bool matches = std::all_of(set.identify.begin(), set.identify.end(), [&](const auto &probe) {
            return playerHash(probe.first) == probe.second;
        });
        if (!matches)
            continue;
        if (selected)
            throw LayerError("patch sets " + selected->id + " and " + set.id + " both identify this game");
        selected = &set;
    }
    // Derived files of other bases, patch versions or games are dropped.
    std::set<fs::path> keep;
    const auto dropStale = [&] {
        if (fs::is_directory(derivedRoot, ec))
            for (const auto &entry : fs::directory_iterator(derivedRoot, ec))
                if (!keep.contains(entry.path().filename()))
                {
                    fs::remove_all(entry.path(), ec);
                    if (ec)
                        report.errors.push_back("cannot remove stale " + entry.path().string() + ": " + ec.message());
                }
    };
    if (!selected)
    {
        dropStale();
        report.info.push_back("no script patch set identifies this game (" + std::to_string(sets.size()) +
                              " set(s) in the layer); scripts are used unchanged");
        return report;
    }
    const Set &set = *selected;
    for (const auto &target : set.targets)
        if (overlaid.contains(target.key))
            throw LayerError("set " + set.id + ": " + target.path +
                             " is replaced by the UI overlay; a file is never both overlaid and patched");
    report.set = set.id;
    report.game = set.game;
    report.version = set.version;

    // 1. Each target: check the base, apply, verify, make sure the derived file exists.
    std::map<std::string, std::string> problems; // key -> reason (target cannot be patched)
    std::map<std::string, fs::path> derived;     // key -> derived file
    for (const auto &target : set.targets)
    {
        const auto &actual = playerHash(target.key);
        const auto original = paths.find(target.key);
        if (original == paths.end())
        {
            problems[target.key] = target.path + " is missing from the game";
            continue;
        }
        if (actual.empty())
        {
            problems[target.key] = target.path + " cannot be read";
            continue;
        }
        if (actual != target.base)
        {
            problems[target.key] = target.path + " differs from the expected version (sha256 " + actual +
                                   ", expected " + target.base + ")";
            continue;
        }
        std::string patch;
        if (!detail::readFile(target.patch, patch))
            throw LayerError("cannot read patch " + target.patch.string());
        std::string base;
        if (!detail::readFile(original->second, base) || sha256(base) != target.base)
        {
            problems[target.key] = target.path + " changed while it was being read";
            continue;
        }
        const auto patched = applyUnifiedDiff(base, patch, target.path);
        if (sha256(patched) != target.result)
            throw LayerError("patch for " + target.path + " gives sha256 " + sha256(patched) + ", expected " +
                             target.result);
        const auto file = derivedRoot / sha256(target.base + ":" + sha256(patch)) / fs::path(std::u8string(target.path.begin(), target.path.end()));
        std::string existing;
        if (!detail::readFile(file, existing) || sha256(existing) != target.result)
        {
            std::string error;
            if (!detail::writeAtomically(file, patched, error))
            {
                problems[target.key] = target.path + ": " + error;
                continue;
            }
        }
        derived[target.key] = file;
    }

    // 2. All-or-nothing per fix, and fixes sharing a withheld target are withheld too.
    std::set<std::string> withheld;
    for (const auto &[key, reason] : problems)
        withheld.insert(key);
    std::set<std::string> unappliedFixes;
    for (bool changed = true; changed;)
    {
        changed = false;
        for (const auto &fix : set.fixes)
        {
            if (unappliedFixes.contains(fix.id))
                continue;
            if (std::none_of(fix.targets.begin(), fix.targets.end(), [&](const auto &key) { return withheld.contains(key); }))
                continue;
            unappliedFixes.insert(fix.id);
            withheld.insert(fix.targets.begin(), fix.targets.end());
            changed = true;
        }
    }
    for (const auto &fix : set.fixes)
    {
        if (!unappliedFixes.contains(fix.id))
        {
            report.applied.push_back(fix.id);
            continue;
        }
        Unapplied entry{set.id, fix.id, fix.title, {}};
        for (const auto &key : fix.targets)
            if (const auto problem = problems.find(key); problem != problems.end())
                entry.reasons.push_back(problem->second);
        if (entry.reasons.empty())
            entry.reasons.push_back("shares a script with a fix that cannot be applied");
        report.errors.push_back("script fix " + set.id + "/" + fix.id + " NOT applied: " + entry.reasons.front());
        report.unapplied.push_back(std::move(entry));
    }

    // 3. Redirect the applied targets (every key spelling of the original file).
    for (const auto &target : set.targets)
    {
        const auto file = derived.find(target.key);
        if (file == derived.end())
            continue;
        keep.insert(file->second.lexically_relative(derivedRoot).begin()->string());
        if (withheld.contains(target.key))
            continue;
        const auto original = paths.at(target.key);
        for (auto &[key, value] : paths)
            if (value == original)
                value = file->second.string();
        report.info.push_back("script patch " + set.id + ": " + target.path + " -> " + file->second.string());
    }

    dropStale();
    report.info.push_back("script patch set " + set.id + " (" + set.game + " " + set.version + "): " +
                          std::to_string(report.applied.size()) + " fix(es) applied, " +
                          std::to_string(report.unapplied.size()) + " not applied");
    return report;
}

// Text of the one-time notice. `russian` selects the language.
inline std::string noticeText(const Report &report, bool russian)
{
    std::string text = russian
        ? "Некоторые исправления скриптов не применены: ваши файлы игры отличаются от версии, для которой они сделаны (" +
              report.game + " " + report.version + "). Игра использует ваши файлы без изменений.\n"
        : "Some script fixes were not applied because your game files differ from the version they were made for (" +
              report.game + " " + report.version + "). The game uses your files unchanged.\n";
    for (const auto &fix : report.unapplied)
        text += "\n- " + fix.title + " [" + fix.fix + "]";
    text += russian ? "\n\nПодробности в журнале движка." : "\n\nDetails are in the engine log.";
    return text;
}
} // namespace storm::script_patches
