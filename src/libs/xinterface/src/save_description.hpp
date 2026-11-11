#pragma once
// Records written without a description.
//
// A save keeps its description (interface.c GetSaveDataString) with the thumbnail in the
// extended section after the script state (core save_file.hpp: the 40-byte header holds
// sFileInfo[32], extOffset, extSize). SFLB_DoSaveFileData used to write that section only
// with a screenshot; the application's background autosave (no frame to take it from)
// therefore wrote records with extSize 0. Their script state is whole, but seadogs.c
// LoadGame asks MSG_INTERFACE_GET_SAVE_DATA for the description first and silently drops the
// load when it is empty. Such a record is described by its file name (the name the game
// gave it, e.g. «Автосохранение 1»); the caller says so in the log. A record whose header
// cannot be read is not described: the load is refused aloud (load_watch.hpp).
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace storm
{
inline std::optional<std::string> UndescribedRecordDescription(const std::filesystem::path &file, const std::string &record)
{
    std::ifstream in(file, std::ios::binary);
    char header[40];
    if (!in || !in.read(header, sizeof(header)))
        return std::nullopt;
    uint32_t extOffset = 0, extSize = 0;
    std::memcpy(&extOffset, header + 32, sizeof(extOffset));
    std::memcpy(&extSize, header + 36, sizeof(extSize));
    if (extOffset != 0 || extSize != 0)
        return std::nullopt; // it has a section: a damaged one is not ours to paper over
    const auto slash = record.find_last_of("\\/");
    const std::string name = slash == std::string::npos ? record : record.substr(slash + 1);
    if (name.empty())
        return std::nullopt;
    return name;
}
} // namespace storm
