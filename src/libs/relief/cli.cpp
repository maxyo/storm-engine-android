// Host tool: prepares the relief maps of a game copy (what the game makes lazily while it runs, at once:
// host renders with every map in place).
//   storm_relief_cli <game RESOURCE/Textures> <relief dir> <deepbump256.onnx> [map size, default fx::kReliefMapSize] [threads]
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
        std::fprintf(stderr, "usage: %s <textures> <relief dir> <model.onnx> [map size] [threads]\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[3], std::ios::binary);
    const std::vector<uint8_t> model{std::istreambuf_iterator<char>(in), {}};
    if (model.empty())
    {
        std::fprintf(stderr, "cannot read %s\n", argv[3]);
        return 2;
    }
    Job job;
    job.textures = argv[1];
    job.output = argv[2];
    job.model = model;
    job.map_size = argc > 4 ? std::atoi(argv[4]) : storm::fx::kReliefMapSize;
    job.threads = argc > 5 ? std::atoi(argv[5]) : 0;
    const Result result = Prepare(job, nullptr);
    std::printf("%s\n", result.Json().c_str());
    return result.status;
}
