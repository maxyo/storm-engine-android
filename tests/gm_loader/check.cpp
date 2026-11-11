// Production .gm loader (GEOM) on synthetic files through a fake GEOM_SERVICE; see run.py.
#include "geom.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace
{
int failures = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                  \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

// Files are served from memory (synthetic) or disk; reads past the end are recorded.
class FakeService : public GEOM_SERVICE
{
  public:
    std::map<std::string, std::string> files;
    std::vector<std::string> textures;
    std::vector<std::vector<uint8_t>> buffers;
    bool overread = false;
    std::streamoff end_position = -1, file_size = -1;

    std::fstream OpenFile(const char *fname) override
    {
        std::fstream stream;
        if (const auto it = files.find(fname); it != files.end())
        {
            // A real fstream is required by the interface: write the synthetic file to a temporary.
            const std::string path = std::filesystem::temp_directory_path() / ("storm-gm-" + std::to_string(getpid()) + ".gm");
            std::ofstream(path, std::ios::binary).write(it->second.data(), std::streamsize(it->second.size()));
            stream.open(path, std::ios::in | std::ios::binary);
            std::filesystem::remove(path); // stays readable while open (POSIX)
        }
        else
            stream.open(fname, std::ios::in | std::ios::binary);
        stream.seekg(0, std::ios::end);
        file_size = stream.tellg();
        stream.seekg(0);
        return stream;
    }
    bool ReadFile(std::fstream &file, void *data, int32_t bytes) override
    {
        file.read(static_cast<char *>(data), bytes);
        if (file.gcount() != bytes)
            overread = true;
        return file.gcount() == bytes;
    }
    int FileSize(const char *) override { return 0; }
    void CloseFile(std::fstream &file) override
    {
        if (file.is_open())
            end_position = file.tellg();
        file.close();
    }
    void *malloc(int32_t bytes) override { return ::malloc(bytes > 0 ? size_t(bytes) : 1); }
    void free(void *ptr) override { ::free(ptr); }
    GEOS::ID CreateTexture(const char *fname) override
    {
        textures.emplace_back(fname);
        return GEOS::ID(textures.size() - 1);
    }
    void ReleaseTexture(GEOS::ID) override {}
    void SetMaterial(const GEOS::MATERIAL &) override {}
    GEOS::ID CreateVertexBuffer(int32_t, int32_t size) override
    {
        buffers.emplace_back(size_t(size));
        return GEOS::ID(buffers.size() - 1);
    }
    void *LockVertexBuffer(GEOS::ID vb) override { return buffers[vb].data(); }
    void UnlockVertexBuffer(GEOS::ID) override {}
    void ReleaseVertexBuffer(GEOS::ID) override {}
    GEOS::ID CreateIndexBuffer(int32_t size) override
    {
        buffers.emplace_back(size_t(size));
        return GEOS::ID(buffers.size() - 1);
    }
    void *LockIndexBuffer(GEOS::ID ib) override { return buffers[ib].data(); }
    void UnlockIndexBuffer(GEOS::ID) override {}
    void ReleaseIndexBuffer(GEOS::ID) override {}
    void SetIndexBuffer(GEOS::ID) override {}
    void SetVertexBuffer(int32_t, GEOS::ID) override {}
    void DrawIndexedPrimitive(int32_t, int32_t, int32_t, int32_t, int32_t) override {}
    void SetPbrMaterial(const storm::render::PbrMaterial *) override {}
    GEOS::ID CreateLight(GEOS::LIGHT) override { return 0; }
    void ActivateLight(GEOS::ID) override {}
    void SetCausticMode(bool) override {}
};

template <class T> void put(std::string &out, const T &value) { out.append(reinterpret_cast<const char *>(&value), sizeof(T)); }

// One object, one material with one texture, one triangle, one 3-vertex buffer of type 0.
std::string make_gm(const char version[4])
{
    const std::string names = std::string("root\0obj\0group\0mat\0Location\\Wall.dds\0", 38);
    std::string out;
    put(out, int32_t(version[3] | version[2] << 8 | version[1] << 16 | version[0] << 24));
    const int32_t header[10] = {0, int32_t(names.size()), 5, 1, 1, 0, 0, 1, 1, 1};
    out.append(reinterpret_cast<const char *>(header), sizeof(header));
    const float bbox[7] = {1, 1, 1, 0, 0, 0, 1};
    out.append(reinterpret_cast<const char *>(bbox), sizeof(bbox));
    out += names;
    for (int32_t offset : {0, 5, 9, 15, 19})
        put(out, offset);
    put(out, int32_t(19)); // texture name
    RDF_MATERIAL material{9, 15, 0.8f, 0, 2, 0, {TEXTURE_BASE, TEXTURE_NONE, TEXTURE_NONE, TEXTURE_NONE}, {0, -1, -1, -1}};
    put(out, material);
    RDF_OBJECT object{};
    object.group_name = 0;
    object.name = 5;
    object.flags = VISIBLE;
    object.ntriangles = 1;
    object.nvertices = 3;
    put(out, object);
    const uint16_t triangle[3] = {0, 1, 2};
    out.append(reinterpret_cast<const char *>(triangle), sizeof(triangle));
    put(out, RDF_VERTEXBUFF{0, 3 * int32_t(sizeof(RDF_VERTEX0))});
    for (int i = 0; i < 3; ++i)
        put(out, RDF_VERTEX0{{float(i), 0, 0}, {0, 1, 0}, int32_t(0xff808080), 0.5f, float(i)});
    return out;
}

void test_synthetic()
{
    for (const char *version : {"1.05", "1.06"})
    {
        FakeService service;
        service.files["model.gm"] = make_gm(version);
        try
        {
            std::unique_ptr<GEOS> geometry(CreateGeometry("model.gm", nullptr, service, 0));
            GEOS::INFO info;
            geometry->GetInfo(info);
            CHECK(info.nobjects == 1 && info.ntextures == 1 && info.nmaterials == 1);
            CHECK(service.textures.size() == 1 && service.textures[0] == "Location\\Wall.dds");
            CHECK(!service.overread);
            CHECK(service.end_position == service.file_size);
            GEOS::OBJECT object;
            geometry->GetObj(0, object);
            CHECK(std::string(object.name) == "obj");
        }
        catch (const std::exception &error)
        {
            fprintf(stderr, "version %s: %s\n", version, error.what());
            ++failures;
        }
    }
    FakeService service;
    service.files["old.gm"] = make_gm("1.04");
    bool rejected = false;
    try
    {
        std::unique_ptr<GEOS>(CreateGeometry("old.gm", nullptr, service, 0));
    }
    catch (const std::runtime_error &error)
    {
        rejected = std::string(error.what()).find("'1.04'") != std::string::npos;
    }
    CHECK(rejected);
}

int corpus(const char *root)
{
    const auto started = std::chrono::steady_clock::now();
    std::map<std::string, int> loaded, failed;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        std::string ext = entry.path().extension().string();
        for (char &c : ext)
            c = char(tolower(static_cast<unsigned char>(c)));
        if (!entry.is_regular_file() || ext != ".gm")
            continue;
        char version[4] = {};
        std::ifstream(entry.path(), std::ios::binary).read(version, 4);
        const std::string v{version[3], version[2], version[1], version[0]};
        if (v != "1.05" && v != "1.06")
        {
            ++loaded["skipped " + v + " (unsupported legacy)"];
            continue;
        }
        FakeService service;
        try
        {
            std::unique_ptr<GEOS>(CreateGeometry(entry.path().c_str(), nullptr, service, 0));
            if (service.overread || service.end_position != service.file_size)
            {
                if (failed[v]++ < 3)
                    printf("FAIL %s: consumed %lld of %lld bytes\n", entry.path().c_str(), (long long)service.end_position,
                           (long long)service.file_size);
            }
            else
                ++loaded[v];
        }
        catch (const std::exception &error)
        {
            if (failed[v]++ < 3)
                printf("FAIL %s: %s\n", entry.path().c_str(), error.what());
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    int bad = 0;
    for (const auto &[v, n] : loaded)
        printf("  ok   %5d  %s\n", n, v.c_str());
    for (const auto &[v, n] : failed)
        printf("  fail %5d  %s\n", n, v.c_str()), bad += n;
    printf("corpus %s: %.1f s\n", root, seconds);
    return bad ? 1 : 0;
}
} // namespace

int main(int argc, char **argv)
{
    test_synthetic();
    if (failures)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("gm_loader: all checks passed\n");
    return argc > 1 ? corpus(argv[1]) : 0;
}
