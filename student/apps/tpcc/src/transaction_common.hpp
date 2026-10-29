#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/protocol/rmdb_client.hpp"
#include "db/types/value.hpp"

#include "workload.hpp"

namespace db::tpcc {

auto transaction_error(std::string message) -> base::detail::Failure;

auto operation(std::uint16_t statement_id, std::vector<types::Value> parameters = {})
    -> protocol::RmdbBatchOperation;
auto integer(std::uint32_t value) -> types::Value;
auto signed_integer(std::int32_t value) -> types::Value;
auto money(float value) -> types::Value;
auto text(std::string value) -> types::Value;

auto query_rows(const protocol::RmdbBatchResponse& batch, std::size_t operation_index)
    -> base::Result<std::span<const std::vector<types::Value>>>;
auto require_row_count(
    const protocol::RmdbBatchResponse& batch,
    std::size_t operation_index,
    std::size_t expected,
    std::string_view label
) -> base::Status;
auto scalar_i32(const protocol::RmdbBatchResponse& batch, std::size_t operation_index)
    -> base::Result<std::int32_t>;
auto scalar_f32(const protocol::RmdbBatchResponse& batch, std::size_t operation_index)
    -> base::Result<float>;
auto value_i32(const types::Value& value) -> base::Result<std::int32_t>;
auto value_f32(const types::Value& value) -> base::Result<float>;
auto value_text(const types::Value& value) -> base::Result<std::string_view>;
auto valid_char(const types::Value& value, std::size_t maximum, bool allow_empty) noexcept -> bool;

auto other_warehouse(
    TransactionGenerator& generator,
    std::uint32_t warehouse_count,
    std::uint32_t home_warehouse
) noexcept -> std::uint32_t;
auto next_stock_quantity(std::int32_t old_quantity, std::int32_t ordered_quantity) noexcept
    -> std::int32_t;
auto timestamp() -> std::string;
auto canonical_float_bits(float value) noexcept -> std::uint32_t;
auto exact_float32_units(float value) noexcept -> std::optional<std::uint64_t>;
auto exact_float32_sum(std::uint64_t units) noexcept -> std::optional<float>;

}  // namespace db::tpcc
