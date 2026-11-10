// RIFF/WAVE PCM or float -> Ogg Vorbis with the libvorbis VBR encoder.
#include "ogg_writer.hpp"

#include <vorbis/vorbisenc.h>

namespace storm::audio_import
{
namespace
{
constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xfffe;

struct Wav
{
    WavInfo info;
    ByteView samples;
};

Wav parseWav(ByteView data)
{
    detail::Reader reader(data, "WAV");
    if (reader.size() < 12 || reader.fourcc() != "RIFF")
        reader.fail("not a RIFF file");
    const uint32_t riffSize = reader.u32();
    if (reader.fourcc() != "WAVE")
        reader.fail("RIFF form is not WAVE");
    if (riffSize < 4 || riffSize > reader.size() - 8)
        reader.fail("RIFF size " + std::to_string(riffSize) + " exceeds the file");
    const size_t end = 8 + size_t{riffSize};

    std::optional<Wav> wav;
    bool haveFormat = false;
    uint16_t blockAlign = 0;
    while (end - reader.position() >= 8)
    {
        const std::string chunk = reader.fourcc();
        const uint32_t size = reader.u32();
        if (size > end - reader.position())
            reader.fail("chunk '" + chunk + "' exceeds the RIFF data");
        detail::Reader body(reader.take(size), "WAV chunk");
        if (chunk == "fmt ")
        {
            if (size < 16)
                body.fail("fmt chunk too short");
            uint16_t format = body.u16();
            Wav parsed;
            parsed.info.channels = body.u16();
            parsed.info.rate = body.u32();
            body.u32(); // byte rate
            blockAlign = body.u16();
            parsed.info.bitsPerSample = body.u16();
            if (format == kFormatExtensible)
            {
                if (size < 40 || body.u16() < 22)
                    body.fail("WAVE_FORMAT_EXTENSIBLE without its extension");
                body.u16(); // valid bits
                body.u32(); // channel mask
                format = body.u16(); // first two bytes of the subformat GUID
            }
            if (format != kFormatPcm && format != kFormatFloat)
                body.fail("unsupported WAV format tag " + std::to_string(format));
            parsed.info.isFloat = format == kFormatFloat;
            const uint32_t bitsPerSample = parsed.info.bitsPerSample;
            const bool supported = parsed.info.isFloat ? bitsPerSample == 32
                                                       : (bitsPerSample == 8 || bitsPerSample == 16 ||
                                                          bitsPerSample == 24 || bitsPerSample == 32);
            if (!supported)
                body.fail("unsupported " + std::string(parsed.info.isFloat ? "float" : "PCM") + " sample size " +
                          std::to_string(bitsPerSample));
            if (parsed.info.channels == 0 || parsed.info.rate == 0 ||
                blockAlign != parsed.info.channels * (bitsPerSample / 8))
                body.fail("inconsistent channels/rate/block alignment");
            wav = parsed;
            haveFormat = true;
        }
        else if (chunk == "data")
        {
            if (!haveFormat)
                body.fail("data chunk before fmt chunk");
            if (size % blockAlign != 0)
                body.fail("data size " + std::to_string(size) + " is not a whole number of frames");
            wav->samples = body.take(size);
            wav->info.frames = size / blockAlign;
            return *wav;
        }
        reader.seek(std::min(end, reader.position() + (size & 1)));
    }
    reader.fail(haveFormat ? "no data chunk" : "no fmt chunk");
}

float sampleAt(const uint8_t *p, const WavInfo &info)
{
    switch (info.bitsPerSample)
    {
    case 8:
        return (static_cast<int>(p[0]) - 128) / 128.0f;
    case 16:
        return static_cast<int16_t>(p[0] | p[1] << 8) / 32768.0f;
    case 24:
        return static_cast<int32_t>(static_cast<uint32_t>(p[0] << 8 | p[1] << 16 | p[2] << 24)) / 2147483648.0f;
    default: {
        const uint32_t bits = p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
        if (info.isFloat)
        {
            float value;
            std::memcpy(&value, &bits, sizeof value);
            return value;
        }
        return static_cast<int32_t>(bits) / 2147483648.0f;
    }
    }
}

class Encoder
{
  public:
    Encoder(const WavInfo &wav, float quality)
    {
        vorbis_info_init(&info_);
        if (vorbis_encode_init_vbr(&info_, static_cast<long>(wav.channels), static_cast<long>(wav.rate), quality) != 0)
        {
            vorbis_info_clear(&info_);
            throw ImportError("libvorbis cannot encode " + std::to_string(wav.channels) + " ch " +
                              std::to_string(wav.rate) + " Hz at quality " + std::to_string(quality));
        }
        vorbis_analysis_init(&dsp_, &info_);
        vorbis_block_init(&dsp_, &block_);
    }
    ~Encoder()
    {
        vorbis_block_clear(&block_);
        vorbis_dsp_clear(&dsp_);
        vorbis_info_clear(&info_);
    }
    Encoder(const Encoder &) = delete;
    Encoder &operator=(const Encoder &) = delete;

    vorbis_info info_{};
    vorbis_dsp_state dsp_{};
    vorbis_block block_{};
};
} // namespace

WavInfo readWavInfo(ByteView wav)
{
    return parseWav(wav).info;
}

Bytes encodeWavToVorbis(ByteView data, float quality)
{
    const Wav wav = parseWav(data);
    Encoder encoder(wav.info, quality);
    detail::CommentHeader comments;
    ogg_packet identification, comment, setup;
    if (vorbis_analysis_headerout(&encoder.dsp_, &comments.comment(), &identification, &comment, &setup) != 0)
        throw ImportError("vorbis_analysis_headerout failed");

    detail::OggWriter writer(static_cast<int>(crc32(data.first(std::min<size_t>(data.size(), 4096)))));
    writer.packet(identification);
    writer.packet(comment);
    writer.packet(setup);
    writer.flush();

    const auto drain = [&] {
        while (vorbis_analysis_blockout(&encoder.dsp_, &encoder.block_) == 1)
        {
            vorbis_analysis(&encoder.block_, nullptr);
            vorbis_bitrate_addblock(&encoder.block_);
            ogg_packet packet;
            while (vorbis_bitrate_flushpacket(&encoder.dsp_, &packet) == 1)
                writer.packet(packet);
        }
    };
    const size_t frameBytes = wav.info.channels * (wav.info.bitsPerSample / 8);
    const size_t sampleBytes = wav.info.bitsPerSample / 8;
    constexpr uint64_t kChunk = 4096;
    for (uint64_t frame = 0; frame < wav.info.frames; frame += kChunk)
    {
        const int count = static_cast<int>(std::min(kChunk, wav.info.frames - frame));
        float **buffer = vorbis_analysis_buffer(&encoder.dsp_, count);
        const uint8_t *p = wav.samples.data() + frame * frameBytes;
        for (int i = 0; i < count; ++i)
            for (uint32_t c = 0; c < wav.info.channels; ++c, p += sampleBytes)
                buffer[c][i] = sampleAt(p, wav.info);
        vorbis_analysis_wrote(&encoder.dsp_, count);
        drain();
    }
    vorbis_analysis_wrote(&encoder.dsp_, 0);
    drain();
    return writer.take();
}
} // namespace storm::audio_import
