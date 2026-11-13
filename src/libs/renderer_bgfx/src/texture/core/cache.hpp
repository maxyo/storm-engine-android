// Transcode of a whole D3D payload into a KTX 1.1 container (written by bimg; its mip chain rebuilt from the top level,
// mips.hpp) and the on-disk cache of those containers.
//
// Cache file: FileHeader followed by the KTX blob. The key covers the D3D
// payload contents, its layout, the tier and kEncoderVersion; a file whose header, size
// or checksum does not match is treated as a miss and deleted.
#pragma once

#include "formats.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace storm::texture
{

// Bump when the decoder, encoder settings or the container layout change.
inline constexpr uint32_t kEncoderVersion = 4; // 2: BC decoding by bimg, 3: rebuilt mip chains, BC tier, 4: cut-out
                                               // levels keep failing texels below the reference

uint64_t hash64(const void *data, size_t size, uint64_t seed);

// Accumulates the cache key over the D3D payload (face-major, level-minor,
// the order the shim keeps its sub-resources in). `bc`: the BC tier (else ETC2).
class KeyBuilder
{
  public:
    KeyBuilder(const Layout &layout, bool bc);
    void add(const uint8_t *data, size_t size);
    uint64_t key() const { return state_; }

  private:
    uint64_t state_;
};

struct Blob
{
    std::unique_ptr<uint8_t[]> data;
    size_t size = 0;
};

// The layout of what transcode_to_ktx makes of a payload of `layout`: the complete chain down to 1x1.
Layout output_layout(const Layout &layout);
// Size of the KTX container for the output layout `out` in `format`.
size_t ktx_size(GpuFormat format, const Layout &out);
// Transcodes the D3D payload (face-major, as KeyBuilder) into a KTX container of output_layout(layout) (mip
// level-major as KTX requires): per face the top level is decoded, its chain rebuilt (mips.hpp) and every level
// encoded in `format`; the BC tier keeps the game's top level as it is. `payload` must hold dxt_payload_size() bytes;
// only the top levels are read.
Blob transcode_to_ktx(const Layout &layout, const uint8_t *payload, GpuFormat format);

struct FileHeader
{
    char magic[8];
    uint32_t version;
    uint32_t gpu_format; // GpuFormat
    uint32_t width, height, levels, faces;
    uint64_t key;
    uint64_t payload_size;
    uint64_t checksum;
};
static_assert(sizeof(FileHeader) == 56);

std::filesystem::path cache_file_path(const std::filesystem::path &directory, uint64_t key);
// `out`: the output layout (output_layout()).
FileHeader make_header(uint64_t key, const Layout &out, GpuFormat format, const Blob &ktx);
// Validates a header against what the caller expects (`out`: the output layout); does not look at the payload.
bool header_valid(const FileHeader &header, uint64_t key, const Layout &out);

// Writes atomically (temporary file + rename). Returns bytes written or 0. `out`: the output layout.
uint64_t write_cache_file(const std::filesystem::path &directory, uint64_t key, const Layout &out, GpuFormat format,
                          const Blob &ktx);
// Returns the KTX blob or nothing; a mismatching or damaged file is deleted. `out`: the output layout.
std::optional<Blob> read_cache_file(const std::filesystem::path &directory, uint64_t key, const Layout &out);

// Other data derived from a texture (normal maps of height textures), kept in the same directory under the same
// budget and LRU eviction. `magic` names the kind, `version` its format; both must match on reading.
struct DerivedShape
{
    char magic[8];
    uint32_t version = 0;
    uint32_t width = 0, height = 0, levels = 0;
};
uint64_t write_derived_file(const std::filesystem::path &directory, uint64_t key, const DerivedShape &shape,
                            const uint8_t *data, size_t size);
// The payload or nothing; a mismatching, oversized or damaged file is deleted.
std::optional<Blob> read_derived_file(const std::filesystem::path &directory, uint64_t key, const DerivedShape &shape,
                                      size_t max_size);

// Total size of cache files; removes stale temporary files.
uint64_t scan_cache(const std::filesystem::path &directory);
// Deletes least recently used files (by mtime) until the total is below
// `target_bytes`. Returns the new total.
uint64_t evict_cache(const std::filesystem::path &directory, uint64_t target_bytes);

} // namespace storm::texture
