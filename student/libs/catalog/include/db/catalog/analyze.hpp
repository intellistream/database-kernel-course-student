#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/catalog/schema.hpp"
#include "db/catalog/statistics.hpp"
#include "db/types/value.hpp"

namespace db::catalog {

inline constexpr std::size_t kDefaultAnalyzeSampleRows = 3'000U;

struct AnalyzeCompletion final {
    std::uint32_t relation_pages{0U};
    std::uint64_t known_dead_rows{0U};
    base::Xid analyzed_xid{base::kInvalidXid};
};

// Catalog owns the deterministic sample/calculation policy; a composition layer owns MVCC-visible
// heap enumeration and supplies each tuple's already-encoded width. This prevents catalog from
// depending on access, transaction, or engine.
class AnalyzeAccumulator final {
public:
    static auto create(
        base::Oid table_oid,
        Schema schema,
        std::size_t sample_target = kDefaultAnalyzeSampleRows
    ) -> base::Result<AnalyzeAccumulator>;

    auto observe(std::span<const types::Value> row, std::size_t encoded_row_width) -> base::Status;
    auto finish(AnalyzeCompletion completion) const -> base::Result<TableStatistics>;

    auto live_rows() const noexcept -> std::uint64_t;
    auto sample_rows() const noexcept -> std::size_t;

private:
    AnalyzeAccumulator(base::Oid table_oid, Schema schema, std::size_t sample_target);

    auto next_random() noexcept -> std::uint64_t;

    base::Oid table_oid_;
    Schema schema_;
    std::size_t sample_target_;
    std::vector<std::vector<types::Value>> sample_;
    std::uint64_t live_rows_{0U};
    std::uint64_t total_row_width_{0U};
    std::uint64_t random_state_;
};

}  // namespace db::catalog
