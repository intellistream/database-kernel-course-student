#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "db/types/type_io.hpp"

namespace db::engine::detail {

// Field views borrow the input line and must be consumed before its next reuse.
struct TextField final {
    std::string_view bytes;
    bool escaped{false};
};

class TextFields final {
public:
    TextFields(const std::string_view line, const std::uint8_t delimiter) noexcept
        : line_{line},
          delimiter_{delimiter} {}

    auto next() noexcept -> std::optional<TextField>;

private:
    std::string_view line_;
    std::uint8_t delimiter_;
    std::size_t position_{0U};
    bool finished_{false};
};

auto decode_copy_text_field(
    TextField field,
    std::string_view null_marker,
    const types::PreparedTypeInput& input,
    types::Value& destination
) -> base::Status;

}  // namespace db::engine::detail
