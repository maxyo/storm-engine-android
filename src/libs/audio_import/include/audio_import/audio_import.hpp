#pragma once
// Import of FMOD Studio banks and WAV files into standalone Ogg Vorbis.
// Formats and provenance: src/libs/audio_import/README.md. Every malformed or
// unsupported input throws ImportError; nothing is skipped silently.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace storm::audio_import
{
using Bytes = std::vector<uint8_t>;
using ByteView = std::span<const uint8_t>;

struct ImportError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// ---- FMOD Studio bank (RIFF "FEV ") ----------------------------------------

// The FSB5 images listed by the bank's SNDH table, in table order. Banks
// without sample data (Master, strings) return an empty list.
std::vector<ByteView> findFsb5(ByteView bank);

struct Guid
{
    std::array<uint8_t, 16> bytes{}; // FMOD_GUID layout: u32, u16, u16, u8[8], little endian
    std::string toString() const;    // {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
};

struct StringEntry
{
    std::string path; // "event:/...", "bus:/...", "bank:/...", "snapshot:/...", ...
    Guid guid;
};

// Path/GUID pairs of the bank's STDT string table (Master.strings.bank),
// ordered by FMOD string index. Throws if the bank has no string table.
std::vector<StringEntry> readStringTable(ByteView bank);

// {"strings":[{"path":"...","guid":"{...}"},...]} with paths sorted.
std::string stringTableJson(const std::vector<StringEntry> &entries);

// ---- FSB5 -------------------------------------------------------------------

enum class Fsb5Codec : uint32_t
{
    Pcm8 = 1,
    Pcm16 = 2,
    Pcm24 = 3,
    Pcm32 = 4,
    PcmFloat = 5,
    Mpeg = 11,
    Vorbis = 15,
};

struct Fsb5Sample
{
    std::string name; // from the name table, or the decimal index when the bank has none
    uint32_t frequency = 0;
    uint32_t channels = 0;
    uint32_t sampleCount = 0;                           // PCM frames
    std::optional<std::pair<uint32_t, uint32_t>> loop;  // as stored: start, end
    std::optional<uint32_t> vorbisSetupCrc;             // CRC-32 of the Vorbis setup packet
    std::vector<uint32_t> chunkTypes;                   // every metadata chunk type, in order
    ByteView data;
};

struct Fsb5
{
    uint32_t version = 0;
    uint32_t codec = 0; // Fsb5Codec value as stored
    std::vector<Fsb5Sample> samples;
};

Fsb5 parseFsb5(ByteView image);

// ---- Vorbis -----------------------------------------------------------------

// FSB5 Vorbis strips the three header packets and keeps the CRC-32 of the
// setup packet. FMOD encoded with libvorbis, so the identical headers are
// regenerated with the pinned libvorbis encoder for the sample's channels and
// rate (README.md "Vorbis setup headers"). Results are cached per format.
class VorbisHeaderCache
{
  public:
    struct Headers
    {
        Bytes identification;
        Bytes setup;
    };
    // Throws ImportError naming the CRC when no encoder setting reproduces it.
    const Headers &find(uint32_t channels, uint32_t rate, uint32_t setupCrc);

  private:
    void generate(uint32_t channels, uint32_t rate, int qualityStep);
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, Headers> headers_;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> sweptPasses_;
};

uint32_t crc32(ByteView data);

// Standalone Ogg Vorbis stream for an FSB5 Vorbis sample. The audio packets are
// copied unchanged; the last granule position is the FSB5 sample count so
// decoders trim the encoder padding. Name and loop points become comments
// (TITLE, LOOPSTART, LOOPEND).
Bytes remuxFsb5Vorbis(const Fsb5Sample &sample, VorbisHeaderCache &headers);

// ---- WAV --------------------------------------------------------------------

struct WavInfo
{
    uint32_t channels = 0;
    uint32_t rate = 0;
    uint32_t bitsPerSample = 0;
    bool isFloat = false;
    uint64_t frames = 0;
};

WavInfo readWavInfo(ByteView wav);

// RIFF/WAVE PCM 8/16/24/32-bit integer or 32-bit float (also
// WAVE_FORMAT_EXTENSIBLE with those subformats) encoded with libvorbis VBR.
// quality is libvorbis' -0.1..1.0 scale.
Bytes encodeWavToVorbis(ByteView wav, float quality);
} // namespace storm::audio_import
