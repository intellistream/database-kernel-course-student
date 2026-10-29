#include "db/protocol/session_port.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "db/base/exception_boundary.hpp"
#include "db/base/text.hpp"
#include "db/types/type_io.hpp"

namespace db::protocol {
namespace {

inline constexpr base::ExceptionBoundaryPolicy kResultBoundary{
    .component = "protocol.execution_result",
    .allocation_message = "RMDB result conversion could not allocate bounded state",
    .exception_prefix = "unexpected exception escaped RMDB result conversion: ",
    .unknown_exception_message = "unknown exception escaped RMDB result conversion",
};

auto result_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "protocol.execution_result",
        "convert query result"
    );
}

auto validate_columns(const std::span<const ColumnMetadata> columns) -> base::Status {
    if (columns.empty() || columns.size() > std::numeric_limits<std::uint16_t>::max()) {
        return base::fail(result_error(
            base::ErrorCode::kInvalidArgument,
            "RMDB query result column count is outside the wire range"
        ));
    }
    for (const ColumnMetadata& column : columns) {
        if (column.name.empty() || column.name.size() > std::numeric_limits<std::uint16_t>::max()
            || !base::is_valid_utf8(column.name) || !is_known_wire_data_type(column.data_type)) {
            return base::fail(result_error(
                base::ErrorCode::kInvalidArgument,
                "RMDB query result contains invalid column metadata"
            ));
        }
    }
    return {};
}

auto convert_cell(const WireDataType data_type, const types::Value& value)
    -> base::Result<WireCell> {
    if (value.is_null()) {
        const bool compatible =
            (data_type == WireDataType::kInt32 && value.data_type().id() == types::kInt64TypeId)
            || (data_type == WireDataType::kFloat32
                && value.data_type().id() == types::kFloat32TypeId)
            || (data_type == WireDataType::kChar
                && (value.data_type().id() == types::kCharTypeId
                    || value.data_type().id() == types::kTimestampTypeId));
        if (!compatible) {
            return base::fail(result_error(
                base::ErrorCode::kInvalidArgument,
                "typed NULL does not match its RMDB result column"
            ));
        }
        return WireCell::null();
    }

    switch (data_type) {
        case WireDataType::kInt32: {
            const std::int64_t* const integer = value.as_integer();
            if (integer == nullptr) {
                return base::fail(result_error(
                    base::ErrorCode::kInvalidArgument,
                    "result value does not match its RMDB INT32 column"
                ));
            }
            if (*integer < std::numeric_limits<std::int32_t>::min()
                || *integer > std::numeric_limits<std::int32_t>::max()) {
                return base::fail(result_error(
                    base::ErrorCode::kOutOfRange,
                    "result integer cannot be represented by RMDB INT32"
                ));
            }
            return WireCell::integer(static_cast<std::int32_t>(*integer));
        }
        case WireDataType::kFloat32: {
            const float* const floating = value.as_float();
            if (floating == nullptr) {
                return base::fail(result_error(
                    base::ErrorCode::kInvalidArgument,
                    "result value does not match its RMDB FLOAT32 column"
                ));
            }
            return WireCell::floating(*floating);
        }
        case WireDataType::kChar: {
            if (value.data_type().id() != types::kCharTypeId
                && value.data_type().id() != types::kTimestampTypeId) {
                return base::fail(result_error(
                    base::ErrorCode::kInvalidArgument,
                    "result value does not match its RMDB CHAR column"
                ));
            }
            auto text = types::output_value(value);
            if (!text || text->size() > std::numeric_limits<std::uint32_t>::max()
                || !base::is_valid_utf8(text.value())) {
                return base::fail(result_error(
                    base::ErrorCode::kOutOfRange,
                    "result text cannot be represented by RMDB CHAR"
                ));
            }
            return WireCell::text(std::move(text).value());
        }
    }
    return base::fail(result_error(
        base::ErrorCode::kInvalidArgument,
        "result column uses an unknown RMDB data type"
    ));
}

}  // namespace

WireCell::WireCell(Storage value) noexcept
    : value_{std::move(value)} {}

// Keeping the active alternative explicit also avoids GCC 15 losing the
// variant discriminator/storage relation when O1 and TSan inline nested row copies.
WireCell::WireCell(const WireCell& other)
    : value_{std::visit(
          []<typename Alternative>(const Alternative& value) -> Storage {
              return Storage{std::in_place_type<Alternative>, value};
          },
          other.value_
      )} {}

auto WireCell::operator=(const WireCell& other) -> WireCell& {
    WireCell replacement{other};
    value_ = std::move(replacement.value_);
    return *this;
}

auto WireCell::null() noexcept -> WireCell {
    return WireCell{Storage{std::monostate{}}};
}

auto WireCell::integer(const std::int32_t value) noexcept -> WireCell {
    return WireCell{Storage{value}};
}

auto WireCell::floating(const float value) noexcept -> WireCell {
    return WireCell{Storage{value}};
}

auto WireCell::text(std::string value) noexcept -> WireCell {
    return WireCell{Storage{std::move(value)}};
}

auto WireCell::is_null() const noexcept -> bool {
    return std::holds_alternative<std::monostate>(value_);
}

auto WireCell::integer() const -> std::int32_t {
    return std::get<std::int32_t>(value_);
}

auto WireCell::floating() const -> float {
    return std::get<float>(value_);
}

auto WireCell::text() const -> std::string_view {
    return std::get<std::string>(value_);
}

ExecutionResult::ExecutionResult(
    std::vector<ColumnMetadata> columns,
    std::vector<std::vector<WireCell>> rows
) noexcept
    : kind_{ExecutionKind::kQuery},
      columns_{std::move(columns)},
      rows_{std::move(rows)} {}

auto ExecutionResult::query(
    std::vector<ColumnMetadata> columns,
    std::vector<std::vector<types::Value>> rows
) -> base::Result<ExecutionResult> {
    return base::invoke_exception_boundary(
        kResultBoundary,
        "convert query result",
        [&]() -> base::Result<ExecutionResult> {
            auto valid = validate_columns(columns);
            if (!valid) {
                return base::fail(std::move(valid).error());
            }
            std::vector<std::vector<WireCell>> wire_rows;
            wire_rows.reserve(rows.size());
            for (const std::vector<types::Value>& row : rows) {
                if (row.size() != columns.size()) {
                    return base::fail(result_error(
                        base::ErrorCode::kInvalidArgument,
                        "result row width does not match its RMDB columns"
                    ));
                }
                std::vector<WireCell> wire_row;
                wire_row.reserve(row.size());
                for (std::size_t index = 0U; index < row.size(); ++index) {
                    auto cell = convert_cell(columns[index].data_type, row[index]);
                    if (!cell) {
                        return base::fail(std::move(cell).error());
                    }
                    wire_row.push_back(std::move(cell).value());
                }
                wire_rows.push_back(std::move(wire_row));
            }
            return ExecutionResult{std::move(columns), std::move(wire_rows)};
        }
    );
}

auto ExecutionResult::kind() const noexcept -> ExecutionKind {
    return kind_;
}

auto ExecutionResult::columns() const noexcept -> std::span<const ColumnMetadata> {
    return columns_;
}

auto ExecutionResult::rows() const noexcept -> std::span<const std::vector<WireCell>> {
    return rows_;
}

}  // namespace db::protocol
