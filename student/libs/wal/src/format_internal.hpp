#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::wal::format_internal {

inline auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.format",
        "encode"
    );
}

inline auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.format",
        "decode"
    );
}

inline auto unsupported(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kUnsupported, std::move(message)}.with_context(
        "wal.format",
        "decode"
    );
}

inline auto finish_write(
    const base::Status& status,
    const base::LittleEndianWriter& writer,
    const std::size_t expected
) -> base::Status {
    if (!status) {
        return base::fail(status.error().with_context("wal.format", "write fixed header"));
    }
    if (writer.position() != expected) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "WAL fixed-header encoder wrote an unexpected number of bytes",
            }
        );
    }
    return {};
}

inline auto all_zero(const base::Bytes bytes) noexcept -> bool {
    return std::all_of(bytes.begin(), bytes.end(), [](const std::byte value) {
        return value == std::byte{0};
    });
}

}  // namespace db::wal::format_internal
