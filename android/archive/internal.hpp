#pragma once
#include "storm_archive.h"
#include <chrono>
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
struct Context {
 Fd root;
 storm_archive_limits limits;
 storm_archive_progress callback;
 void *userdata;
 uint64_t total=0, entries=0, extracted=0;
 bool resources=false, programs=false, engine=false;
 std::string prefix;
 bool prefix_set=false;
 std::set<std::string> paths;
 std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
 Context(const char *output, const storm_archive_limits*, storm_archive_progress, void*);
 void check(const std::string &path="");
 void account(uint64_t n, const std::string &path);
 std::string select(const std::string &path);
 int create(const std::string &path);
 void write(int fd, const char *data, size_t size);
 void finish();
};
struct Manifest {
 std::string format, version;
 std::vector<std::pair<std::string,uint64_t>> files;
 std::vector<std::string> volumes;
 uint64_t unpacked_bytes=0;
 bool has_resources=false,has_program=false,has_engine_ini=false;
 void add(const std::string &path,uint64_t size);
 std::string json() const;
};
Manifest inno_manifest(const std::string &input);
Manifest archive_manifest(const std::string &input);
bool inno_probe(const std::string &input);
void inno_extract(const std::string &input, Context &ctx);
void archive_extract(const std::string &input, Context &ctx);
std::string archive_probe(const std::string &input);
std::string checked_input(const char *input);
}
