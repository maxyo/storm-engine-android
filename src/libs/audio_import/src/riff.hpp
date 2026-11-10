#pragma once
// FMOD Studio bank RIFF helpers shared by bank.cpp and string_table.cpp.

#include "reader.hpp"

namespace storm::audio_import::detail
{
// Body of the first chunk with this id, searched depth first through LISTs
// below the top-level RIFF "FEV " form. Empty optional when absent.
std::optional<ByteView> findBankChunk(ByteView bank, const char *id);

// FMOD's array prefix: u16 (count << 1 | 1) followed by u16 element size.
uint32_t readStructArrayHeader(Reader &reader, uint32_t elementSize);
} // namespace storm::audio_import::detail
