#include "transaction_common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <limits>
#include <string>
#include <utility>

#include "db/base/error.hpp"

#include "workload.hpp"

namespace db::tpcc {

auto transaction_error(std::string message) -> base::detail::Failure {
    return base::fail(base::Error{base::ErrorCode::kExecution, std::move(message)});
}

auto operation(const std::uint16_t statement_id, std::vector<types::Value> parameters)
    -> protocol::RmdbBatchOperation {
    return protocol::RmdbBatchOperation{
        .statement_id = statement_id,
        .parameters = std::move(parameters),
    };
}

auto integer(const std::uint32_t value) -> types::Value {
    return types::Value::integer(static_cast<std::int64_t>(value));
}

auto signed_integer(const std::int32_t value) -> types::Value {
    return types::Value::integer(value);
}

auto money(const float value) -> types::Value {
    return types::Value::floating(value);
}

auto text(std::string value) -> types::Value {
    return types::Value::character(std::move(value));
}

auto query_rows(const protocol::RmdbBatchResponse& batch, const std::size_t operation_index)
    -> base::Result<std::span<const std::vector<types::Value>>> {
    if (operation_index >= batch.responses.size()
        || !batch.responses[operation_index].has_value()) {
        return transaction_error(
            "TPC-C query operation " + std::to_string(operation_index) + " omitted its rows"
        );
    }
    const protocol::RmdbResponse& response = *batch.responses[operation_index];
    if (response.terminal.kind != protocol::RmdbTerminalKind::kResultEnd) {
        return transaction_error(
            "TPC-C query operation " + std::to_string(operation_index)
            + " did not end with RESULT_END"
        );
    }
    return std::span<const std::vector<types::Value>>{response.rows};
}

auto require_row_count(
    const protocol::RmdbBatchResponse& batch,
    const std::size_t operation_index,
    const std::size_t expected,
    const std::string_view label
) -> base::Status {
    auto rows = query_rows(batch, operation_index);
    if (!rows) {
        return base::fail(std::move(rows).error());
    }
    if (rows->size() != expected) {
        return transaction_error(
            std::string{label} + " returned " + std::to_string(rows->size()) + " rows, expected "
            + std::to_string(expected)
        );
    }
    return {};
}

namespace {

auto scalar_cell(
    const protocol::RmdbBatchResponse& batch,
    const std::size_t operation_index,
    const std::string_view type_name
) -> base::Result<const types::Value*> {
    auto rows = query_rows(batch, operation_index);
    if (!rows) {
        return base::fail(std::move(rows).error());
    }
    if (rows->size() != 1U || rows->front().size() != 1U) {
        return transaction_error(
            "TPC-C batch operation " + std::to_string(operation_index)
            + " did not return one scalar " + std::string{type_name} + " row"
        );
    }
    return &rows->front().front();
}

}  // namespace

auto scalar_i32(const protocol::RmdbBatchResponse& batch, const std::size_t operation_index)
    -> base::Result<std::int32_t> {
    auto cell = scalar_cell(batch, operation_index, "INT32");
    if (!cell) {
        return base::fail(std::move(cell).error());
    }
    return value_i32(*cell.value());
}

auto scalar_f32(const protocol::RmdbBatchResponse& batch, const std::size_t operation_index)
    -> base::Result<float> {
    auto cell = scalar_cell(batch, operation_index, "FLOAT32");
    if (!cell) {
        return base::fail(std::move(cell).error());
    }
    return value_f32(*cell.value());
}

auto value_i32(const types::Value& value) -> base::Result<std::int32_t> {
    const std::int64_t* const integer_value = value.as_integer();
    if (integer_value == nullptr) {
        return transaction_error("TPC-C expected an INT32 result cell");
    }
    return static_cast<std::int32_t>(*integer_value);
}

auto value_f32(const types::Value& value) -> base::Result<float> {
    const float* const floating_value = value.as_float();
    if (floating_value == nullptr || !std::isfinite(*floating_value)) {
        return transaction_error("TPC-C expected a finite FLOAT32 result cell");
    }
    return *floating_value;
}

auto value_text(const types::Value& value) -> base::Result<std::string_view> {
    const std::string* const string_value = value.as_string();
    if (string_value == nullptr) {
        return transaction_error("TPC-C expected a CHAR result cell");
    }
    return std::string_view{*string_value};
}

auto valid_char(
    const types::Value& value,
    const std::size_t maximum,
    const bool allow_empty
) noexcept -> bool {
    const std::string* const string_value = value.as_string();
    if (string_value == nullptr || string_value->size() > maximum
        || (!allow_empty && string_value->empty())) {
        return false;
    }
    return std::ranges::none_of(*string_value, [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20U || byte == 0x7fU;
    });
}

auto other_warehouse(
    TransactionGenerator& generator,
    const std::uint32_t warehouse_count,
    const std::uint32_t home_warehouse
) noexcept -> std::uint32_t {
    const std::uint32_t candidate = generator.value(1U, warehouse_count - 1U);
    return candidate >= home_warehouse ? candidate + 1U : candidate;
}

auto next_stock_quantity(
    const std::int32_t old_quantity,
    const std::int32_t ordered_quantity
) noexcept -> std::int32_t {
    return old_quantity >= ordered_quantity + 10 ? old_quantity - ordered_quantity
                                                 : old_quantity - ordered_quantity + 91;
}

auto timestamp() -> std::string {
    const std::time_t current =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    static_cast<void>(::gmtime_r(&current, &utc));
    std::array<char, 20> buffer{};
    static_cast<void>(std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &utc));
    return std::string{buffer.data()};
}

auto canonical_float_bits(const float value) noexcept -> std::uint32_t {
    return std::bit_cast<std::uint32_t>(value == 0.0F ? 0.0F : value);
}

auto exact_float32_units(const float value) noexcept -> std::optional<std::uint64_t> {
    constexpr double scale = 1'073'741'824.0;
    if (!std::isfinite(value) || value < 0.0F) {
        return std::nullopt;
    }
    const double scaled = static_cast<double>(value) * scale;
    if (!std::isfinite(scaled) || scaled != std::trunc(scaled)
        || scaled >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(scaled);
}

auto exact_float32_sum(const std::uint64_t units) noexcept -> std::optional<float> {
    constexpr std::uint64_t maximum_exact_integer = std::uint64_t{1U} << 53U;
    constexpr double scale = 1'073'741'824.0;
    if (units > maximum_exact_integer) {
        return std::nullopt;
    }
    const double sum = static_cast<double>(units) / scale;
    const float rounded = static_cast<float>(sum);
    if (!std::isfinite(sum) || !std::isfinite(rounded)) {
        return std::nullopt;
    }
    return rounded;
}

}  // namespace db::tpcc
