#include "user_string_files.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

using storm::strings::UserStringFiles;

namespace
{
int failures = 0;
void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

// A language: file name -> rows; every load is counted.
struct Disk
{
    std::map<std::string, UserStringFiles::Rows> files;
    std::map<std::string, int> reads;
    UserStringFiles::Loader Loader()
    {
        return [this](const std::string &name) -> std::optional<UserStringFiles::Rows> {
            ++reads[name];
            const auto found = files.find(name);
            if (found == files.end())
                return {};
            return found->second;
        };
    }
};

bool Is(const char *text, const char *expected)
{
    return text && std::strcmp(text, expected) == 0;
}
} // namespace

int main()
{
    Disk ru;
    ru.files["ItemsDescribe.txt"] = {{"blade1", "Сабля"}, {"BLADE1", "second of the same name"}, {std::nullopt, "orphan"},
                                     {"empty", std::nullopt}};
    ru.files["AbilityDescribe.txt"] = {{"Sliding", "Скольжение"}};
    UserStringFiles files(2);
    const auto load = ru.Loader();

    // Open, translate (case-insensitive, first of a name wins), close.
    const int32_t items = files.Open("ItemsDescribe.txt", load);
    Check(items == 0, "first file gets id 0");
    Check(Is(files.Translate(items, "Blade1"), "Сабля"), "case-insensitive lookup, first row of a name");
    Check(files.Translate(items, "empty") == nullptr, "a name without text translates to nothing");
    Check(files.Translate(items, "missing") == nullptr, "unknown name");
    // Reference counting: a second open shares the id, one close keeps it open.
    Check(files.Open("itemsdescribe.TXT", load) == items, "file names compare case-insensitively");
    files.Close(items);
    Check(Is(files.Translate(items, "blade1"), "Сабля"), "still open after one of two closes");
    files.Close(items);
    Check(files.Translate(items, "blade1") == nullptr, "a closed id translates nothing");
    files.Close(items); // closing a closed id is harmless
    // The per-frame pattern: open, read, close, many times: one disk read.
    for (int i = 0; i < 100; ++i)
    {
        const int32_t id = files.Open("ItemsDescribe.txt", load);
        Check(id == items, "a kept file reopens under its old id");
        Check(Is(files.Translate(id, "blade1"), "Сабля"), "kept file translates");
        files.Close(id);
    }
    Check(ru.reads["ItemsDescribe.txt"] == 1, "reopening a closed file does not read it again");
    // A kept closed file still holds its id: a new file gets the next one.
    const int32_t ability = files.Open("AbilityDescribe.txt", load);
    Check(ability == 1, "kept file keeps its id reserved");
    // A file that does not exist: -1, and it is retried on the next open (nothing cached).
    Check(files.Open("Nope.txt", load) == -1, "missing file");
    Check(files.Open("Nope.txt", load) == -1 && ru.reads["Nope.txt"] == 2, "a missing file is not cached");

    // Only `keepClosed` closed files stay: the least recently closed goes first.
    ru.files["A.txt"] = {{"a", "1"}};
    ru.files["B.txt"] = {{"b", "2"}};
    files.Close(ability); // closed: Items (older), Ability
    files.Close(files.Open("A.txt", load)); // three closed: Items is dropped
    Check(files.Open("AbilityDescribe.txt", load) == ability && ru.reads["AbilityDescribe.txt"] == 1, "recent closed kept");
    files.Close(ability);
    files.Open("ItemsDescribe.txt", load);
    Check(ru.reads["ItemsDescribe.txt"] == 2, "oldest closed file was dropped and is read again");

    // Language change: closed files are dropped, open ones re-read under the same id and count.
    Disk en;
    en.files["ItemsDescribe.txt"] = {{"blade1", "Saber"}, {"blade2", "Rapier"}};
    const int32_t itemsNow = files.Open("ItemsDescribe.txt", load); // refs 2
    const auto problems = files.Reload(en.Loader());
    Check(Is(files.Translate(itemsNow, "blade1"), "Saber"), "open file re-read in the new language");
    Check(en.reads.count("A.txt") == 0 && en.reads.count("AbilityDescribe.txt") == 0, "closed files are not re-read");
    Check(problems.size() == 2, "new and missing names are reported");
    files.Close(itemsNow);
    Check(Is(files.Translate(itemsNow, "blade2"), "Rapier"), "reference count survives the reload");
    files.Close(itemsNow);
    Check(files.Translate(itemsNow, "blade1") == nullptr, "and the second close closes it");
    Check(files.Open("ItemsDescribe.txt", en.Loader()) == itemsNow && en.reads["ItemsDescribe.txt"] == 1,
          "the re-read file is kept after its close");
    files.Close(itemsNow);
    // An open file that does not exist in the new language is closed and reported.
    const int32_t b = files.Open("B.txt", load);
    Disk empty;
    const auto lost = files.Reload(empty.Loader());
    Check(lost.size() == 1 && files.Translate(b, "b") == nullptr, "file missing in the new language is closed");

    if (failures)
        return EXIT_FAILURE;
    std::puts("PASS user string files: one read per file, ids and reference counts kept, reload on language change");
    return EXIT_SUCCESS;
}
