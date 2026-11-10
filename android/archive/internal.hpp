#pragma once
#include "storm_archive.h"
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <set>
#include <cstdint>
#include <vector>
#include <utility>
namespace storm_archive {
bool allocation_failed();
void clear_allocation_failure();
struct Failure : std::runtime_error {
 int status;
 Failure(int code, const std::string &message): std::runtime_error(message), status(code) {}
};
struct Fd {
 int value;
 explicit Fd(int fd=-1): value(fd) {}
 ~Fd();
 Fd(const Fd&)=delete; Fd& operator=(const Fd&)=delete;
};
// Validated relative member path split into components (no traversal, control
// characters, drive/ADS colons, empty components or Windows trailing dots).
std::vector<std::string> path_parts(const std::string &raw);
std::string lower_path(std::string s);
std::string join_parts(const std::vector<std::string> &parts, size_t count);
// Game-root anchors: RESOURCE/ or PROGRAM/ directory, engine.ini or options leaf.
enum Anchor { NO_ANCHOR, RESOURCE_DIR, PROGRAM_DIR, ENGINE_INI, OPTIONS_FILE };
Anchor anchor_at(const std::vector<std::string> &parts, size_t index);
// Output path inside the staging root for a member anchored at index, or empty
// for files that are never written (executables, libraries, scripts).
std::string root_output(const std::vector<std::string> &parts, size_t index);
// Free bytes for an unprivileged writer on the filesystem holding fd.
uint64_t free_bytes(int fd);
struct Context {
 Fd root;
 storm_archive_limits limits;
 storm_archive_progress callback;
 void *userdata;
 // Source-set API: staged progress, bomb guard, free-space reserve and an
 // explicitly selected game root. Zero/empty keeps the legacy behavior.
 storm_archive_stage_progress staged=nullptr;
 int stage=0;
 uint64_t stage_total=0;
 uint64_t max_ratio=0, container_input=0, container_decoded=0;
 uint64_t reserve=0, written_since_space_check=0;
 bool root_selected=false;
 std::string selected_root;
 std::string password;
 uint64_t total=0, entries=0, extracted=0;
 bool resources=false, programs=false, engine=false;
 std::string prefix;
 bool prefix_set=false;
 std::set<std::string> paths;
 std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
 Context(const char *output, const storm_archive_limits*, storm_archive_progress, void*);
 void check(const std::string &path="");
 void account(uint64_t n, const std::string &path);
 void begin_container(uint64_t input_bytes) { container_input=input_bytes; container_decoded=0; }
 std::string select(const std::string &path);
 int create(const std::string &path);
 void write(int fd, const char *data, size_t size);
 void finish();
};
struct Manifest {
 std::string format, version, title;
 std::vector<std::pair<std::string,uint64_t>> files;
 std::vector<std::string> volumes;
 uint64_t unpacked_bytes=0;
 bool has_resources=false,has_program=false,has_engine_ini=false;
 void add(const std::string &path,uint64_t size);
 std::string json() const;
};
std::string json_quote(const std::string &s);
Manifest inno_manifest(const std::string &input);
// All literal {app} user files (normalized, not root-mapped), header title and
// version, and the sibling volume names the payload needs. Metadata only.
Manifest inno_listing(const std::string &input);
bool inno_probe(const std::string &input);
void inno_extract(const std::string &input, Context &ctx);
// Every volume of one logical archive in order (one entry for a plain file).
void archive_extract(const std::vector<std::string> &volumes, Context &ctx);
// Sequential walk without writing output. entry() returns true to receive the
// member's data through chunk(); chunk() returns false to skip the rest.
struct ArchiveVisitor {
 virtual ~ArchiveVisitor()=default;
 virtual bool entry(const std::string &path, uint64_t size)=0;
 virtual bool chunk(const char *data, size_t size)=0;
 virtual void done()=0;
};
std::string archive_walk(const std::vector<std::string> &volumes, Context &ctx, ArchiveVisitor &visitor);
Manifest archive_manifest(const std::string &input);
std::string archive_probe(const std::string &input);
bool container_signature(const char *head, size_t size);
bool inno_volume_signature(const char *head, size_t size);
std::string checked_input(const char *input);
}
