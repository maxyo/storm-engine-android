#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace storm::torrent {
// All calls except destruction may throw std::runtime_error. Caller serializes calls.
// Metadata describes candidates only; downloaded content still needs the game validator.
std::string inspect_file(std::string const& path);
class Session {
public:
    // source is a local .torrent filename or magnet URI. staging must be an app-private,
    // dedicated directory. Opening a magnet retrieves metadata only, never payload.
    Session(std::string source, std::string staging, bool local_test_only = false);
    ~Session();
    std::string poll(); // JSON; call periodically from a foreground service worker
    void start(std::vector<int> const& selected_files);
    void pause(); // pauses and requests an atomic resume checkpoint
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
