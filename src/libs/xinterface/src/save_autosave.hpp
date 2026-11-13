#pragma once
// The autosave records: three files per profile, «Автосохранение 1..3», written in rotation.
//
// Every autosave (the application going to the background, a landing from the ship, the
// game's own autosave before a boarding) asks which record to write
// (MSG_INTERFACE_AUTOSAVE_RECORD). The answer is the lowest number that has no file yet,
// else the record written longest ago. The player's own records are never an answer, and no
// record of the player can take one of these names (XINTERFACE::NewSaveFileName refuses it),
// so an autosave cannot replace a save the player made.
//
// The name is matching data shared with the save list (ui menu_saves.cpp shows these records
// as the autosave group) and with files written by earlier builds; it is not UI text.
#include <array>
#include <cstdlib>
#include <optional>
#include <string>

namespace storm::save
{
inline constexpr int kAutosaveRecords = 3;
inline constexpr char kAutosavePrefix[] = "Автосохранение ";

// "Автосохранение 2" → 2; 0 for any other name (also for a number outside the rotation).
inline int AutosaveRecordNumber(const std::string &file)
{
    static const std::string prefix = kAutosavePrefix;
    if (file.size() <= prefix.size() || file.compare(0, prefix.size(), prefix) != 0)
        return 0;
    const std::string number = file.substr(prefix.size());
    if (number.size() > 2 || number.find_first_not_of("0123456789") != std::string::npos || number[0] == '0')
        return 0;
    const int value = std::atoi(number.c_str());
    return value >= 1 && value <= kAutosaveRecords ? value : 0;
}

inline std::string AutosaveRecordName(int number)
{
    return kAutosavePrefix + std::to_string(number);
}

// Chooses the record to write. `Time` is any ordered time of last writing.
template <class Time> class AutosaveRotation
{
  public:
    // A file of the profile's save folder; names that are no autosave record are ignored.
    void Add(const std::string &file, Time written)
    {
        if (const int number = AutosaveRecordNumber(file))
            written_[static_cast<size_t>(number - 1)] = written;
    }
    int Next() const
    {
        int oldest = 1;
        for (int number = 1; number <= kAutosaveRecords; ++number)
        {
            const auto &written = written_[static_cast<size_t>(number - 1)];
            if (!written)
                return number;
            if (*written < *written_[static_cast<size_t>(oldest - 1)])
                oldest = number;
        }
        return oldest;
    }

  private:
    std::array<std::optional<Time>, kAutosaveRecords> written_;
};
} // namespace storm::save
