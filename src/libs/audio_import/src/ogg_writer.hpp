#pragma once
// Ogg page output and Vorbis comment packets shared by remux and encode.

#include "reader.hpp"

#include <vorbis/codec.h>

namespace storm::audio_import::detail
{
class OggWriter
{
  public:
    explicit OggWriter(int serial)
    {
        if (ogg_stream_init(&stream_, serial) != 0)
            throw ImportError("ogg_stream_init failed");
    }
    ~OggWriter()
    {
        ogg_stream_clear(&stream_);
    }
    OggWriter(const OggWriter &) = delete;
    OggWriter &operator=(const OggWriter &) = delete;

    void packet(ogg_packet &packet)
    {
        if (ogg_stream_packetin(&stream_, &packet) != 0)
            throw ImportError("ogg_stream_packetin failed");
        drain(false);
    }
    // Ends the current page; Vorbis audio must start on a fresh page.
    void flush()
    {
        drain(true);
    }
    Bytes take()
    {
        flush();
        return std::move(out_);
    }

  private:
    void drain(bool force)
    {
        ogg_page page;
        while (force ? ogg_stream_flush(&stream_, &page) : ogg_stream_pageout(&stream_, &page))
        {
            out_.insert(out_.end(), page.header, page.header + page.header_len);
            out_.insert(out_.end(), page.body, page.body + page.body_len);
        }
    }
    ogg_stream_state stream_{};
    Bytes out_;
};

// Owns a vorbis_comment and its header packet.
class CommentHeader
{
  public:
    CommentHeader()
    {
        vorbis_comment_init(&comment_);
    }
    ~CommentHeader()
    {
        ogg_packet_clear(&packet_);
        vorbis_comment_clear(&comment_);
    }
    CommentHeader(const CommentHeader &) = delete;
    CommentHeader &operator=(const CommentHeader &) = delete;

    void add(const std::string &tag, const std::string &value)
    {
        vorbis_comment_add_tag(&comment_, tag.c_str(), value.c_str());
    }
    vorbis_comment &comment()
    {
        return comment_;
    }
    ogg_packet &packet()
    {
        ogg_packet_clear(&packet_);
        if (vorbis_commentheader_out(&comment_, &packet_) != 0)
            throw ImportError("vorbis_commentheader_out failed");
        return packet_;
    }

  private:
    vorbis_comment comment_{};
    ogg_packet packet_{};
};
} // namespace storm::audio_import::detail
