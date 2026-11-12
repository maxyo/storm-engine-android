// Host tool: prepares the relief maps of a game copy (the same work as the Android import step).
//   storm_relief_cli <game RESOURCE/Textures> <relief dir> <deepbump256.onnx> [map size, default fx::kReliefMapSize] [threads]
//   storm_relief_cli --check <game RESOURCE/Textures> <relief dir> [map size]
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
    if (argc >= 4 && std::string(argv[1]) == "--check")
    {
        const Status status = Check(argv[2], argv[3], argc > 4 ? std::atoi(argv[4]) : storm::fx::kReliefMapSize);
        std::printf("{\"total\":%d,\"missing\":%d}\n", status.total, status.missing);
        return 0;
    }
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <textures> <relief dir> <model.onnx> [map size] [threads]\n"
                             "       %s --check <textures> <relief dir> [map size]\n", argv[0], argv[0]);
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
