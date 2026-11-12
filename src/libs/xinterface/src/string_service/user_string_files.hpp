#pragma once

#include "istring.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace storm::strings
{

// The user string files of the current language (resource/ini/texts/<language>/*.txt) that scripts
// open by name (LanguageOpenFile), translate from by id and close again. Ids, reference counts and
// "a closed id translates nothing" are those of the original STRSERVICE list.
//
// A file whose last reference is closed stays parsed (up to `keepClosed` of them, the least
// recently closed one goes first): the interface scripts open, read and close the same file many
// times per frame (GetConvertStr, the application's facts adapter), and each open read the file
// from disk and parsed it again. Reopening a kept file gives it its old id back. A language change
// (Reload) drops the closed files and re-reads the open ones.
class UserStringFiles
{
  public:
    // One "name {text}" entry of a file; either part may be missing in a malformed file.
    using Row = std::pair<std::optional<std::string>, std::optional<std::string>>;
    using Rows = std::vector<Row>;
    // Reads and parses one file of the current language; none when it cannot be read.
    using Loader = std::function<std::optional<Rows>(const std::string &fileName)>;

    explicit UserStringFiles(size_t keepClosed) : keepClosed_(keepClosed)
    {
    }

    // The id of the open file, or -1 when it cannot be loaded (the loader reports why).
    int32_t Open(std::string_view fileName, const Loader &load)
    {
        const auto key = Fold(fileName);
        for (auto &[id, file] : files_)
            if (file.key == key)
            {
                ++file.refs;
                return id;
            }
        auto rows = load(std::string(fileName));
        if (!rows)
            return -1;
        ++loads_;
        const int32_t id = FreeId();
        File file{std::string(fileName), key, 1, 0, {}, {}};
        file.Index(std::move(*rows));
        files_.emplace(id, std::move(file));
        return id;
    }

    void Close(int32_t id)
    {
        const auto found = files_.find(id);
        if (found == files_.end() || found->second.refs <= 0)
            return;
        if (--found->second.refs > 0)
            return;
        found->second.closedAt = ++closes_;
        Trim();
    }

    // The text of `name` in the open file `id`; nullptr for an unknown name, a missing text, or an
    // id that is not open.
    const char *Translate(int32_t id, std::string_view name) const
    {
        const auto found = files_.find(id);
        if (found == files_.end() || found->second.refs <= 0)
            return nullptr;
        const auto &file = found->second;
        const auto row = file.index.find(Fold(name));
        if (row == file.index.end())
            return nullptr;
        const auto &text = file.rows[row->second].second;
        return text ? text->c_str() : nullptr;
    }

    // The language changed: closed files are dropped, open ones are re-read under their ids and
    // reference counts. Returns a line for each open file that could not be re-read (it is closed)
    // or whose names differ from the previous language's.
    std::vector<std::string> Reload(const Loader &load)
    {
        std::vector<std::string> problems;
        for (auto it = files_.begin(); it != files_.end();)
        {
            auto &file = it->second;
            if (file.refs <= 0)
            {
                it = files_.erase(it);
                continue;
            }
            auto rows = load(file.name);
            if (!rows)
            {
                problems.push_back("can't reinit user language file " + file.name);
                it = files_.erase(it);
                continue;
            }
            ++loads_;
            File fresh{file.name, file.key, file.refs, 0, {}, {}};
            fresh.Index(std::move(*rows));
            for (const auto &[name, row] : fresh.index)
                if (!file.index.count(name))
                    problems.push_back(file.name + ": string <" + *fresh.rows[row].first + "> is new");
            for (const auto &[name, row] : file.index)
                if (!fresh.index.count(name))
                    problems.push_back(file.name + ": string <" + *file.rows[row].first + "> not found");
            file = std::move(fresh);
            ++it;
        }
        return problems;
    }

    // Files read from disk so far (tests, diagnostics).
    size_t Loads() const noexcept
    {
        return loads_;
    }

  private:
    struct File
    {
        std::string name;
        std::string key;
        int32_t refs = 0;
        uint64_t closedAt = 0;
        Rows rows;
        std::unordered_map<std::string, size_t> index; // folded name -> first row of that name

        void Index(Rows parsed)
        {
            rows = std::move(parsed);
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].first)
                    index.emplace(Fold(*rows[i].first), i);
        }
    };

    static std::string Fold(std::string_view text)
    {
        std::string folded(text);
        for (auto &c : folded)
            c = static_cast<char>(FoldCase(c));
        return folded;
    }

    int32_t FreeId() const
    {
        int32_t id = 0;
        for (const auto &entry : files_)
            if (entry.first == id)
                ++id;
            else if (entry.first > id)
                break;
        return id;
    }

    void Trim()
    {
        size_t closed = 0;
        for (const auto &entry : files_)
            closed += entry.second.refs <= 0;
        while (closed > keepClosed_)
        {
            auto oldest = files_.end();
            for (auto it = files_.begin(); it != files_.end(); ++it)
                if (it->second.refs <= 0 && (oldest == files_.end() || it->second.closedAt < oldest->second.closedAt))
                    oldest = it;
            files_.erase(oldest);
            --closed;
        }
    }

    size_t keepClosed_;
    std::map<int32_t, File> files_;
    uint64_t closes_ = 0;
    size_t loads_ = 0;
};

} // namespace storm::strings
