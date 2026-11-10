// Transcode of a whole D3D payload into a KTX 1.1 container and the on-disk
// cache of those containers.
//
// Cache file: FileHeader followed by the KTX blob. The key covers the D3D
// payload contents, its layout and kEncoderVersion; a file whose header, size
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
inline constexpr uint32_t kEncoderVersion = 1;

uint64_t hash64(const void *data, size_t size, uint64_t seed);

// Accumulates the cache key over the D3D payload (face-major, level-minor,
// the order the shim keeps its sub-resources in).
class KeyBuilder
{
  public:
    explicit KeyBuilder(const Layout &layout);
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

// Size of the KTX container produced for `layout` in `format`.
size_t ktx_size(EtcFormat format, const Layout &layout);
// Transcodes the D3D payload (face-major, as KeyBuilder) into a KTX container
// (mip level-major as KTX requires). `payload` must hold dxt_payload_size() bytes.
Blob transcode_to_ktx(const Layout &layout, const uint8_t *payload, EtcFormat format);

struct FileHeader
{
    char magic[8];
    uint32_t version;
    uint32_t etc_format;
    uint32_t width, height, levels, faces;
    uint64_t key;
    uint64_t payload_size;
    uint64_t checksum;
};
static_assert(sizeof(FileHeader) == 56);

std::filesystem::path cache_file_path(const std::filesystem::path &directory, uint64_t key);
FileHeader make_header(uint64_t key, const Layout &layout, EtcFormat format, const Blob &ktx);
// Validates a header against what the caller expects; does not look at the payload.
bool header_valid(const FileHeader &header, uint64_t key, const Layout &layout);

// Writes atomically (temporary file + rename). Returns bytes written or 0.
uint64_t write_cache_file(const std::filesystem::path &directory, uint64_t key, const Layout &layout,
                          EtcFormat format, const Blob &ktx);
// Returns the KTX blob or nothing; a mismatching or damaged file is deleted.
std::optional<Blob> read_cache_file(const std::filesystem::path &directory, uint64_t key, const Layout &layout);

// Total size of cache files; removes stale temporary files.
uint64_t scan_cache(const std::filesystem::path &directory);
// Deletes least recently used files (by mtime) until the total is below
// `target_bytes`. Returns the new total.
uint64_t evict_cache(const std::filesystem::path &directory, uint64_t target_bytes);

} // namespace storm::texture
