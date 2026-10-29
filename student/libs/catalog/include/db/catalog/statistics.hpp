#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/catalog/schema.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::catalog {

inline constexpr std::size_t kMaximumAnalyzeSampleRows = 1'000'000U;

struct ColumnStatistics final {
    types::DataType data_type{types::kUnknownType};
    double null_fraction{0.0};
    std::uint32_t average_width{0U};
    std::uint64_t sample_distinct{0U};
    std::optional<types::Value> minimum{};
    std::optional<types::Value> maximum{};

    auto operator==(const ColumnStatistics&) const -> bool = default;
};

struct TableStatistics final {
    base::Oid table_oid{base::kInvalidOid};
    std::uint32_t relation_pages{0U};
    std::uint64_t live_rows{0U};
    std::uint64_t dead_rows{0U};
    std::uint32_t average_row_width{0U};
    std::uint32_t sample_rows{0U};
    base::Xid analyzed_xid{base::kInvalidXid};
    std::vector<ColumnStatistics> columns;

    auto operator==(const TableStatistics&) const -> bool = default;
};

// Validates both the statistical invariants and the exact schema association used by FCAT v1.
auto validate_table_statistics(const TableStatistics& statistics, const Schema& schema)
    -> base::Status;

}  // namespace db::catalog
