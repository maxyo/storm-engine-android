#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace storm::android {

inline constexpr size_t dialogueEditMaxCodePoints = 256;
inline constexpr size_t dialogueEditMaxBytes = 1024;

struct DialogueInput {
    uint64_t generation;
    unsigned index;
    // No value means an ordinary choice; an empty value is an explicit empty edit.
    std::optional<std::string_view> text;
};

// The outer "dialogue:" prefix is removed by the existing game-thread mailbox.
// Only the first two separators are metadata. User text is never script source.
inline std::optional<DialogueInput> parseDialogueInput(std::string_view input) {
    const auto first = input.find(':');
    if (first == std::string_view::npos) return {};
    const auto second = input.find(':', first + 1);
    const auto end = second == std::string_view::npos ? input.size() : second;
    DialogueInput result{};
    const auto a = std::from_chars(input.data(), input.data() + first, result.generation);
    const auto b = std::from_chars(input.data() + first + 1, input.data() + end, result.index);
    if (a.ec != std::errc{} || a.ptr != input.data() + first ||
        b.ec != std::errc{} || b.ptr != input.data() + end) return {};
    if (second != std::string_view::npos) result.text = input.substr(second + 1);
    return result;
}

// Empty strings and punctuation are intentional script inputs. Reject rather
// than repair invalid text so the value shown by Android is the value committed.
inline std::string_view dialogueEditError(std::string_view text) {
    if (text.size() > dialogueEditMaxBytes) return "too_long";
    size_t codePoints = 0;
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const size_t count = lead >= 0x20 && lead <= 0x7e ? 1 :
            lead >= 0xc2 && lead <= 0xdf ? 2 : lead >= 0xe0 && lead <= 0xef ? 3 :
            lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        if (!count || i + count > text.size()) return "invalid_text";
        for (size_t j = 1; j < count; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80) return "invalid_text";
        if (count >= 3) {
            const auto second = static_cast<unsigned char>(text[i + 1]);
            if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
                (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f)) return "invalid_text";
        }
        // C1 controls and Unicode line/paragraph separators are not single-line input.
        if ((lead == 0xc2 && static_cast<unsigned char>(text[i + 1]) <= 0x9f) ||
            (lead == 0xe2 && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
             (static_cast<unsigned char>(text[i + 2]) == 0xa8 ||
              static_cast<unsigned char>(text[i + 2]) == 0xa9))) return "invalid_text";
        i += count;
        if (++codePoints > dialogueEditMaxCodePoints) return "too_long";
    }
    return {};
}

} // namespace storm::android
