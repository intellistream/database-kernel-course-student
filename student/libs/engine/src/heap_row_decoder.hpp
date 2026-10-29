#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "db/access/tuple_codec.hpp"
#include "db/execution/scan_port.hpp"

#include "query_error.hpp"

namespace db::engine::detail {

inline auto decode_values(
    const access::TupleSchema& schema,
    const std::span<const std::byte> payload,
    const std::string_view operation,
    std::vector<types::Value> values
) -> base::Result<std::vector<types::Value>> {
    auto decoded =
        access::decode_row_into(schema, base::Bytes{payload.data(), payload.size()}, values);
    if (!decoded) {
        return base::fail(
            std::move(decoded).error().with_context("engine.query", std::string{operation})
        );
    }
    return values;
}

inline auto decode_projected_values(
    const access::TupleSchema& schema,
    const std::span<const std::byte> payload,
    const std::span<const access::TupleColumnUsage> column_usage,
    const std::string_view operation,
    std::vector<types::Value> values
) -> base::Result<std::vector<types::Value>> {
    auto decoded = access::decode_row_projected_into(
        schema,
        base::Bytes{payload.data(), payload.size()},
        column_usage,
        values
    );
    if (!decoded) {
        return base::fail(
            std::move(decoded).error().with_context("engine.query", std::string{operation})
        );
    }
    return values;
}

inline auto scan_column_usage(
    const access::TupleSchema& schema,
    const std::optional<std::vector<execution::ScanColumnIndex>>& required_columns
) -> base::Result<std::vector<access::TupleColumnUsage>> {
    const auto default_usage = required_columns.has_value()
                                   ? access::TupleColumnUsage::kSkipped
                                   : access::TupleColumnUsage::kMaterialized;
    std::vector<access::TupleColumnUsage> usage(schema.size(), default_usage);
    const auto mark_required = [&usage](const execution::ScanColumnIndex column) -> base::Status {
        const auto index = static_cast<std::size_t>(column.zero_based());
        if (index >= usage.size()) {
            return base::fail(query_error(
                base::ErrorCode::kPlan,
                "scan required column is outside the relation schema",
                "prepare projected heap decoder"
            ));
        }
        usage[index] = access::TupleColumnUsage::kMaterialized;
        return {};
    };
    if (required_columns.has_value()) {
        for (const auto column : *required_columns) {
            if (auto marked = mark_required(column); !marked) {
                return base::fail(std::move(marked).error());
            }
        }
    }
    return usage;
}

}  // namespace db::engine::detail
