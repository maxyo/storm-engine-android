#pragma once

// In-game video playback on FFmpeg: a demux/audio thread and a video decode thread fill a
// small queue of RGBA frames; the game thread presents them against the audio clock.
// Decoder policy (hardware/software per codec) and the threading model: README.md next to
// this library. Nothing here touches the engine core, the renderer or the sound service:
// callers adapt those through Source and AudioSink (see xinterface/aviplayer).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace storm::video
{

enum class LogLevel
{
    Info,
    Warning,
    Error
};
using LogSink = void (*)(LogLevel level, const std::string &message);
// Every decision (decoder choice, fallbacks, errors) and FFmpeg's own warnings and errors are
// reported here. Without a sink they go to stderr. Set once, before the first player.
void setLogSink(LogSink sink);

// Random-access byte source of a media file.
class Source
{
  public:
    virtual ~Source() = default;
    // Bytes read; 0 at the end, negative on error.
    virtual int64_t read(uint8_t *data, size_t size) = 0;
    // whence: SEEK_SET, SEEK_CUR or SEEK_END. New position, or negative on error.
    virtual int64_t seek(int64_t offset, int whence) = 0;
    // Total size in bytes, negative when unknown.
    virtual int64_t size() = 0;
};
// nullptr (and an error in the log) when the file cannot be opened.
std::unique_ptr<Source> openFile(const std::filesystem::path &path);

// Audio output. The player writes interleaved stereo float PCM at kAudioRate from its demux
// thread; playedFrames() is the master clock and may be read from any thread.
constexpr uint32_t kAudioRate = 48000;
constexpr uint32_t kAudioChannels = 2;
class AudioSink
{
  public:
    virtual ~AudioSink() = default;
    // Frames accepted (fewer than offered when the sink's buffer is full).
    virtual uint32_t write(const float *pcm, uint32_t frames) = 0;
    // Frames the output has consumed so far.
    virtual uint64_t playedFrames() = 0;
    virtual void setPaused(bool paused) = 0;
};

enum class Decoding
{
    Software,
    Hardware
};

struct StreamInfo
{
    int width = 0, height = 0;       // coded picture size
    int outputWidth = 0, outputHeight = 0; // RGBA frame size (scaled down to the bounds, never up)
    double aspect = 1.0;             // display aspect ratio (width / height, sample aspect applied)
    double duration = 0.0;           // seconds, 0 when unknown
    std::string videoCodec;          // FFmpeg decoder name actually used, e.g. "vp9_mediacodec"
    Decoding videoDecoding = Decoding::Software;
    std::string audioCodec;          // empty without an audio stream
    int audioRate = 0, audioChannels = 0;
};

struct Frame
{
    int width = 0, height = 0;
    double pts = 0.0;          // seconds from the start of the file
    std::vector<uint8_t> rgba; // width * height * 4 bytes, rows top to bottom, R G B A
};

struct Options
{
    // Bounds of the RGBA output (usually the screen); larger pictures are scaled down.
    int maxWidth = 1 << 14, maxHeight = 1 << 14;
};

class Player
{
  public:
    // name is only used in log lines. audio may be null (no audio output): the video then
    // plays on the wall clock and the missing sound is logged as a warning when the file has
    // an audio stream. Returns nullptr after logging the reason when the file cannot be played.
    static std::unique_ptr<Player> open(std::unique_ptr<Source> source, const std::string &name,
                                        std::unique_ptr<AudioSink> audio, const Options &options = {});
    virtual ~Player() = default;

    virtual const StreamInfo &info() const = 0;
    // Game thread, once per frame: advances the clock and picks the frame to show.
    // Returns the frame to display (nullptr before the first one is decoded); *changed is set
    // when it differs from the previous call's frame. The frame stays valid until the next call.
    virtual const Frame *update(bool *changed) = 0;
    // True once every frame has been shown and the sound has played out, or after a decode error.
    virtual bool finished() const = 0;
    virtual bool failed() const = 0;
    // Stops the clock and the audio output (application in background).
    virtual void setPaused(bool paused) = 0;
    // Current presentation time in seconds.
    virtual double clock() const = 0;
};

} // namespace storm::video
