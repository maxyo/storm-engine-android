// Paging a long text that must be read before what follows it (dialogue speech before the
// replies): the pure choice of where a page ends, given the measured lines. <ui-pages>
// (draw_elements.cpp) measures; this decides, so the rules are checked without a renderer.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace storm::ui
{

struct PageBreak
{
    size_t next = 0;    // where the following page starts (byte offset into the whole text)
    size_t display = 0; // where the visible part of this page ends
    bool more = false;  // another page follows; the reserved part (replies) stays hidden
};

// `starts[i]` is the start of line i relative to `offset` (one entry more than there are
// lines: the end of the text), `bottoms[i]` the bottom of line i; `total` the height of
// the rest of the text. An intermediate page may fill `intermediate`; the final page has
// only `last` (the rest is reserved for the replies). The last line always goes to the
// final page, and a page ending on a newline does not show an empty line for it.
inline PageBreak ChoosePage(const std::string &text, size_t offset, const std::vector<size_t> &starts, const std::vector<float> &bottoms,
                            float total, float intermediate, float last)
{
    if (total <= last || bottoms.empty())
        return {text.size(), text.size(), false};
    size_t lines = 0;
    while (lines < bottoms.size() && bottoms[lines] <= intermediate)
        ++lines;
    lines = std::max<size_t>(1, std::min(lines, bottoms.size() - 1));
    const size_t end = std::min(text.size(), offset + starts[lines]);
    size_t display = end;
    if (display > offset && text[display - 1] == '\n')
        --display;
    return {end, display, true};
}

// Breaks `text` into lines no wider than `width` at spaces and at '\n' (a word wider than
// the line stays alone on its line). Returns the start offset of every line plus the end.
template <typename Measure> std::vector<size_t> WrapLines(const std::string &text, float width, Measure measure)
{
    std::vector<size_t> starts;
    size_t line_start = 0;
    while (line_start < text.size())
    {
        starts.push_back(line_start);
        const size_t newline = text.find('\n', line_start);
        const size_t hard_end = newline == std::string::npos ? text.size() : newline;
        size_t fit = line_start; // end of the longest prefix (whole words) that fits
        size_t at = line_start;
        while (at < hard_end)
        {
            size_t word_end = text.find(' ', at);
            if (word_end == std::string::npos || word_end > hard_end)
                word_end = hard_end;
            if (fit != line_start && measure(text.substr(line_start, word_end - line_start)) > width)
                break;
            fit = word_end;
            at = word_end + 1;
        }
        if (fit >= hard_end)
            line_start = hard_end + (newline == std::string::npos ? 0 : 1);
        else
            line_start = fit + 1; // the space that broke the line belongs to neither line
        if (fit >= hard_end && newline == std::string::npos)
            break;
    }
    starts.push_back(text.size());
    return starts;
}

} // namespace storm::ui
