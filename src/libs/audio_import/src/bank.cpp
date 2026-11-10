#include "riff.hpp"

namespace storm::audio_import
{
namespace detail
{
namespace
{
std::optional<ByteView> findIn(Reader &reader, size_t end, const char *id, int depth)
{
    if (depth > 16)
        reader.fail("RIFF nesting too deep");
    while (reader.position() < end)
    {
        if (end - reader.position() < 8)
            reader.fail("partial RIFF chunk header");
        const std::string chunk = reader.fourcc();
        const uint32_t size = reader.u32();
        const size_t body = reader.position();
        if (size > end - body)
            reader.fail("RIFF chunk '" + chunk + "' exceeds its parent");
        if (chunk == id)
            return reader.take(size);
        if (chunk == "LIST")
        {
            if (size < 4)
                reader.fail("LIST chunk without a type");
            reader.seek(body + 4);
            if (auto found = findIn(reader, body + size, id, depth + 1))
                return found;
        }
        // Chunks are word aligned; the pad byte may be missing on the last one.
        reader.seek(std::min(end, body + size + (size & 1)));
    }
    return std::nullopt;
}
} // namespace

std::optional<ByteView> findBankChunk(ByteView bank, const char *id)
{
    Reader reader(bank, "FMOD bank");
    if (reader.size() < 12 || reader.fourcc() != "RIFF")
        reader.fail("not a RIFF file");
    const uint32_t size = reader.u32();
    if (size < 4 || size > reader.remaining())
        reader.fail("RIFF size " + std::to_string(size) + " does not match the file");
    if (reader.fourcc() != "FEV ")
        reader.fail("RIFF form is not 'FEV '");
    return findIn(reader, 8 + size, id, 0);
}

uint32_t readStructArrayHeader(Reader &reader, uint32_t elementSize)
{
    const uint16_t header = reader.u16();
    if ((header & 1) == 0)
        reader.fail("array header " + std::to_string(header) + " lacks the element-size flag");
    const uint16_t size = reader.u16();
    if (size != elementSize)
        reader.fail("array element size " + std::to_string(size) + ", expected " + std::to_string(elementSize));
    return header >> 1;
}
} // namespace detail

std::vector<ByteView> findFsb5(ByteView bank)
{
    std::vector<ByteView> images;
    const auto table = detail::findBankChunk(bank, "SNDH");
    if (!table || table->empty())
        return images;
    // SNDH: one (absolute file offset, size) entry per FSB5 image in SND chunks.
    detail::Reader reader(*table, "SNDH");
    const uint32_t count = detail::readStructArrayHeader(reader, 8);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t offset = reader.u32();
        const uint32_t size = reader.u32();
        if (offset > bank.size() || size > bank.size() - offset)
            reader.fail("FSB5 image " + std::to_string(i) + " lies outside the bank");
        ByteView image = bank.subspan(offset, size);
        if (size < 4 || std::memcmp(image.data(), "FSB5", 4) != 0)
            reader.fail("SNDH entry " + std::to_string(i) + " does not point at an FSB5 header");
        images.push_back(image);
    }
    if (reader.remaining() != 0)
        reader.fail("trailing bytes in SNDH");
    return images;
}
} // namespace storm::audio_import
