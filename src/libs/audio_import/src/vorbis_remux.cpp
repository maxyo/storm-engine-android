// FSB5 Vorbis -> Ogg Vorbis. The packet framing (u16 size + packet, 0 ends)
// and granule rule follow python-fsb5 (MIT, Simon Pinfold); see README.md.
#include "ogg_writer.hpp"

namespace storm::audio_import
{
namespace
{
ogg_packet makePacket(Bytes &bytes, int64_t number)
{
    ogg_packet packet{};
    packet.packet = bytes.data();
    packet.bytes = static_cast<long>(bytes.size());
    packet.packetno = number;
    return packet;
}

// Decoder-side vorbis_info for vorbis_packet_blocksize.
class DecoderInfo
{
  public:
    DecoderInfo(Bytes &identification, Bytes &comment, Bytes &setup)
    {
        vorbis_info_init(&info_);
        vorbis_comment_init(&comment_);
        Bytes *packets[] = {&identification, &comment, &setup};
        for (int i = 0; i < 3; ++i)
        {
            ogg_packet packet = makePacket(*packets[i], i);
            packet.b_o_s = i == 0;
            if (vorbis_synthesis_headerin(&info_, &comment_, &packet) != 0)
                throw ImportError("rebuilt Vorbis header " + std::to_string(i) + " was rejected by libvorbis");
        }
    }
    ~DecoderInfo()
    {
        vorbis_comment_clear(&comment_);
        vorbis_info_clear(&info_);
    }
    vorbis_info *info()
    {
        return &info_;
    }

  private:
    vorbis_info info_{};
    vorbis_comment comment_{};
};
} // namespace

Bytes remuxFsb5Vorbis(const Fsb5Sample &sample, VorbisHeaderCache &cache)
{
    const std::string what = "FSB5 Vorbis sample '" + sample.name + "'";
    if (!sample.vorbisSetupCrc)
        throw ImportError(what + " has no VORBISDATA chunk");
    const auto &headers = cache.find(sample.channels, sample.frequency, *sample.vorbisSetupCrc);

    detail::CommentHeader comments;
    comments.add("TITLE", sample.name);
    if (sample.loop)
    {
        comments.add("LOOPSTART", std::to_string(sample.loop->first));
        comments.add("LOOPEND", std::to_string(sample.loop->second));
    }
    ogg_packet &commentPacket = comments.packet();
    Bytes identification = headers.identification;
    Bytes comment(commentPacket.packet, commentPacket.packet + commentPacket.bytes);
    Bytes setup = headers.setup;
    DecoderInfo decoder(identification, comment, setup);

    // Audio packets: u16 little-endian size, payload; size 0 or the end stops.
    std::vector<ByteView> packets;
    detail::Reader reader(sample.data, what.c_str());
    while (reader.remaining() >= 2)
    {
        const uint16_t size = reader.u16();
        if (size == 0)
            break;
        packets.push_back(reader.take(size));
    }
    if (packets.empty())
        throw ImportError(what + " contains no audio packets");

    detail::OggWriter writer(static_cast<int>(crc32(ByteView(
        reinterpret_cast<const uint8_t *>(sample.name.data()), sample.name.size()))));
    Bytes *headerPackets[] = {&identification, &comment, &setup};
    for (int i = 0; i < 3; ++i)
    {
        ogg_packet packet = makePacket(*headerPackets[i], i);
        packet.b_o_s = i == 0;
        writer.packet(packet);
    }
    writer.flush();

    int64_t granule = 0;
    long previousBlock = 0;
    for (size_t i = 0; i < packets.size(); ++i)
    {
        Bytes payload(packets[i].begin(), packets[i].end());
        ogg_packet packet = makePacket(payload, static_cast<int64_t>(i) + 3);
        const long block = vorbis_packet_blocksize(decoder.info(), &packet);
        if (block <= 0)
            throw ImportError(what + ": audio packet " + std::to_string(i) + " is not a valid Vorbis audio packet");
        if (previousBlock != 0)
            granule += (previousBlock + block) / 4;
        previousBlock = block;
        const bool last = i + 1 == packets.size();
        if (last)
        {
            // The encoder pads the final block; the FSB5 header holds the exact length.
            if (sample.sampleCount > granule)
                throw ImportError(what + ": FSB5 length " + std::to_string(sample.sampleCount) +
                                  " exceeds the decodable " + std::to_string(granule) + " frames");
            granule = sample.sampleCount;
            packet.e_o_s = 1;
        }
        packet.granulepos = granule;
        writer.packet(packet);
    }
    return writer.take();
}
} // namespace storm::audio_import
