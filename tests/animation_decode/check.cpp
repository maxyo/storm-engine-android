#include <algorithm>
#include <array>
#include <cfenv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "math_inlines.h"
#include "math3d/quaternion.h"

struct PackedQuaternion
{
    short x, y, z, w;
};
static_assert(sizeof(PackedQuaternion) == 8);

// Only the track fields used by production GetFrame/BlendFrame are needed.
struct Track
{
    std::vector<PackedQuaternion> ang;
    int32_t numFrames;
    void GetFrame(int32_t frame, Quaternion &out);
    void baseline(int32_t frame, float blend, Quaternion &out);
    void candidate(int32_t frame, float blend, Quaternion &out);
};
#include "functions.inc"

constexpr std::array roundingModes{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
constexpr std::array coefficients{-0.25f, -0.0f, 0.0f, 0.375f, 1.0f, 1.25f};

void compare(Track &track, int frame, float blend)
{
    Quaternion before(1, 2, 3, 4), after = before;
    feclearexcept(FE_ALL_EXCEPT);
    track.baseline(frame, blend, before);
    const auto flags = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT);
    track.candidate(frame, blend, after);
    if (memcmp(&before, &after, sizeof(before)) || flags != fetestexcept(FE_ALL_EXCEPT))
        throw std::runtime_error("quaternion or FP flags differ at frame " + std::to_string(frame));
    Matrix beforeMatrix, afterMatrix;
    before.GetMatrix(beforeMatrix);
    after.GetMatrix(afterMatrix);
    if (memcmp(&beforeMatrix, &afterMatrix, sizeof(beforeMatrix)))
        throw std::runtime_error("local matrix differs");
}

void synthetic()
{
    uint64_t checked = 0;
    for (int mode : roundingModes)
    {
        fesetround(mode);
        for (int value = -32768; value <= 32767; ++value)
        {
            // Every compressed scalar value, equal and unequal endpoints,
            // terminal frames, and clamped/interior blend coefficients.
            const auto v = static_cast<short>(value);
            const auto other = static_cast<short>(~value);
            Track track{{{v, other, 0, 32767}, {v, other, 0, 32767}, {0, v, -32768, 1}}, 3};
            for (int frame = 0; frame <= 3; ++frame)
                for (float blend : coefficients)
                {
                    compare(track, frame, blend);
                    ++checked;
                }
        }
    }
    Track empty{{}, 0};
    compare(empty, 0, 0.5f);
    fesetround(FE_TONEAREST);
    std::cout << "SYNTHETIC PASS " << checked << " comparisons\n";
}

std::vector<Track> load(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::badbit | std::ios::failbit);
    int32_t frames, joints;
    float fps;
    input.read(reinterpret_cast<char *>(&frames), 4);
    input.read(reinterpret_cast<char *>(&joints), 4);
    input.read(reinterpret_cast<char *>(&fps), 4);
    if (frames <= 0 || joints <= 0 || joints > 256 ||
        std::filesystem::file_size(path) != 12ull + joints * 16ull + frames * 12ull + frames * 16ull * joints)
        throw std::runtime_error("invalid animation: " + path.string());
    input.seekg(12ull + joints * 16ull + frames * 12ull);
    std::vector<Track> result(joints);
    for (auto &track : result)
    {
        track.numFrames = frames;
        track.ang.resize(frames);
        for (auto &packed : track.ang)
        {
            std::array<float, 4> source;
            std::array<short, 4> compressed;
            input.read(reinterpret_cast<char *>(source.data()), sizeof(source));
            for (size_t i = 0; i < source.size(); ++i)
            {
                // Same quantization as Bone::SetAngles. Loading occurs under
                // FE_TONEAREST before decoding is tested under all four modes.
                float value = std::clamp(source[i], -1.0f, 1.0f);
                value = static_cast<float>(asin(value) / (PI * 0.5)) * 32767.0f;
                compressed[i] = static_cast<short>(static_cast<int32_t>(value));
            }
            packed = {compressed[0], compressed[1], compressed[2], compressed[3]};
        }
    }
    return result;
}

void benchmark(std::vector<Track> &tracks, uint64_t pairs)
{
    if (!pairs)
        return;
    const auto repeats = std::max<uint64_t>(3, 2000000 / pairs);
    volatile float checksum = 0;
    for (int run = 0; run < 8; ++run)
    {
        const bool candidate = run % 4 == 1 || run % 4 == 2;
        const auto begin = std::chrono::steady_clock::now();
        for (uint64_t repeat = 0; repeat < repeats; ++repeat)
            for (auto &track : tracks)
                for (int frame = 0; frame < track.numFrames - 1; ++frame)
                {
                    Quaternion result;
                    if (candidate)
                        track.candidate(frame, .375f, result);
                    else
                        track.baseline(frame, .375f, result);
                    checksum = checksum + result.x;
                }
        const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        std::cout << "BENCH " << run << " " << (candidate ? "B" : "A") << " " << ns / (repeats * pairs)
                  << " ns_per_blend " << checksum << "\n";
    }
}

int main(int argc, char **argv)
{
    std::cout.precision(12);
    if (argc == 1)
    {
        synthetic();
        return 0;
    }
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(argv[1]))
    {
        for (const auto &entry : std::filesystem::directory_iterator(argv[1]))
            if (entry.path().extension() == ".an")
                files.push_back(entry.path());
    }
    else
        files.emplace_back(argv[1]);
    std::sort(files.begin(), files.end());
    uint64_t all = 0, equal = 0, checked = 0;
    const std::string mode = argc > 2 ? argv[2] : "diff";
    for (const auto &path : files)
    {
        auto tracks = load(path);
        uint64_t count = 0, hits = 0;
        for (const auto &track : tracks)
            for (int frame = 0; frame < track.numFrames - 1; ++frame)
            {
                ++count;
                hits += !memcmp(&track.ang[frame], &track.ang[frame + 1], sizeof(PackedQuaternion));
            }
        all += count;
        equal += hits;
        std::cout << "STATS " << path.filename() << " " << count << " " << hits << " "
                  << (count ? double(hits) / count : 0) << std::endl;
        if (mode == "diff")
        {
            for (int rounding : roundingModes)
            {
                fesetround(rounding);
                for (auto &track : tracks)
                    for (int frame = 0; frame < track.numFrames; ++frame)
                        for (float blend : coefficients)
                        {
                            compare(track, frame, blend);
                            ++checked;
                        }
            }
            fesetround(FE_TONEAREST);
        }
        if (mode == "bench")
            benchmark(tracks, count);
    }
    std::cout << "TOTAL " << files.size() << " " << all << " " << equal << " "
              << (all ? double(equal) / all : 0) << " checked " << checked << "\n";
}
