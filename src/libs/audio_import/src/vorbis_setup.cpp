// Regenerates the Vorbis identification/setup headers that FSB5 strips.
// The FSB5 VORBISDATA chunk stores the CRC-32 of the original setup packet,
// which FMOD produced with the libvorbis encoder. The same encoder (pinned
// libvorbis) is run over its quality range for the sample's channel count
// and rate until a setup packet with that CRC appears. No pre-extracted
// header table is shipped. Details: README.md "Vorbis setup headers".
#include "reader.hpp"

#include <vorbis/vorbisenc.h>

#include <cstdio>

namespace storm::audio_import
{
namespace
{
const std::array<uint32_t, 256> &crcTable()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
            t[i] = c;
        }
        return t;
    }();
    return table;
}

Bytes packetBytes(const ogg_packet &packet)
{
    return Bytes(packet.packet, packet.packet + packet.bytes);
}

// Quality sweeps, in libvorbis hundredths (-10 .. 100): tenths first, then all.
constexpr int kQualitySteps[] = {10, 1};
} // namespace

uint32_t crc32(ByteView data)
{
    uint32_t c = 0xffffffffu;
    for (uint8_t byte : data)
        c = crcTable()[(c ^ byte) & 0xff] ^ (c >> 8);
    return ~c;
}

void VorbisHeaderCache::generate(uint32_t channels, uint32_t rate, int qualityStep)
{
    for (int q = -10; q <= 100; q += qualityStep)
    {
        vorbis_info info;
        vorbis_info_init(&info);
        if (vorbis_encode_init_vbr(&info, static_cast<long>(channels), static_cast<long>(rate), q / 100.0f) != 0)
        {
            vorbis_info_clear(&info); // libvorbis has no mode for this format/quality
            continue;
        }
        vorbis_dsp_state dsp;
        vorbis_comment comment;
        vorbis_comment_init(&comment);
        ogg_packet identification, commentPacket, setup;
        const bool ok = vorbis_analysis_init(&dsp, &info) == 0;
        if (ok && vorbis_analysis_headerout(&dsp, &comment, &identification, &commentPacket, &setup) == 0)
        {
            const uint32_t crc = crc32(ByteView(setup.packet, static_cast<size_t>(setup.bytes)));
            headers_.try_emplace({channels, rate, crc}, Headers{packetBytes(identification), packetBytes(setup)});
        }
        if (ok)
            vorbis_dsp_clear(&dsp);
        vorbis_comment_clear(&comment);
        vorbis_info_clear(&info);
        if (!ok)
            throw ImportError("libvorbis analysis init failed for " + std::to_string(channels) + " ch " +
                              std::to_string(rate) + " Hz");
    }
}

const VorbisHeaderCache::Headers &VorbisHeaderCache::find(uint32_t channels, uint32_t rate, uint32_t setupCrc)
{
    const auto key = std::make_tuple(channels, rate, setupCrc);
    uint32_t &passes = sweptPasses_[{channels, rate}];
    for (;;)
    {
        if (auto it = headers_.find(key); it != headers_.end())
            return it->second;
        if (passes == std::size(kQualitySteps))
        {
            char crc[16];
            std::snprintf(crc, sizeof crc, "%08x", setupCrc);
            throw ImportError(std::string("no libvorbis encoder setting reproduces Vorbis setup CRC ") + crc + " for " +
                              std::to_string(channels) + " ch " + std::to_string(rate) + " Hz");
        }
        generate(channels, rate, kQualitySteps[passes++]);
    }
}
} // namespace storm::audio_import
