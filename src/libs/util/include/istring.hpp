#pragma once

#include <string>

namespace storm
{

// Case folding of the engine's case-insensitive strings: ASCII letters only, byte by byte, the
// same in every locale. std::toupper(char) is undefined for the negative values cp1251 and
// UTF-8 bytes (Cyrillic text) have as signed char. Other bytes are kept and, like
// std::char_traits<char>, ordered as unsigned char.
constexpr unsigned char FoldCase(unsigned char c) noexcept
{
    return c >= 'a' && c <= 'z' ? static_cast<unsigned char>(c - ('a' - 'A')) : c;
}

constexpr unsigned char FoldCase(char c) noexcept
{
    return FoldCase(static_cast<unsigned char>(c));
}

template <typename T> struct ichar_traits : public std::char_traits<T>
{
    static bool eq(T c1, T c2)
    {
        return FoldCase(c1) == FoldCase(c2);
    }

    static bool lt(T c1, T c2)
    {
        return FoldCase(c1) < FoldCase(c2);
    }

    static int compare(const T *s1, const T *s2, size_t n)
    {
        while (n-- != 0)
        {
            if (FoldCase(*s1) < FoldCase(*s2))
                return -1;
            if (FoldCase(*s1) > FoldCase(*s2))
                return 1;
            ++s1;
            ++s2;
        }
        return 0;
    }

    static const T *find(const T *source, int n, const T &target)
    {
        const auto uppercase_target = FoldCase(target);
        while (n-- > 0 && FoldCase(*source) != uppercase_target)
        {
            ++source;
        }
        return source;
    }
};

using istring = std::basic_string<char, ichar_traits<char>>;
using istring_view = std::basic_string_view<char, ichar_traits<char>>;

template <class DstTraits, class CharT, class SrcTraits>
constexpr std::basic_string_view<CharT, DstTraits> traits_cast(
    const std::basic_string_view<CharT, SrcTraits> &src) noexcept
{
    return {src.data(), src.size()};
}

template <class DstTraits, class CharT, class SrcTraits>
constexpr std::basic_string<CharT, DstTraits> traits_cast(
    const std::basic_string<CharT, SrcTraits> &src) noexcept
{
    return {src.data(), src.size()};
}

} // namespace storm
