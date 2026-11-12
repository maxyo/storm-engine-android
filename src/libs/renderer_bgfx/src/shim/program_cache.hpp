// The renderer's bgfx::CallbackI (storm/render/program_cache.hpp): program binaries in files, plus bgfx's
// fatal errors and traces into the engine log.
//
// Layout: <directory>/<device key>/<bgfx id>.bin. The device key hashes GL_VENDOR, GL_RENDERER, GL_VERSION (the
// driver version on Android) and BGFX_API_VERSION; bgfx's id already hashes both shaders. A file carries the key,
// the id, the size and a checksum of the binary: anything that does not match is logged, removed and reported as
// missing, and bgfx links the program (patches/bgfx-program-binary-cache.patch: also when the driver rejects a
// binary, which programCache() then replaces). Writes go to a temporary file renamed into place.
#pragma once

#include "storm/render/program_cache.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace storm::shim
{

class ProgramCache final : public bgfx::CallbackI
{
  public:
    static ProgramCache &instance();

    void configure(const render::ProgramCacheConfig &config);
    // After bgfx::init on a real backend (current GL context): opens the directory of this device; the binaries of
    // other devices or drivers are removed.
    void attach_device(const std::string &device);
    void detach_device();
    render::ProgramCacheStats stats();
    // Logs what changed since the last call (once per presented frame that loaded or stored programs).
    void log_activity();

    // bgfx::CallbackI
    void fatal(const char *file, uint16_t line, bgfx::Fatal::Enum code, const char *str) override;
    void traceVargs(const char *file, uint16_t line, const char *format, va_list args) override;
    // bgfx's own scopes (bgfx built with BGFX_CONFIG_PROFILER in Tracy builds) as Tracy zones; nothing otherwise.
    void profilerBegin(const char *name, uint32_t abgr, const char *file, uint16_t line) override;
    void profilerBeginLiteral(const char *name, uint32_t abgr, const char *file, uint16_t line) override;
    void profilerEnd() override;
    uint32_t cacheReadSize(uint64_t id) override;
    bool cacheRead(uint64_t id, void *data, uint32_t size) override;
    void cacheWrite(uint64_t id, const void *data, uint32_t size) override;
    // The renderer neither requests screenshots nor captures frames: a call is a bug, logged.
    void screenShot(const char *, uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, const void *, uint32_t,
                    bool) override;
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override;
    void captureEnd() override {}
    void captureFrame(const void *, uint32_t) override {}

  private:
    struct Entry
    {
        uint64_t bytes = 0;
        std::filesystem::file_time_type used{};
    };
    std::filesystem::path file_of(uint64_t id) const;
    void forget(uint64_t id, const char *why);
    void evict();

    std::mutex mutex_;
    render::ProgramCacheConfig config_{};
    bool configured_ = false;
    bool unconfigured_logged_ = false;
    std::filesystem::path device_dir_; // empty: no device attached, nothing is read or written
    uint64_t key_ = 0;
    std::map<uint64_t, Entry> entries_;
    uint64_t total_bytes_ = 0;
    // The binary read by cacheReadSize, handed out by the cacheRead that follows.
    uint64_t pending_id_ = 0;
    std::vector<uint8_t> pending_;
    std::set<uint64_t> served_; // ids loaded this run: a later write of one means the driver rejected it
    render::ProgramCacheStats stats_{};
    render::ProgramCacheStats logged_{};
};

} // namespace storm::shim
