#include "storm/video/player.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifdef __ANDROID__
#include <libavcodec/jni.h>
#endif
}

#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>
#endif

namespace storm::video
{
namespace
{
using Clock = std::chrono::steady_clock;

std::atomic<LogSink> logSink{nullptr};

void log(LogLevel level, const std::string &message)
{
    if (const LogSink sink = logSink.load())
        sink(level, message);
    else
        std::fprintf(stderr, "[video] %s\n", message.c_str());
}

template <typename... Args> std::string format(const char *pattern, Args... args)
{
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), pattern, args...);
    return buffer;
}

std::string averror(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

// FFmpeg's warnings and errors are reported, never swallowed; its info/debug chatter is not.
void ffmpegLog(void *context, int level, const char *pattern, va_list args)
{
    if (level > AV_LOG_WARNING)
        return;
    static thread_local int prefix = 1;
    char line[1024];
    av_log_format_line2(context, level, pattern, args, line, sizeof(line), &prefix);
    std::string text = line;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    if (!text.empty())
        log(level <= AV_LOG_ERROR ? LogLevel::Error : LogLevel::Warning, "FFmpeg: " + text);
}

void initialiseOnce()
{
    static std::once_flag once;
    std::call_once(once, [] {
        av_log_set_callback(ffmpegLog);
#ifdef __ANDROID__
        // MediaCodec decoders look up hardware codecs through the Java MediaCodecList.
        JavaVM *vm = nullptr;
        auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        if (!env || env->GetJavaVM(&vm) != JNI_OK || av_jni_set_java_vm(vm, nullptr) < 0)
            log(LogLevel::Error, "no Java VM for MediaCodec: hardware VP8/VP9 decoding is unavailable");
#endif
    });
}

// Decoder policy, one row per codec the game videos use (README.md, "Decoder policy").
// hardware: the MediaCodec wrapper tried first on Android. If it cannot be opened (the device
// has no hardware decoder for the codec), the software decoder is used and a warning is logged.
struct CodecPolicy
{
    AVCodecID id;
    const char *software;
    const char *hardware;
};
constexpr CodecPolicy kPolicy[] = {
    {AV_CODEC_ID_WMV3, "wmv3", nullptr},
    {AV_CODEC_ID_VC1, "vc1", nullptr},
    {AV_CODEC_ID_VP9, "vp9", "vp9_mediacodec"},
    {AV_CODEC_ID_VP8, "vp8", "vp8_mediacodec"},
    {AV_CODEC_ID_WMAV2, "wmav2", nullptr},
    {AV_CODEC_ID_VORBIS, "vorbis", nullptr},
    {AV_CODEC_ID_OPUS, "opus", nullptr},
};

const CodecPolicy *policyFor(AVCodecID id)
{
    for (const auto &row : kPolicy)
        if (row.id == id)
            return &row;
    return nullptr;
}

struct CodecDeleter
{
    void operator()(AVCodecContext *context) const
    {
        avcodec_free_context(&context);
    }
};
using CodecContext = std::unique_ptr<AVCodecContext, CodecDeleter>;

CodecContext openDecoder(const AVStream *stream, const char *name, int threads, std::string *error)
{
    const AVCodec *codec = avcodec_find_decoder_by_name(name);
    if (!codec)
    {
        *error = "decoder not built";
        return nullptr;
    }
    CodecContext context(avcodec_alloc_context3(codec));
    int result = avcodec_parameters_to_context(context.get(), stream->codecpar);
    if (result >= 0)
    {
        context->pkt_timebase = stream->time_base;
        context->thread_count = threads;
        context->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        result = avcodec_open2(context.get(), codec, nullptr);
    }
    if (result < 0)
    {
        *error = averror(result);
        return nullptr;
    }
    return context;
}

class FileSource final : public Source
{
  public:
    explicit FileSource(std::ifstream file) : file_(std::move(file))
    {
    }
    int64_t read(uint8_t *data, size_t size) override
    {
        file_.read(reinterpret_cast<char *>(data), static_cast<std::streamsize>(size));
        const auto count = file_.gcount();
        if (count == 0 && file_.bad())
            return -1;
        file_.clear(file_.rdstate() & ~std::ios::failbit & ~std::ios::eofbit);
        return count;
    }
    int64_t seek(int64_t offset, int whence) override
    {
        file_.clear();
        file_.seekg(offset, whence == SEEK_SET ? std::ios::beg : whence == SEEK_CUR ? std::ios::cur : std::ios::end);
        return file_ ? static_cast<int64_t>(file_.tellg()) : -1;
    }
    int64_t size() override
    {
        const auto position = file_.tellg();
        file_.seekg(0, std::ios::end);
        const auto end = file_.tellg();
        file_.seekg(position);
        return end;
    }

  private:
    std::ifstream file_;
};

// Packets for the video thread. Bounded by bytes so that a broken file cannot grow it without
// limit; real files keep well under a second of video here (the audio buffer paces demuxing).
class PacketQueue
{
  public:
    static constexpr size_t kMaxBytes = 64u << 20;

    // nullptr marks the end of the stream. False when the queue was aborted.
    bool push(AVPacket *packet)
    {
        std::unique_lock lock(mutex_);
        const size_t size = packet ? static_cast<size_t>(packet->size) : 0;
        space_.wait(lock, [&] { return abort_ || bytes_ + size <= kMaxBytes || packets_.empty(); });
        if (abort_)
        {
            av_packet_free(&packet);
            return false;
        }
        bytes_ += size;
        packets_.push_back(packet);
        ready_.notify_one();
        return true;
    }
    // False when aborted; *packet is nullptr at the end of the stream.
    bool pop(AVPacket **packet)
    {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [&] { return abort_ || !packets_.empty(); });
        if (abort_)
            return false;
        *packet = packets_.front();
        packets_.pop_front();
        bytes_ -= *packet ? static_cast<size_t>((*packet)->size) : 0;
        space_.notify_one();
        return true;
    }
    void abort()
    {
        std::lock_guard lock(mutex_);
        abort_ = true;
        for (AVPacket *packet : packets_)
            av_packet_free(&packet);
        packets_.clear();
        ready_.notify_all();
        space_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable ready_, space_;
    std::deque<AVPacket *> packets_;
    size_t bytes_ = 0;
    bool abort_ = false;
};

class PlayerImpl final : public Player
{
  public:
    PlayerImpl(std::unique_ptr<Source> source, std::string name, std::unique_ptr<AudioSink> audio)
        : source_(std::move(source)), name_(std::move(name)), audio_(std::move(audio))
    {
    }

    ~PlayerImpl() override
    {
        stop_ = true;
        packets_.abort();
        {
            std::lock_guard lock(framesMutex_);
            framesSpace_.notify_all();
        }
        if (demuxThread_.joinable())
            demuxThread_.join();
        if (videoThread_.joinable())
            videoThread_.join();
        sws_freeContext(sws_);
        swr_free(&swr_);
        audioCodec_.reset();
        videoCodec_.reset();
        avformat_close_input(&format_);
        if (io_)
            av_freep(&io_->buffer);
        avio_context_free(&io_);
    }

    bool open(const Options &options)
    {
        constexpr int kIoBuffer = 64 * 1024;
        auto *buffer = static_cast<uint8_t *>(av_malloc(kIoBuffer));
        io_ = avio_alloc_context(buffer, kIoBuffer, 0, source_.get(), &PlayerImpl::readPacket, nullptr,
                                 &PlayerImpl::seekSource);
        format_ = avformat_alloc_context();
        format_->pb = io_;
        int result = avformat_open_input(&format_, nullptr, nullptr, nullptr);
        if (result < 0)
            return fail("unrecognised or unsupported container: " + averror(result));
        if ((result = avformat_find_stream_info(format_, nullptr)) < 0)
            return fail("cannot read stream parameters: " + averror(result));
        startTime_ = format_->start_time != AV_NOPTS_VALUE ? static_cast<double>(format_->start_time) / AV_TIME_BASE : 0.0;
        info_.duration = format_->duration > 0 ? static_cast<double>(format_->duration) / AV_TIME_BASE : 0.0;

        videoIndex_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoIndex_ < 0)
            return fail("no video stream");
        if (!openVideo(format_->streams[videoIndex_], options))
            return false;
        audioIndex_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, videoIndex_, nullptr, 0);
        if (audioIndex_ >= 0 && !openAudio(format_->streams[audioIndex_]))
            return false;
        for (unsigned i = 0; i < format_->nb_streams; ++i)
            if (static_cast<int>(i) != videoIndex_ && static_cast<int>(i) != audioIndex_)
                format_->streams[i]->discard = AVDISCARD_ALL;

        log(LogLevel::Info,
            format("%s: %dx%d %s (%s) -> %dx%d RGBA, %s, %.1f s", name_.c_str(), info_.width, info_.height,
                   info_.videoCodec.c_str(), info_.videoDecoding == Decoding::Hardware ? "hardware" : "software",
                   info_.outputWidth, info_.outputHeight,
                   audioIndex_ >= 0 ? format("%s %d Hz %d ch", info_.audioCodec.c_str(), info_.audioRate,
                                             info_.audioChannels)
                                          .c_str()
                                    : "no audio",
                   info_.duration));
        if (audioIndex_ < 0 || !audio_)
        {
            if (audioIndex_ >= 0)
                log(LogLevel::Warning, name_ + ": no audio output available, playing without sound");
            audio_.reset();
            audioActive_ = false;
        }
        else
        {
            audioActive_ = true;
        }
        wallAt_ = Clock::now();
        demuxThread_ = std::thread([this] { demuxLoop(); });
        videoThread_ = std::thread([this] { videoLoop(); });
        return true;
    }

    const StreamInfo &info() const override
    {
        return info_;
    }

    const Frame *update(bool *changed) override
    {
        *changed = false;
        const double now = advanceClock();
        std::lock_guard lock(framesMutex_);
        // Show the newest decoded frame that is due; earlier ones are dropped (late decoder or
        // a slow game frame). The very first frame is shown as soon as it exists.
        while (!ready_.empty() && (ready_.front().pts <= now || !shown_))
        {
            if (shown_)
                spare_.push_back(std::move(current_));
            current_ = std::move(ready_.front());
            ready_.pop_front();
            shown_ = true;
            *changed = true;
            framesSpace_.notify_one();
        }
        if (!finished_ && (videoEnded_ && ready_.empty()) &&
            (!audioActive_ || audioDrained_) && now >= lastPts_ + frameDuration_)
            finished_ = true;
        return shown_ ? &current_ : nullptr;
    }

    bool finished() const override
    {
        return finished_ || failed_;
    }

    bool failed() const override
    {
        return failed_;
    }

    void setPaused(bool paused) override
    {
        if (paused == paused_)
            return;
        if (paused)
            pausedClock_ = advanceClock();
        paused_ = paused;
        if (audio_)
            audio_->setPaused(paused);
        if (!paused)
        {
            wallClock_ = pausedClock_;
            wallAt_ = Clock::now();
        }
    }

    double clock() const override
    {
        return clock_;
    }

  private:
    static int readPacket(void *opaque, uint8_t *data, int size)
    {
        const int64_t count = static_cast<Source *>(opaque)->read(data, static_cast<size_t>(size));
        return count > 0 ? static_cast<int>(count) : count == 0 ? AVERROR_EOF : AVERROR(EIO);
    }

    static int64_t seekSource(void *opaque, int64_t offset, int whence)
    {
        auto *source = static_cast<Source *>(opaque);
        if (whence & AVSEEK_SIZE)
            return source->size();
        return source->seek(offset, whence & ~AVSEEK_FORCE);
    }

    bool fail(const std::string &message)
    {
        log(LogLevel::Error, name_ + ": " + message);
        failed_ = true;
        return false;
    }

    double seconds(int64_t pts, AVRational base) const
    {
        return pts == AV_NOPTS_VALUE ? -1.0 : static_cast<double>(pts) * av_q2d(base) - startTime_;
    }

    bool openVideo(const AVStream *stream, const Options &options)
    {
        const CodecPolicy *policy = policyFor(stream->codecpar->codec_id);
        if (!policy)
            return fail(std::string("video codec ") + avcodec_get_name(stream->codecpar->codec_id) +
                        " is not supported (README.md, decoder policy)");
        std::string error;
#ifdef __ANDROID__
        if (policy->hardware)
        {
            videoCodec_ = openDecoder(stream, policy->hardware, 1, &error);
            if (videoCodec_)
                info_.videoDecoding = Decoding::Hardware;
            else
                log(LogLevel::Warning,
                    format("%s: no hardware %s decoder on this device (%s); decoding in software, large videos may "
                           "drop frames",
                           name_.c_str(), avcodec_get_name(policy->id), error.c_str()));
        }
#endif
        if (!videoCodec_)
        {
            const int threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 4u));
            videoCodec_ = openDecoder(stream, policy->software, threads, &error);
            if (!videoCodec_)
                return fail(std::string("cannot open the ") + policy->software + " decoder: " + error);
        }
        info_.videoCodec = videoCodec_->codec->name;
        info_.width = videoCodec_->width;
        info_.height = videoCodec_->height;
        if (info_.width <= 0 || info_.height <= 0)
            return fail("video stream has no picture size");
        const AVRational sar = av_guess_sample_aspect_ratio(format_, const_cast<AVStream *>(stream), nullptr);
        info_.aspect = static_cast<double>(info_.width) / info_.height * (sar.num > 0 && sar.den > 0 ? av_q2d(sar) : 1.0);
        const double scale = std::min({1.0, static_cast<double>(options.maxWidth) / info_.width,
                                       static_cast<double>(options.maxHeight) / info_.height});
        info_.outputWidth = std::max(2, static_cast<int>(info_.width * scale) & ~1);
        info_.outputHeight = std::max(2, static_cast<int>(info_.height * scale) & ~1);
        videoBase_ = stream->time_base;
        const AVRational rate = av_guess_frame_rate(format_, const_cast<AVStream *>(stream), nullptr);
        frameDuration_ = rate.num > 0 && rate.den > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 25;
        return true;
    }

    bool openAudio(const AVStream *stream)
    {
        const CodecPolicy *policy = policyFor(stream->codecpar->codec_id);
        if (!policy)
            return fail(std::string("audio codec ") + avcodec_get_name(stream->codecpar->codec_id) +
                        " is not supported (README.md, decoder policy)");
        std::string error;
        audioCodec_ = openDecoder(stream, policy->software, 1, &error);
        if (!audioCodec_)
            return fail(std::string("cannot open the ") + policy->software + " decoder: " + error);
        info_.audioCodec = audioCodec_->codec->name;
        info_.audioRate = audioCodec_->sample_rate;
        info_.audioChannels = audioCodec_->ch_layout.nb_channels;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        const int result = swr_alloc_set_opts2(&swr_, &stereo, AV_SAMPLE_FMT_FLT, static_cast<int>(kAudioRate),
                                               &audioCodec_->ch_layout, audioCodec_->sample_fmt,
                                               audioCodec_->sample_rate, 0, nullptr);
        if (result < 0 || swr_init(swr_) < 0)
            return fail("cannot convert the audio to 48 kHz stereo: " + averror(result));
        audioBase_ = stream->time_base;
        return true;
    }

    // Demux thread: routes video packets to the video thread and decodes audio into the sink.
    // The sink's bounded buffer paces the whole pipeline to real time.
    void demuxLoop()
    {
        AVPacket *packet = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        bool ok = true;
        while (!stop_ && ok)
        {
            const int result = av_read_frame(format_, packet);
            if (result == AVERROR_EOF)
                break;
            if (result < 0)
            {
                streamError("reading the file failed: " + averror(result));
                ok = false;
                break;
            }
            if (packet->stream_index == videoIndex_)
            {
                AVPacket *queued = av_packet_alloc();
                av_packet_move_ref(queued, packet);
                if (!packets_.push(queued))
                    ok = false;
            }
            else if (packet->stream_index == audioIndex_ && audio_)
            {
                ok = decodeAudio(packet, frame);
            }
            av_packet_unref(packet);
        }
        if (ok && !stop_)
        {
            packets_.push(nullptr);
            if (audio_)
                decodeAudio(nullptr, frame);
        }
        audioEnded_ = true;
        av_frame_free(&frame);
        av_packet_free(&packet);
    }

    bool decodeAudio(const AVPacket *packet, AVFrame *frame)
    {
        int result = avcodec_send_packet(audioCodec_.get(), packet);
        if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
        {
            // A damaged audio packet: reported, the rest of the stream continues.
            log(LogLevel::Warning, name_ + ": audio packet rejected: " + averror(result));
            return true;
        }
        while (!stop_ && (result = avcodec_receive_frame(audioCodec_.get(), frame)) >= 0)
        {
            if (!audioStarted_)
            {
                const double pts = seconds(frame->best_effort_timestamp, audioBase_);
                audioStart_ = std::max(0.0, pts);
                audioStarted_ = true;
            }
            const int capacity = swr_get_out_samples(swr_, frame->nb_samples);
            pcm_.resize(static_cast<size_t>(std::max(capacity, 0)) * kAudioChannels);
            uint8_t *out = reinterpret_cast<uint8_t *>(pcm_.data());
            const int converted =
                swr_convert(swr_, &out, capacity, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
            av_frame_unref(frame);
            if (converted < 0)
            {
                streamError("audio conversion failed: " + averror(converted));
                return false;
            }
            if (!writeAudio(pcm_.data(), static_cast<uint32_t>(converted)))
                return false;
        }
        if (!packet && !stop_)
        {
            // Flush the resampler's delay line at the end of the stream.
            pcm_.resize(4096 * kAudioChannels);
            uint8_t *out = reinterpret_cast<uint8_t *>(pcm_.data());
            int converted;
            while ((converted = swr_convert(swr_, &out, 4096, nullptr, 0)) > 0)
                if (!writeAudio(pcm_.data(), static_cast<uint32_t>(converted)))
                    return false;
        }
        return true;
    }

    bool writeAudio(const float *pcm, uint32_t frames)
    {
        while (frames && !stop_)
        {
            const uint32_t accepted = audio_->write(pcm, frames);
            written_ += accepted;
            pcm += static_cast<size_t>(accepted) * kAudioChannels;
            frames -= accepted;
            if (frames)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return !stop_;
    }

    void videoLoop()
    {
        AVFrame *frame = av_frame_alloc();
        AVPacket *packet = nullptr;
        bool ok = true;
        while (ok && !stop_ && packets_.pop(&packet))
        {
            const bool end = packet == nullptr;
            int result = avcodec_send_packet(videoCodec_.get(), packet);
            av_packet_free(&packet);
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
                log(LogLevel::Warning, name_ + ": video packet rejected: " + averror(result));
            while (ok && !stop_ && (result = avcodec_receive_frame(videoCodec_.get(), frame)) >= 0)
            {
                ok = present(frame);
                av_frame_unref(frame);
            }
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
            {
                streamError("video decoding failed: " + averror(result));
                ok = false;
            }
            if (end)
                break;
        }
        {
            std::lock_guard lock(framesMutex_);
            videoEnded_ = true;
        }
        av_frame_free(&frame);
    }

    // Converts a decoded picture into a free RGBA frame and queues it.
    bool present(const AVFrame *picture)
    {
        Frame target;
        {
            std::unique_lock lock(framesMutex_);
            framesSpace_.wait(lock, [&] { return stop_ || ready_.size() < kQueuedFrames; });
            if (stop_)
                return false;
            if (!spare_.empty())
            {
                target = std::move(spare_.back());
                spare_.pop_back();
            }
        }
        sws_ = sws_getCachedContext(sws_, picture->width, picture->height, static_cast<AVPixelFormat>(picture->format),
                                    info_.outputWidth, info_.outputHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                    nullptr, nullptr);
        if (!sws_)
        {
            streamError(std::string("no conversion from ") +
                        av_get_pix_fmt_name(static_cast<AVPixelFormat>(picture->format)) + " to RGBA");
            return false;
        }
        const int matrix = picture->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_ITU601;
        const int fullRange = picture->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        sws_setColorspaceDetails(sws_, sws_getCoefficients(matrix), fullRange, sws_getCoefficients(SWS_CS_DEFAULT), 1,
                                 0, 1 << 16, 1 << 16);
        target.width = info_.outputWidth;
        target.height = info_.outputHeight;
        target.rgba.resize(static_cast<size_t>(target.width) * target.height * 4);
        uint8_t *planes[4] = {target.rgba.data(), nullptr, nullptr, nullptr};
        int strides[4] = {target.width * 4, 0, 0, 0};
        sws_scale(sws_, picture->data, picture->linesize, 0, picture->height, planes, strides);
        double pts = seconds(picture->best_effort_timestamp, videoBase_);
        if (pts < 0)
            pts = lastPts_ + frameDuration_;
        target.pts = pts;
        std::lock_guard lock(framesMutex_);
        lastPts_ = std::max(lastPts_.load(), pts);
        ready_.push_back(std::move(target));
        return true;
    }

    void streamError(const std::string &message)
    {
        log(LogLevel::Error, name_ + ": " + message);
        failed_ = true;
    }

    // Audio clock while the sound plays; afterwards (or without sound) the wall clock continues
    // from where it stopped. Paused: frozen.
    double advanceClock()
    {
        if (paused_)
            return clock_ = pausedClock_;
        const auto now = Clock::now();
        if (audioActive_)
        {
            const uint64_t played = audio_->playedFrames();
            const double t = audioStart_ + static_cast<double>(played) / kAudioRate;
            if (audioEnded_ && played >= written_)
            {
                audioDrained_ = true;
                audioActive_ = false;
                wallClock_ = t;
                wallAt_ = now;
            }
            return clock_ = t;
        }
        return clock_ = wallClock_ + std::chrono::duration<double>(now - wallAt_).count();
    }

    static constexpr size_t kQueuedFrames = 3;

    std::unique_ptr<Source> source_;
    std::string name_;
    std::unique_ptr<AudioSink> audio_;
    StreamInfo info_;
    AVIOContext *io_ = nullptr;
    AVFormatContext *format_ = nullptr;
    CodecContext videoCodec_, audioCodec_;
    SwsContext *sws_ = nullptr;
    SwrContext *swr_ = nullptr;
    int videoIndex_ = -1, audioIndex_ = -1;
    AVRational videoBase_{}, audioBase_{};
    double startTime_ = 0.0, frameDuration_ = 1.0 / 25;
    std::vector<float> pcm_;
    PacketQueue packets_;
    std::thread demuxThread_, videoThread_;
    std::atomic<bool> stop_{false}, failed_{false}, audioEnded_{false}, audioStarted_{false};
    std::atomic<double> audioStart_{0.0}, lastPts_{0.0};
    std::atomic<uint64_t> written_{0};

    std::mutex framesMutex_;
    std::condition_variable framesSpace_;
    std::deque<Frame> ready_;
    std::vector<Frame> spare_;
    bool videoEnded_ = false; // guarded by framesMutex_

    // Game thread only.
    Frame current_;
    bool shown_ = false, finished_ = false, paused_ = false, audioActive_ = false, audioDrained_ = false;
    double clock_ = 0.0, pausedClock_ = 0.0, wallClock_ = 0.0;
    Clock::time_point wallAt_;
};
} // namespace

void setLogSink(LogSink sink)
{
    logSink = sink;
}

std::unique_ptr<Source> openFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        log(LogLevel::Error, "cannot open " + path.string());
        return nullptr;
    }
    return std::make_unique<FileSource>(std::move(file));
}

std::unique_ptr<Player> Player::open(std::unique_ptr<Source> source, const std::string &name,
                                     std::unique_ptr<AudioSink> audio, const Options &options)
{
    initialiseOnce();
    if (!source)
        return nullptr;
    auto player = std::make_unique<PlayerImpl>(std::move(source), name, std::move(audio));
    if (!player->open(options))
        return nullptr;
    return player;
}

} // namespace storm::video
