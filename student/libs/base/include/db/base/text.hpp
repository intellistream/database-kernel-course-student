#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace db::base {

constexpr auto is_ascii_space(const char character) noexcept -> bool {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r'
           || character == '\f' || character == '\v';
}

constexpr auto trim_ascii_space(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && is_ascii_space(text.front())) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && is_ascii_space(text.back())) {
        text.remove_suffix(1U);
    }
    return text;
}

constexpr auto ascii_lower(const char character) noexcept -> char {
    if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character + ('a' - 'A'));
    }
    return character;
}

constexpr auto
ascii_equal_ignoring_case(const std::string_view left, const std::string_view right) noexcept
    -> bool {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

// Validates the UTF-8 code point beginning at position and returns its
// exclusive byte end. The decoder rejects overlong encodings, UTF-16
// surrogates, values above U+10FFFF, continuation leads, and truncation.
constexpr auto utf8_code_point_end(const std::string_view text, const std::size_t position) noexcept
    -> std::optional<std::size_t> {
    if (position >= text.size()) {
        return std::nullopt;
    }

    const auto first = static_cast<unsigned char>(text[position]);
    if (first <= 0x7fU) {
        return position + 1U;
    }

    std::size_t length = 0U;
    if (first >= 0xc2U && first <= 0xdfU) {
        length = 2U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        length = 3U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        length = 4U;
    } else {
        return std::nullopt;
    }
    if (length > text.size() - position) {
        return std::nullopt;
    }

    const auto continuation = [](const unsigned char byte) constexpr noexcept -> bool {
        return byte >= 0x80U && byte <= 0xbfU;
    };
    const auto second = static_cast<unsigned char>(text[position + 1U]);
    if (!continuation(second) || (first == 0xe0U && second < 0xa0U)
        || (first == 0xedU && second > 0x9fU) || (first == 0xf0U && second < 0x90U)
        || (first == 0xf4U && second > 0x8fU)) {
        return std::nullopt;
    }
    for (std::size_t offset = 2U; offset < length; ++offset) {
        if (!continuation(static_cast<unsigned char>(text[position + offset]))) {
            return std::nullopt;
        }
    }
    return position + length;
}

// Returns the largest complete code-point prefix whose byte size does not
// exceed maximum_bytes. The complete input is still validated, so malformed
// bytes beyond the returned prefix cannot be hidden by truncation.
constexpr auto
valid_utf8_prefix_size(const std::string_view text, const std::size_t maximum_bytes) noexcept
    -> std::optional<std::size_t> {
    std::size_t position = 0U;
    std::size_t prefix_size = 0U;
    while (position < text.size()) {
        const auto next = utf8_code_point_end(text, position);
        if (!next.has_value()) {
            return std::nullopt;
        }
        position = *next;
        if (position <= maximum_bytes) {
            prefix_size = position;
        }
    }
    return prefix_size;
}

constexpr auto is_valid_utf8(const std::string_view text) noexcept -> bool {
    const auto prefix = valid_utf8_prefix_size(text, text.size());
    return prefix.has_value() && *prefix == text.size();
}

}  // namespace db::base
