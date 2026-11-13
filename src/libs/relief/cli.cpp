// Host tool: prepares the relief maps of a game copy (what the game makes lazily while it runs, at once:
// host renders with every map in place), or the game's ×2 textures (fx/upscale.hpp) of a list of texture files.
//   storm_relief_cli <game RESOURCE/Textures> <relief dir> <model dir (the build's, src/libs/relief/model/README.md)> [map size, default fx::kReliefMapSize] [threads] [upscale dir] [bc 0|1]
//   storm_relief_cli --x2-list <game RESOURCE/Textures>   (the textures that get a ×2: path, target, width, height)
//   storm_relief_cli --x2 <game RESOURCE/Textures> <upscale dir> <model dir> <bc 0|1> <threads> <list of texture paths, one per line, relative to the textures>
#include "relief.hpp"

#include <algorithm>
#include "storm/fx/relief_store.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    using namespace storm::relief;
    if (argc == 3 && std::strcmp(argv[1], "--x2-list") == 0)
    {
        // The textures that get a ×2, with their class and size (tab-separated): the list for --x2.
        for (const UpscaleCandidate &c : UpscaleCandidates(argv[2]))
            std::printf("%s\t%s\t%d\t%d\n", std::filesystem::relative(c.file, argv[2]).generic_string().c_str(),
                        storm::fx::UpscaleTargetName(c.target), c.width, c.height);
        return 0;
    }
    const bool x2 = argc > 1 && std::strcmp(argv[1], "--x2") == 0;
    if (argc < 4 || (x2 && argc < 8))
    {
        std::fprintf(stderr,
                     "usage: %s <textures> <relief dir> <model dir> [map size] [threads] [upscale dir] [bc 0|1]\n"
                     "       %s --x2 <textures> <upscale dir> <model dir> <bc 0|1> <threads> <list>\n",
                     argv[0], argv[0]);
        return 2;
    }
    const char *model_dir = argv[x2 ? 4 : 3];
    const auto read = [&](const char *name) {
        std::ifstream in(std::string(model_dir) + "/" + name, std::ios::binary);
        return std::vector<uint8_t>{std::istreambuf_iterator<char>(in), {}};
    };
    std::vector<std::vector<uint8_t>> files;
    for (const char *name : kModelFiles)
        files.push_back(read(name));
    if (std::any_of(files.begin(), files.end(), [](const auto &bytes) { return bytes.empty(); }))
    {
        std::fprintf(stderr, "cannot read the models in %s\n", model_dir);
        return 2;
    }
    if (x2)
    {
        std::string error;
        const Models models{files[0], files[1], files[2], files[3], files[4],  files[5],
                            files[6], files[7], files[8], files[9], files[10], files[11]};
        // The device measured as the game measures it, kept beside the ×2.
        std::unique_ptr<Session> session = Session::Open(models, std::atoi(argv[6]), argv[3], error);
        if (!session)
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
        const UpscaleOutput display{argv[3], std::atoi(argv[5]) != 0};
        std::ifstream list(argv[7]);
        int counts[4] = {};
        const auto start = std::chrono::steady_clock::now();
        for (std::string line; std::getline(list, line);)
        {
            line = line.substr(0, line.find('\t')); // a --x2-list line: its path
            if (line.empty())
                continue;
            std::string failure;
            const Outcome outcome = session->Upscale(argv[2], std::filesystem::path(argv[2]) / line, display, 0, failure);
            ++counts[static_cast<int>(outcome)];
            if (outcome == Outcome::Failed)
                std::fprintf(stderr, "%s: %s\n", line.c_str(), failure.c_str());
        }
        std::printf("{\"prepared\": %d, \"existing\": %d, \"skipped\": %d, \"failed\": %d, \"seconds\": %.1f, \"peak_rss_mb\": %lld}\n",
                    counts[0], counts[1], counts[2], counts[3],
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), PeakRssKb() / 1024);
        return counts[3] ? 1 : 0;
    }
    Job job;
    job.textures = argv[1];
    job.output = argv[2];
    job.models = {files[0], files[1], files[2], files[3], files[4],  files[5],
                  files[6], files[7], files[8], files[9], files[10], files[11]};
    job.map_size = argc > 4 ? std::atoi(argv[4]) : storm::fx::kReliefMapSize;
    job.threads = argc > 5 ? std::atoi(argv[5]) : 0;
    if (argc > 6)
        job.display = UpscaleOutput{argv[6], argc > 7 && std::atoi(argv[7]) != 0};
    const Result result = Prepare(job, nullptr);
    std::printf("%s\n", result.Json().c_str());
    return result.status;
}
