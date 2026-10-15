// stb must precede miniaudio to enable its Vorbis decoding backend.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

// miniaudio's generic Vorbis callbacks cannot report duration. File-backed game
// music is seekable; use stb's file index, not a full PCM allocation, for length.
extern "C" ma_uint64 storm_vorbis_frames(const char* path, unsigned int* rate) {
    int error = 0;
    stb_vorbis* decoder = stb_vorbis_open_filename(path, &error, nullptr);
    if (!decoder) return 0;
    *rate = stb_vorbis_get_info(decoder).sample_rate;
    ma_uint64 frames = stb_vorbis_stream_length_in_samples(decoder);
    stb_vorbis_close(decoder);
    frames = ma_calculate_frame_count_after_resampling(48000, *rate, frames);
    *rate = 48000;
    return frames;
}
