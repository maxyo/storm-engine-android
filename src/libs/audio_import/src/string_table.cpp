// FMOD Studio string table (STDT chunk of *.strings.bank). The layout was
// derived from the bank data itself; no FMOD code was used. Format notes:
// README.md "String table".
//
//   u32 version (1)
//   struct array of trie records, 8 bytes each:
//     u24 pool offset of the edge label (0xffffff: empty label),
//     u8 first label char in lower case (lookups are case-insensitive),
//     u24 first child record (inner node) or string index (leaf), u8 child count
//   struct array of GUIDs (16 bytes), indexed by string index
//   u16 pool size, pool of NUL-terminated labels
//   u16 count, u24 record index per string index
//   u16 count, u24 parent record per record (0xffffff for the root)
#include "riff.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace storm::audio_import
{
namespace
{
constexpr uint32_t kNone = 0xffffff;

struct Record
{
    uint32_t label;
    uint8_t first;
    uint32_t child;
    uint8_t childCount;
};

struct Table
{
    std::vector<Record> records;
    ByteView pool;
    std::vector<std::string> paths;
    std::vector<uint32_t> leafRecord;

    std::string label(const Record &record, detail::Reader &reader) const
    {
        if (record.label == kNone)
            return {};
        if (record.label >= pool.size())
            reader.fail("label offset outside the string pool");
        const auto *begin = pool.data() + record.label;
        const auto *end = static_cast<const uint8_t *>(std::memchr(begin, 0, pool.size() - record.label));
        if (end == nullptr || end == begin || std::tolower(*begin) != record.first)
            reader.fail("malformed trie label");
        return std::string(reinterpret_cast<const char *>(begin), end - begin);
    }

    void walk(uint32_t index, const std::string &prefix, int depth, detail::Reader &reader)
    {
        if (depth > 256)
            reader.fail("string trie too deep");
        const Record &record = records.at(index);
        const std::string path = prefix + label(record, reader);
        if (record.childCount == 0)
        {
            if (record.child >= paths.size() || leafRecord[record.child] != kNone)
                reader.fail("string index " + std::to_string(record.child) + " invalid or repeated");
            paths[record.child] = path;
            leafRecord[record.child] = index;
            return;
        }
        if (record.child <= index || size_t{record.child} + record.childCount > records.size())
            reader.fail("trie children out of range");
        for (uint32_t child = record.child; child < record.child + record.childCount; ++child)
            walk(child, path, depth + 1, reader);
    }
};
} // namespace

std::string Guid::toString() const
{
    const auto &b = bytes;
    char text[40];
    std::snprintf(text, sizeof text, "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}", b[3],
                  b[2], b[1], b[0], b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return text;
}

std::vector<StringEntry> readStringTable(ByteView bank)
{
    const auto chunk = detail::findBankChunk(bank, "STDT");
    if (!chunk || chunk->empty())
        throw ImportError("FMOD bank has no string table (STDT); use the *.strings.bank");
    detail::Reader reader(*chunk, "STDT");
    if (const uint32_t version = reader.u32(); version != 1)
        reader.fail("unsupported string table version " + std::to_string(version));

    Table table;
    const uint32_t recordCount = detail::readStructArrayHeader(reader, 8);
    for (uint32_t i = 0; i < recordCount; ++i)
    {
        Record record;
        record.label = reader.u24();
        record.first = reader.u8();
        record.child = reader.u24();
        record.childCount = reader.u8();
        table.records.push_back(record);
    }
    const uint32_t guidCount = detail::readStructArrayHeader(reader, 16);
    std::vector<Guid> guids(guidCount);
    for (auto &guid : guids)
    {
        auto bytes = reader.take(16);
        std::copy(bytes.begin(), bytes.end(), guid.bytes.begin());
    }
    table.pool = reader.take(reader.u16());

    const uint32_t indexCount = reader.u16();
    if (indexCount != guidCount)
        reader.fail("string index count differs from GUID count");
    std::vector<uint32_t> stringRecord(indexCount);
    for (auto &record : stringRecord)
        record = reader.u24();
    if (reader.u16() != recordCount)
        reader.fail("parent table size differs from record count");
    std::vector<uint32_t> parents(recordCount);
    for (auto &parent : parents)
        parent = reader.u24();
    if (reader.remaining() != 0)
        reader.fail("trailing bytes");

    if (recordCount == 0 || table.records[0].label != kNone || parents[0] != kNone)
        reader.fail("missing trie root");
    table.paths.resize(guidCount);
    table.leafRecord.assign(guidCount, kNone);
    table.walk(0, "", 0, reader);
    if (table.leafRecord != stringRecord)
        reader.fail("trie leaves disagree with the string index table");
    for (uint32_t i = 1; i < recordCount; ++i)
    {
        const Record &parent = table.records.at(parents[i] == kNone ? 0 : parents[i]);
        if (parents[i] == kNone || i < parent.child || i >= parent.child + parent.childCount)
            reader.fail("parent table disagrees with the trie at record " + std::to_string(i));
    }

    std::vector<StringEntry> entries;
    for (uint32_t i = 0; i < guidCount; ++i)
    {
        if (table.leafRecord[i] == kNone)
            reader.fail("string index " + std::to_string(i) + " has no trie leaf");
        entries.push_back({table.paths[i], guids[i]});
    }
    return entries;
}

std::string stringTableJson(const std::vector<StringEntry> &entries)
{
    std::vector<const StringEntry *> sorted;
    for (const auto &entry : entries)
        sorted.push_back(&entry);
    std::sort(sorted.begin(), sorted.end(), [](auto *a, auto *b) { return a->path < b->path; });
    std::string json = "{\"strings\":[";
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        json += i ? ",\n" : "\n";
        json += "{\"path\":\"";
        for (unsigned char c : sorted[i]->path)
        {
            if (c == '"' || c == '\\')
            {
                json += '\\';
                json += static_cast<char>(c);
            }
            else if (c < 0x20)
            {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                json += escaped;
            }
            else
                json += static_cast<char>(c);
        }
        json += "\",\"guid\":\"" + sorted[i]->guid.toString() + "\"}";
    }
    json += "\n]}\n";
    return json;
}
} // namespace storm::audio_import
