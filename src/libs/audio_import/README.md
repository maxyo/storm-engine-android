# audio_import

Converts FMOD Studio banks (`*.bank`, FSB5 Vorbis inside) and WAV files into
standalone Ogg Vorbis for the open audio backend (stb_vorbis + miniaudio), and
reads the bank string table (`*.strings.bank`) as path/GUID pairs for the later
event mapping. No FMOD code, SDK or runtime is used or linked. API:
`include/audio_import/audio_import.hpp`; every malformed or unsupported input
throws `ImportError` (nothing is skipped silently).

## Dependencies

libogg 1.3.6 and libvorbis 1.3.7 (BSD-3-Clause), pinned by commit in
`cmake/xiph.cmake` and fetched with FetchContent like the bgfx pin. Their own
CMake files require CMake < 3.5 semantics, so `cmake/xiph.cmake` compiles the
upstream sources directly (targets `storm_ogg`, `storm_vorbis`; vorbisfile is
not built). Offline/reuse: `-DFETCHCONTENT_SOURCE_DIR_STORM_LIBOGG=<checkout>`
and `-DFETCHCONTENT_SOURCE_DIR_STORM_LIBVORBIS=<checkout>` at those commits.
The library is part of the engine tree (Android arm64 included) and builds
standalone for the host tool and tests. Notices: `third_party/licenses/`
(`libogg-BSD-3-Clause.txt`, `libvorbis-BSD-3-Clause.txt`, `python-fsb5-MIT.txt`).

## Host tool and checks

```sh
cmake -S tools/audio-import -B build/audio-import -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build/audio-import
build/audio-import/storm-audio-import bank <file.bank> <out-dir>      # *.ogg + manifest.json
build/audio-import/storm-audio-import strings <Master.strings.bank> <out.json>
build/audio-import/storm-audio-import wav <in.wav> <out.ogg> [quality]  # default 0.5
build/audio-import/storm-audio-import scan <Banks-dir> <out-dir>      # all banks + string tables

python3 tests/audio_import/run.py   # synthetic FSB5/bank/WAV/string-table checks, decoded with stb_vorbis
python3 tests/audio_import/run.py --game-dir <game root> --out <local dir>  # local only, never commit outputs
```

`manifest.json` lists each sample's file, channels, rate, frame count, FSB5
loop points and status. A sample that fails is reported with its error and the
tool exits non-zero.

## Formats

**Bank.** RIFF form `FEV `. The `SNDH` chunk is an FMOD struct array
(`u16 count<<1|1`, `u16 element size` = 8) of absolute file offset and size of
each FSB5 image (stored in `SND ` chunks after alignment padding). The FSB5 is
located through this table, not by searching for its magic.

**FSB5** (as described by python-fsb5, MIT, Simon Pinfold, and the public
vgmstream notes): 60-byte header (64 for version 0), 64-bit sample headers
(frequency code, channels, data offset/16, frame count) with chained metadata
chunks (1 channels, 2 frequency, 3 loop start/end, 11 Vorbis setup CRC-32 plus
seek table; others are listed in `chunkTypes` and ignored), a name table and
the sample data. Only Vorbis (codec 15) is remuxed; the importer was written
here because no C/C++ library for FSB5 exists that fits the engine
(python-fsb5 is Python, fsbex is Rust, vgmstream is a full player).

**FSB5 Vorbis remux.** Sample data is `u16 size + packet` repeated, ended by
size 0. The three Vorbis header packets are rebuilt (below), a comment header
carries `TITLE`, `LOOPSTART`, `LOOPEND`, and the audio packets are copied
unchanged into Ogg pages with granule positions from
`vorbis_packet_blocksize` (first packet 0, then `+= (previous + current) / 4`).
The last granule is the FSB5 frame count, so decoders trim the encoder padding.

**Vorbis setup headers.** FSB5 keeps only the CRC-32 of the setup packet.
python-fsb5 and vgmstream ship a table of pre-extracted setup packets keyed by
that CRC. This library ships no such table: the setup packets are libvorbis
encoder output, so `VorbisHeaderCache` runs the pinned libvorbis encoder
(`vorbis_encode_init_vbr` + `vorbis_analysis_headerout`, no audio) for the
sample's channel count and rate over the quality range (tenths, then
hundredths) and picks the packet whose CRC matches; the identification packet
comes from the same encoder setting. Both setup CRCs used by Caribbean Legend
banks are reproduced exactly. An unmatched CRC is an error naming it.

**String table** (`STDT` chunk of `*.strings.bank`; layout derived from bank
data, no FMOD code): `u32` version 1; a struct array of 8-byte trie records
(`u24` label offset into the pool or `0xffffff` for an empty label, `u8` first
label char in lower case, `u24` first child record or string index for a leaf,
`u8` child count); a struct array of 16-byte GUIDs (`FMOD_GUID` layout) indexed
by string index; `u16` pool size and the NUL-terminated labels; `u16` count and
the `u24` record of each string index; `u16` count and the `u24` parent of each
record. A path is the concatenation of labels from the root to its leaf. The
parser checks the trie against both index tables.

**WAV.** RIFF/WAVE PCM 8/16/24/32-bit, float 32-bit, and
`WAVE_FORMAT_EXTENSIBLE` with those subformats; encoded with libvorbis VBR.

## Limitations

Only Vorbis FSB5 banks are converted (PCM/MPEG/other codecs raise an error).
FMOD event logic (events, parameters, buses, snapshots) is not interpreted; the
string table only provides names and GUIDs. Strings tables larger than the
`u16` counts allow have not been seen and are rejected by the size checks.
