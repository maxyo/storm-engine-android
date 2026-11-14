// Far LOD game-data check (run.py): every model of the game that gets far LODs (far_lod::Wanted) is built and
// checked (tests/far_lod/verify.hpp), with the triangles kept per level, the encoded size and the build time by folder.
#include "verify.hpp"

namespace
{
using namespace far_lod_check;

// .gm (src/libs/geometry/src/rdf.h): only what the LODs read. The engine refuses files of other versions.
constexpr int32_t kRdfVersion = ('1' << 24) | ('.' << 16) | ('0' << 8) | '5';
struct OtherVersion
{
};

template <class T> T readAt(const std::vector<uint8_t> &file, size_t offset)
{
    if (offset + sizeof(T) > file.size())
        fail("truncated");
    T value;
    std::memcpy(&value, file.data() + offset, sizeof(T));
    return value;
}

struct Loaded
{
    Model model;
    float radius = 0;
    bool animated = false;
};

Loaded loadGm(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto head = [&](int i) { return readAt<int32_t>(file, 4 * size_t(i)); };
    if (head(0) != kRdfVersion)
        throw OtherVersion();
    const int32_t name_size = head(2), names = head(3), textures = head(4), materials = head(5), lights = head(6),
                  labels = head(7), objects = head(8), triangles = head(9), buffers = head(10);
    constexpr size_t kHead = 11 * 4 + 7 * 4, kMaterial = 56, kLight = 80, kLabel = 108, kObject = 104;
    size_t at = kHead + name_size + 4 * size_t(names) + 4 * size_t(textures) + kMaterial * materials + kLight * lights +
                kLabel * labels;
    Loaded loaded;
    std::memcpy(&loaded.radius, file.data() + 17 * 4, sizeof(float)); // RDF_HEAD::radius
    Model &model = loaded.model;
    for (int32_t o = 0; o < objects; ++o, at += kObject)
    {
        MeshObject object;
        object.vertex_buffer = readAt<uint32_t>(file, at + 28);
        object.triangle_count = readAt<uint32_t>(file, at + 32);
        object.first_triangle = readAt<uint32_t>(file, at + 36);
        object.vertex_count = readAt<uint32_t>(file, at + 40);
        object.first_vertex = readAt<uint32_t>(file, at + 44);
        model.objects.push_back(object);
    }
    model.triangles.resize(3 * size_t(triangles));
    if (at + 6 * size_t(triangles) > file.size())
        fail("truncated");
    std::memcpy(model.triangles.data(), file.data() + at, 6 * size_t(triangles));
    at += 6 * size_t(triangles);
    std::vector<std::pair<int32_t, int32_t>> descriptors;
    for (int32_t b = 0; b < buffers; ++b, at += 8)
        descriptors.emplace_back(readAt<int32_t>(file, at), readAt<int32_t>(file, at + 4));
    for (const auto &[type, size] : descriptors)
    {
        const uint32_t stride = 36 + (type & 3) * 8 + (type >> 2) * 8;
        loaded.animated = loaded.animated || (type & 4) != 0;
        const size_t used = size_t(size / stride) * stride;
        if (at + size > file.size())
            fail("truncated");
        model.strides.push_back(stride);
        model.buffers.emplace_back(file.begin() + at, file.begin() + at + used);
        at += size;
    }
    return loaded;
}

int gameModels(const std::filesystem::path &root, const std::vector<std::string> &folders)
{
    std::map<std::string, Totals> totals;
    size_t failures = 0, other_versions = 0, refused = 0, without = 0;
    for (const std::string &folder : folders)
    {
        std::vector<std::filesystem::path> files;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root / folder))
        {
            std::string extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(), ::tolower);
            if (entry.is_regular_file() && extension == ".gm")
                files.push_back(entry.path());
        }
        std::ranges::sort(files);
        for (const auto &path : files)
        {
            const std::string name = std::filesystem::relative(path, root).string();
            try
            {
                const Loaded loaded = loadGm(path);
                const Model &model = loaded.model;
                if (!far_lod::Wanted(name, loaded.radius, loaded.animated))
                {
                    ++without; // drawn whole by the engine
                    continue;
                }
                if (far_lod::CheckMeshLayout(model.objects, model.counts(), model.triangles))
                {
                    ++refused; // the engine draws such a model whole, with a warning
                    continue;
                }
                totals[folder].add(check(model));
            }
            catch (const OtherVersion &)
            {
                ++other_versions;
            }
            catch (const std::exception &error)
            {
                ++failures;
                std::printf("FAIL %s: %s\n", name.c_str(), error.what());
            }
        }
    }
    for (const auto &[folder, t] : totals)
    {
        std::printf("%-12s %5zu models %7zu objects %7zu clusters %9zu triangles; kept per level", folder.c_str(),
                    t.models, t.objects, t.clusters, t.triangles);
        for (int l = 0; l < far_lod::kLevels; ++l)
            std::printf(" %5.1f%%", 100.0 * t.kept[l] / std::max<size_t>(1, t.triangles));
        std::printf("; %.2f bytes/triangle encoded; %.1f s (%.1f us/triangle)\n",
                    double(t.encoded) / std::max<size_t>(1, t.triangles), t.ms / 1000,
                    1000 * t.ms / std::max<size_t>(1, t.triangles));
    }
    std::printf("level errors (m):");
    for (const float e : far_lod::kLevelError)
        std::printf(" %.2f", e);
    std::printf("\n%zu failed, %zu without far LODs, %zu other versions skipped, %zu layouts refused\n", failures, without,
                other_versions, refused);
    return failures == 0 ? 0 : 1;
}} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::printf("check <RESOURCE/MODELS directory>\n");
        return 2;
    }
    try
    {
        return gameModels(argv[1], {"Ships", "Islands", "Locations"});
    }
    catch (const std::exception &error)
    {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
}
