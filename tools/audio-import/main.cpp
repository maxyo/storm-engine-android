// storm-audio-import: FMOD Studio banks / WAV -> Ogg Vorbis (host tool).
#include "audio_import/audio_import.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace fs = std::filesystem;
using namespace storm::audio_import;

namespace
{
Bytes readFile(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw ImportError("cannot open " + path.string());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

void writeFile(const fs::path &path, const void *data, size_t size)
{
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    if (!out)
        throw ImportError("cannot write " + path.string());
}

std::string fileName(const std::string &name)
{
    std::string out;
    for (char c : name)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == ' ') ? c : '_';
    return out.empty() || out[0] == '.' ? "_" + out : out;
}

std::string quote(const std::string &text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c < 0x20)
        {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
            out += escaped;
            continue;
        }
        out += static_cast<char>(c);
    }
    return out + "\"";
}

struct BankResult
{
    size_t samples = 0;
    size_t remuxed = 0;
};

// Remuxes every sample; failures are reported per sample and counted.
BankResult importBank(const fs::path &bankPath, const fs::path &outDir, VorbisHeaderCache &cache)
{
    const Bytes bank = readFile(bankPath);
    BankResult result;
    fs::create_directories(outDir);
    std::string manifest = "{\"bank\":" + quote(bankPath.filename().string()) + ",\"samples\":[";
    std::set<std::string> used;
    bool firstEntry = true;
    for (ByteView image : findFsb5(bank))
    {
        const Fsb5 fsb = parseFsb5(image);
        if (fsb.codec != static_cast<uint32_t>(Fsb5Codec::Vorbis))
            throw ImportError(bankPath.string() + ": FSB5 codec " + std::to_string(fsb.codec) +
                              " is not supported (only Vorbis)");
        for (const auto &sample : fsb.samples)
        {
            ++result.samples;
            const std::string file = fileName(sample.name) + ".ogg";
            std::string status = "ok";
            try
            {
                if (!used.insert(file).second)
                    throw ImportError("duplicate output name " + file);
                const Bytes ogg = remuxFsb5Vorbis(sample, cache);
                writeFile(outDir / file, ogg.data(), ogg.size());
                ++result.remuxed;
            }
            catch (const ImportError &error)
            {
                status = error.what();
                std::cerr << bankPath.filename().string() << ": " << sample.name << ": " << status << "\n";
            }
            manifest += firstEntry ? "\n" : ",\n";
            firstEntry = false;
            manifest += "{\"name\":" + quote(sample.name) + ",\"file\":" + quote(file) +
                        ",\"channels\":" + std::to_string(sample.channels) +
                        ",\"rate\":" + std::to_string(sample.frequency) +
                        ",\"frames\":" + std::to_string(sample.sampleCount);
            if (sample.loop)
                manifest += ",\"loop\":[" + std::to_string(sample.loop->first) + "," +
                            std::to_string(sample.loop->second) + "]";
            manifest += ",\"status\":" + quote(status) + "}";
        }
    }
    manifest += "\n]}\n";
    if (result.samples != 0)
        writeFile(outDir / "manifest.json", manifest.data(), manifest.size());
    return result;
}

int usage()
{
    std::cerr << "usage:\n"
                 "  storm-audio-import bank <file.bank> <out-dir>\n"
                 "  storm-audio-import strings <file.strings.bank> <out.json>\n"
                 "  storm-audio-import wav <in.wav> <out.ogg> [quality -0.1..1.0, default 0.5]\n"
                 "  storm-audio-import scan <banks-dir> <out-dir>\n";
    return 2;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
        return usage();
    const std::string command = argv[1];
    const auto started = std::chrono::steady_clock::now();
    try
    {
        VorbisHeaderCache cache;
        if (command == "bank" && argc == 4)
        {
            const BankResult result = importBank(argv[2], argv[3], cache);
            std::cout << "samples " << result.samples << " remuxed " << result.remuxed << "\n";
            return result.samples == result.remuxed ? 0 : 1;
        }
        if (command == "strings" && argc == 4)
        {
            const auto entries = readStringTable(readFile(argv[2]));
            const std::string json = stringTableJson(entries);
            writeFile(argv[3], json.data(), json.size());
            std::cout << "strings " << entries.size() << "\n";
            return 0;
        }
        if (command == "wav" && (argc == 4 || argc == 5))
        {
            const float quality = argc == 5 ? std::stof(argv[4]) : 0.5f;
            const Bytes ogg = encodeWavToVorbis(readFile(argv[2]), quality);
            writeFile(argv[3], ogg.data(), ogg.size());
            return 0;
        }
        if (command == "scan" && argc == 4)
        {
            const fs::path root = argv[2], out = argv[3];
            BankResult total;
            size_t banks = 0, strings = 0;
            for (const auto &entry : fs::recursive_directory_iterator(root))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".bank")
                    continue;
                ++banks;
                const fs::path relative = fs::relative(entry.path(), root);
                const std::string name = entry.path().filename().string();
                if (name.size() > 13 && name.ends_with(".strings.bank"))
                {
                    const auto entries = readStringTable(readFile(entry.path()));
                    fs::create_directories(out);
                    const std::string json = stringTableJson(entries);
                    writeFile(out / (entry.path().stem().string() + ".json"), json.data(), json.size());
                    strings += entries.size();
                    continue;
                }
                const BankResult result = importBank(entry.path(), out / relative.parent_path() / entry.path().stem(), cache);
                std::cout << relative.string() << ": samples " << result.samples << " remuxed " << result.remuxed << "\n";
                total.samples += result.samples;
                total.remuxed += result.remuxed;
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::cout << "banks " << banks << " samples " << total.samples << " remuxed " << total.remuxed
                      << " strings " << strings << " seconds " << seconds << "\n";
            return total.samples == total.remuxed ? 0 : 1;
        }
        return usage();
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
