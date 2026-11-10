// Engine-independent parts of the Caribbean Legend internal natives (src/libs/core/src/natives_logic.*):
// hash, hrand, SmartSort keys and Sprintf formatting.
#include "natives_logic.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace storm::natives;

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

std::string fmt(std::string_view f, std::vector<FormatArg> args, std::string *error = nullptr)
{
    std::string e;
    auto out = formatScript(f, args, e);
    if (error)
        *error = e;
    else if (!e.empty())
        std::fprintf(stderr, "unexpected format error: %s\n", e.c_str());
    return out;
}
} // namespace

int main()
{
    // hash: deterministic, differs for different strings, empty string is the FNV offset basis
    expect(hash32("") == static_cast<int32_t>(2166136261u), "hash of empty string");
    expect(hash32("alpha") == hash32("alpha") && hash32("alpha") != hash32("alphb"), "hash determinism");

    // hrand: in [0, n], same for same seed/site/tag, '&' tags ignore the site, seed changes values
    bool inRange = true;
    std::set<int32_t> seen;
    for (int i = 0; i < 2000; ++i)
    {
        const int32_t v = hashRandom(9, "seed", "site" + std::to_string(i), "");
        inRange = inRange && v >= 0 && v <= 9;
        seen.insert(v);
    }
    expect(inRange, "hrand range");
    expect(seen.size() == 10, "hrand reaches every value of [0, 9]");
    expect(hashRandom(1000, "s", "f@1", "t") == hashRandom(1000, "s", "f@1", "t"), "hrand stable");
    expect(hashRandom(100000, "s", "f@1", "&t") == hashRandom(100000, "s", "g@7", "&t"), "& tags shared across sites");
    int differ = 0;
    for (int i = 0; i < 20; ++i)
        differ += hashRandom(100000, "day1", "f@" + std::to_string(i), "") !=
                  hashRandom(100000, "day2", "f@" + std::to_string(i), "");
    expect(differ >= 18, "another seed gives other numbers");
    expect(hashRandom(0, "s", "x", "") == 0 && hashRandom(-5, "s", "x", "") == 0, "hrand of n <= 0");

    // sort keys
    SortType t{};
    expect(parseSortType("int", t) && t == SortType::Int, "int type");
    expect(!parseSortType("bogus", t), "unknown type");
    expect(sortKeyLess(makeSortKey("ico_2", 4, SortType::Int), makeSortKey("ico_10", 4, SortType::Int), SortType::Int),
           "offset skips the prefix of numbers");
    expect(sortKeyLess(makeSortKey("apple", 0, SortType::String), makeSortKey("Banana", 0, SortType::String),
                       SortType::String),
           "strings compare case-insensitively");
    expect(sortKeyLess(makeSortKey("1.5", 0, SortType::Float), makeSortKey("1.75", 0, SortType::Float), SortType::Float),
           "floats");
    expect(sortKeyLess(makeSortKey("31.12.1650", 0, SortType::Date), makeSortKey("01.01.1651", 0, SortType::Date),
                       SortType::Date),
           "dates day.month.year by year first");
    expect(sortKeyLess(makeSortKey("1650-12-31", 0, SortType::Date), makeSortKey("1651-01-01", 0, SortType::Date),
                       SortType::Date),
           "dates year-month-day");
    expect(sortKeyLess(makeSortKey("05.03.1650 10:15", 0, SortType::Date),
                       makeSortKey("05.03.1650 10:20", 0, SortType::Date), SortType::Date),
           "dates with time");

    // Sprintf
    expect(fmt("%d apples and %s", {int32_t{3}, std::string("pears")}) == "3 apples and pears", "d and s");
    expect(fmt("%5.2f|%-4d|%03d|%x", {1.5f, int32_t{7}, int32_t{5}, int32_t{255}}) == " 1.50|7   |005|ff", "flags");
    expect(fmt("100%%", {}) == "100%", "percent");
    expect(fmt("%s", {int32_t{12}}) == "12", "int as string");
    std::string error;
    expect(fmt("%d and %d", {int32_t{1}}, &error) == "1 and %d" && !error.empty(), "missing argument reported");
    expect(fmt("%q", {int32_t{1}}, &error) == "%q" && !error.empty(), "unknown conversion reported");

    if (failures)
        return EXIT_FAILURE;
    std::puts("natives logic: OK");
    return EXIT_SUCCESS;
}
