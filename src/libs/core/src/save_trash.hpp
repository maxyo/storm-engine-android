#pragma once

// Deferred deletion of save files.
//
// The game's scripts overwrite a slot by deleting the old save and then
// writing a new one, possibly under another name. A process death between the
// two steps used to lose the slot. The scripts belong to the player's game
// files, so the engine makes the delete itself recoverable.
//
// Rule
// ----
// 1. Deleting a save renames it to "<name>.storm-deleted". It disappears from
//    listings at once (FILE_SERVICE hides auxiliary names in save folders).
// 2. The delete becomes final, i.e. the file is really removed, when either
//      - the next save is written successfully (the overwrite completed), or
//      - kGracePeriod has passed in the same session (a plain delete by the
//        player; checked every frame and whenever a save folder is listed).
// 3. If writing the next save fails, every pending delete is undone.
// 4. A "<name>.storm-deleted" that this session did not create is the trace of
//    a session that died inside the grace period. When its folder is next
//    listed, it is restored under its name, or removed if a save with that
//    name exists again.
//
// Consequence: a save deleted by the player comes back only if the process is
// killed within kGracePeriod of the delete. The rule errs towards keeping data.

#include "save_file.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace storm::save
{
inline constexpr char kDeletedSuffix[] = ".storm-deleted";

inline bool hasSuffix(const std::string &name, const char *suffix)
{
    const auto n = std::strlen(suffix);
    return name.size() > n && name.compare(name.size() - n, n, suffix) == 0;
}

// Names the engine creates next to saves; never shown as saves.
inline bool isReservedName(const std::string &name)
{
    return hasSuffix(name, kTempSuffix) || hasSuffix(name, kBackupSuffix) || hasSuffix(name, kDeletedSuffix);
}

class Trash
{
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds kGracePeriod{10};

    // Replace a delete of `path`. Returns false if nothing was deleted.
    bool discard(const fs::path &path, Clock::time_point now = Clock::now())
    {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec)
            return false;
        const auto deleted = withSuffix(path, kDeletedSuffix);
        forget(deleted);
        fs::remove(deleted, ec); // an older deleted save of the same name is superseded
        ec.clear();
        fs::rename(path, deleted, ec);
        if (ec)
        {
            // Cannot defer (e.g. unusual filesystem): keep the original semantics.
            ec.clear();
            return fs::remove(path, ec) && !ec;
        }
        // Leftovers of an interrupted atomic write of this name die with it.
        fs::remove(withSuffix(path, kTempSuffix), ec);
        fs::remove(withSuffix(path, kBackupSuffix), ec);
        pending_.push_back({deleted, now});
        return true;
    }

    // Rule 2, time: call regularly.
    void tick(Clock::time_point now = Clock::now())
    {
        if (pending_.empty())
            return;
        std::error_code ec;
        for (auto it = pending_.begin(); it != pending_.end();)
        {
            if (now - it->since >= kGracePeriod)
            {
                fs::remove(it->deleted, ec);
                it = pending_.erase(it);
            }
            else
                ++it;
        }
    }

    // Rule 2, a save was written: every pending delete is final.
    void commit()
    {
        std::error_code ec;
        for (const auto &entry : pending_)
            fs::remove(entry.deleted, ec);
        pending_.clear();
    }

    // Rule 3, a save could not be written: bring the deleted saves back.
    void rollback()
    {
        for (const auto &entry : pending_)
            restore(entry.deleted);
        pending_.clear();
    }

    // Rule 4: call before listing `directory`. Returns the number of restored saves.
    int recover(const fs::path &directory, Clock::time_point now = Clock::now())
    {
        tick(now);
        int restored = 0;
        std::error_code ec;
        std::vector<fs::path> found;
        for (auto it = fs::directory_iterator(directory, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            std::error_code type;
            if (it->is_regular_file(type) && !type && hasSuffix(it->path().filename().string(), kDeletedSuffix))
                found.push_back(it->path());
        }
        for (const auto &deleted : found)
        {
            if (isPending(deleted))
                continue; // deleted in this session, still inside its grace period
            restored += restore(deleted) ? 1 : 0;
        }
        return restored;
    }

    size_t pendingCount() const
    {
        return pending_.size();
    }

  private:
    struct Entry
    {
        fs::path deleted;
        Clock::time_point since;
    };

    static fs::path originalName(const fs::path &deleted)
    {
        auto text = deleted.string();
        text.resize(text.size() - std::strlen(kDeletedSuffix));
        return fs::path(text);
    }

    // Returns true if the save is back under its name.
    static bool restore(const fs::path &deleted)
    {
        std::error_code ec;
        const auto original = originalName(deleted);
        if (fs::exists(original, ec))
        {
            fs::remove(deleted, ec); // the name is in use again; the old version is obsolete
            return false;
        }
        fs::rename(deleted, original, ec);
        return !ec;
    }

    bool isPending(const fs::path &deleted) const
    {
        return std::any_of(pending_.begin(), pending_.end(),
                           [&](const Entry &entry) { return entry.deleted == deleted; });
    }

    void forget(const fs::path &deleted)
    {
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      [&](const Entry &entry) { return entry.deleted == deleted; }),
                       pending_.end());
    }

    std::vector<Entry> pending_;
};

// The engine's single instance (game thread only).
inline Trash &trash()
{
    static Trash instance;
    return instance;
}
} // namespace storm::save
