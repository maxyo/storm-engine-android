#include "utf8.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

int main()
{
    using utf8::Utf8ToCodepoint;
    assert(Utf8ToCodepoint("") == -1);
    assert(Utf8ToCodepoint("A") == 'A');
    assert(Utf8ToCodepoint("\x7f") == 0x7f);
    assert(Utf8ToCodepoint("\xD0\x96") == 0x416);         // Ж
    assert(Utf8ToCodepoint("\xE2\x82\xAC") == 0x20AC);     // €
    assert(Utf8ToCodepoint("\xF0\x9F\x98\x80") == 0x1F600); // 😀
    // Truncated sequences: no read past the terminator (heap copies of exact size, ASan).
    for (const char *text : {"\xD0", "\xE2\x82", "\xF0\x9F\x98", "\xE2", "\xF0"})
    {
        const size_t size = std::strlen(text) + 1;
        std::unique_ptr<char[]> exact(new char[size]);
        std::memcpy(exact.get(), text, size);
        assert(Utf8ToCodepoint(exact.get()) == -1);
    }
    // Encoded UTF-16 surrogates (U+D800..U+DFFF) are invalid; signed char used to hide this check.
    assert(Utf8ToCodepoint("\xED\xA0\x80") == -1);
    assert(Utf8ToCodepoint("\xED\xBF\xBF") == -1);
    assert(Utf8ToCodepoint("\xED\x9F\xBF") == 0xD7FF); // last code point before the surrogates
    assert(Utf8ToCodepoint("\xEE\x80\x80") == 0xE000); // first one after
    // Round trip through the encoder.
    for (uint32_t cp : {0x24u, 0x416u, 0x20ACu, 0xFFFDu, 0x1F600u, 0x10FFFFu})
    {
        char out[5];
        utf8::CodepointToUtf8(out, cp);
        assert(static_cast<uint32_t>(Utf8ToCodepoint(out)) == cp);
    }
    // Decoding every character of a long text is linear: 4 MiB took minutes with strlen per call.
    std::string text;
    for (int i = 0; i < 1024 * 1024; ++i)
        text += "\xD0\x96\xE2\x82";
    text.resize(text.size() - 2);
    const auto begin = std::chrono::steady_clock::now();
    long long sum = 0;
    for (size_t i = 0; i < text.size(); i += utf8::u8_inc(text.c_str() + i))
        sum += Utf8ToCodepoint(text.c_str() + i);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    assert(sum > 0 && ms < 5000);
    std::printf("utf8: decode/surrogates/bounds PASS (%zu bytes walked in %.0f ms under ASan)\n", text.size(), ms);
}
