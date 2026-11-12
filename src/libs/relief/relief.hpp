// Relief preparation ("Подготовка рельефа"): normal maps for the game's stone, wood, building, plaster and ground
// textures, made once from the player's game files with DeepBump (fx/deepbump.hpp) through onnxruntime, and stored in
// the relief directory (fx/relief_store.hpp) that the game reads. Runs at game import (Android launcher:
// NativeRelief.prepare, android/README in this folder) and on the host (storm_relief_cli). Contract: README.md here.
#pragma once

#include "storm/fx/relief_store.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

namespace storm::relief
{

struct Job
{
    std::filesystem::path textures; // the game's RESOURCE/Textures
    std::filesystem::path output;   // the relief directory
    std::span<const uint8_t> model; // deepbump256.onnx
    int map_size = fx::kReliefMapSize; // larger side of the maps (the game reads maps of that size)
    int threads = 0;                // inference threads; 0 = every core
};

struct Progress
{
    int done = 0;  // textures handled (prepared, already there, or failed)
    int total = 0; // relief textures found
    std::string current;
};

struct Result
{
    int status = 0; // 0 complete, 1 cancelled, 2 error (`error` says why); failed textures alone do not make an error
    std::string error;
    int total = 0, prepared = 0, existing = 0, failed = 0, skipped = 0;
    int removed = 0; // stale maps deleted after a complete run
    long long tiles = 0;
    double seconds = 0.0, infer_seconds = 0.0;
    double max_texture_ms = 0.0;
    long long peak_rss_kb = 0;
    std::string provider; // the onnxruntime execution provider that ran the model
    std::string Json() const;
};

// `progress` is called on the calling thread after each texture (and once with done = 0 when the list is known);
// returning true cancels. Synchronous: call it from a worker thread. Resumable: textures whose map exists are skipped.
Result Prepare(const Job &job, const std::function<bool(const Progress &)> &progress);

// The relief textures of a game and how many of them have no map yet (for the launcher: whether to offer the step).
struct Status
{
    int total = 0, missing = 0;
};
Status Check(const std::filesystem::path &textures, const std::filesystem::path &output, int map_size);

// Logging of the library: Android log tag "storm-relief", stderr elsewhere.
void Log(const char *format, ...);

} // namespace storm::relief
