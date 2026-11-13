// Host tool: prepares the relief maps of a game copy (what the game makes lazily while it runs, at once:
// host renders with every map in place).
//   storm_relief_cli <game RESOURCE/Textures> <relief dir> <model dir (src/libs/relief/model)> [map size, default fx::kReliefMapSize] [threads]
#include "relief.hpp"
#include "storm/fx/relief_store.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    using namespace storm::relief;
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <textures> <relief dir> <model dir> [map size] [threads]\n", argv[0]);
        return 2;
    }
    const auto read = [&](const char *name) {
        std::ifstream in(std::string(argv[3]) + "/" + name, std::ios::binary);
        return std::vector<uint8_t>{std::istreambuf_iterator<char>(in), {}};
    };
    const std::vector<uint8_t> normal = read("pbrify-normal-int8.onnx"), height = read("pbrify-height-int8.onnx"),
                               roughness = read("pbrify-roughness-int8.onnx");
    if (normal.empty() || height.empty() || roughness.empty())
    {
        std::fprintf(stderr, "cannot read the models in %s\n", argv[3]);
        return 2;
    }
    Job job;
    job.textures = argv[1];
    job.output = argv[2];
    job.models = {normal, height, roughness};
    job.map_size = argc > 4 ? std::atoi(argv[4]) : storm::fx::kReliefMapSize;
    job.threads = argc > 5 ? std::atoi(argv[5]) : 0;
    const Result result = Prepare(job, nullptr);
    std::printf("%s\n", result.Json().c_str());
    return result.status;
}
