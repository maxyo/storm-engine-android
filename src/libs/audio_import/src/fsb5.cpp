// FSB5 container parsing. Layout per the MIT-licensed python-fsb5
// (Simon Pinfold, https://github.com/HearthSim/python-fsb5) and the public
// vgmstream format notes; see README.md.
#include "reader.hpp"

namespace storm::audio_import
{
namespace
{
enum ChunkType : uint32_t
{
    ChunkChannels = 1,
    ChunkFrequency = 2,
    ChunkLoop = 3,
    ChunkVorbisData = 11,
};

uint32_t frequencyFromCode(uint32_t code)
{
    switch (code)
    {
    case 1: return 8000;
    case 2: return 11000;
    case 3: return 11025;
    case 4: return 16000;
    case 5: return 22050;
    case 6: return 24000;
    case 7: return 32000;
    case 8: return 44100;
    case 9: return 48000;
    case 10: return 96000;
    default: return 0;
    }
}

uint64_t bits(uint64_t value, unsigned start, unsigned count)
{
    return (value >> start) & ((uint64_t{1} << count) - 1);
}
} // namespace

Fsb5 parseFsb5(ByteView image)
{
    detail::Reader reader(image, "FSB5");
    if (reader.size() < 60 || reader.fourcc() != "FSB5")
        reader.fail("missing FSB5 magic");
    Fsb5 fsb;
    fsb.version = reader.u32();
    if (fsb.version > 1)
        reader.fail("unsupported FSB5 version " + std::to_string(fsb.version));
    const uint32_t sampleCount = reader.u32();
    const uint32_t sampleHeadersSize = reader.u32();
    const uint32_t nameTableSize = reader.u32();
    const uint32_t dataSize = reader.u32();
    fsb.codec = reader.u32();
    reader.take(8 + 16 + 8); // zero, hash, dummy
    if (fsb.version == 0)
        reader.u32();
    const size_t headersStart = reader.position();
    const uint64_t total = uint64_t{headersStart} + sampleHeadersSize + nameTableSize + dataSize;
    if (total > image.size())
        reader.fail("sections need " + std::to_string(total) + " bytes, image has " + std::to_string(image.size()));
    const size_t nameTableStart = headersStart + sampleHeadersSize;
    const size_t dataStart = nameTableStart + nameTableSize;

    std::vector<uint32_t> offsets;
    for (uint32_t i = 0; i < sampleCount; ++i)
    {
        Fsb5Sample sample;
        const uint64_t raw = reader.u64();
        bool more = bits(raw, 0, 1) != 0;
        sample.frequency = frequencyFromCode(static_cast<uint32_t>(bits(raw, 1, 4)));
        sample.channels = static_cast<uint32_t>(bits(raw, 5, 1)) + 1;
        offsets.push_back(static_cast<uint32_t>(bits(raw, 6, 28) * 16));
        sample.sampleCount = static_cast<uint32_t>(bits(raw, 34, 30));
        while (more)
        {
            const uint32_t chunk = reader.u32();
            more = (chunk & 1) != 0;
            const uint32_t size = (chunk >> 1) & 0xffffff;
            const uint32_t type = chunk >> 25;
            detail::Reader body(reader.take(size), "FSB5 metadata chunk");
            sample.chunkTypes.push_back(type);
            switch (type)
            {
            case ChunkChannels:
                sample.channels = body.u8();
                break;
            case ChunkFrequency:
                sample.frequency = body.u32();
                break;
            case ChunkLoop: {
                const uint32_t start = body.u32();
                sample.loop = std::make_pair(start, body.u32());
                break;
            }
            case ChunkVorbisData:
                sample.vorbisSetupCrc = body.u32(); // a seek table follows; unused
                break;
            default:
                break; // other chunk types do not affect decoding; kept in chunkTypes
            }
        }
        if (sample.frequency == 0)
            reader.fail("sample " + std::to_string(i) + " has an unknown frequency code and no frequency chunk");
        if (sample.channels == 0)
            reader.fail("sample " + std::to_string(i) + " has zero channels");
        sample.name = std::to_string(i);
        fsb.samples.push_back(std::move(sample));
    }
    if (reader.position() != nameTableStart)
        reader.fail("sample headers end at " + std::to_string(reader.position()) + ", declared " +
                    std::to_string(nameTableStart));

    if (nameTableSize != 0)
    {
        ByteView table = image.subspan(nameTableStart, nameTableSize);
        detail::Reader names(table, "FSB5 name table");
        for (auto &sample : fsb.samples)
        {
            const uint32_t offset = names.u32();
            if (offset >= table.size())
                names.fail("name offset outside the table");
            const auto *begin = table.data() + offset;
            const auto *end = static_cast<const uint8_t *>(std::memchr(begin, 0, table.size() - offset));
            if (end == nullptr)
                names.fail("unterminated sample name");
            sample.name.assign(reinterpret_cast<const char *>(begin), end - begin);
        }
    }

    for (size_t i = 0; i < fsb.samples.size(); ++i)
    {
        const uint32_t begin = offsets[i];
        const uint32_t end = i + 1 < offsets.size() ? offsets[i + 1] : dataSize;
        if (begin > end || end > dataSize)
            reader.fail("sample " + std::to_string(i) + " data range " + std::to_string(begin) + ".." +
                        std::to_string(end) + " is invalid");
        fsb.samples[i].data = image.subspan(dataStart + begin, end - begin);
    }
    return fsb;
}
} // namespace storm::audio_import
