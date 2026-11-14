// Host tool: prepares the relief maps of a game copy (what the game makes lazily while it runs, at once:
// host renders with every map in place).
//   storm_relief_cli <game RESOURCE/Textures> <relief dir> <model dir (the build's, src/libs/relief/model/README.md)> [map size, default fx::kReliefMapSize] [threads]
#include "relief.hpp"

#include <algorithm>
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
    std::vector<std::vector<uint8_t>> files;
    for (const char *name : kModelFiles)
        files.push_back(read(name));
    if (std::any_of(files.begin(), files.end(), [](const auto &bytes) { return bytes.empty(); }))
    {
        std::fprintf(stderr, "cannot read the models in %s\n", argv[3]);
        return 2;
    }
    Job job;
    job.textures = argv[1];
    job.output = argv[2];
    job.models = {files[0], files[1], files[2], files[3], files[4], files[5], files[6], files[7], files[8]};
    job.map_size = argc > 4 ? std::atoi(argv[4]) : storm::fx::kReliefMapSize;
    job.threads = argc > 5 ? std::atoi(argv[5]) : 0;
    const Result result = Prepare(job, nullptr);
    std::printf("%s\n", result.Json().c_str());
    return result.status;
}
