#include <algorithm>
#include <compare>

#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/access/heap_fetch.hpp"
#include "db/access/heap_table.hpp"
#include "db/base/lab_missing.hpp"

#include "heap_row_decoder.hpp"
#include "logical_scan_ssi.hpp"
#include "storage_query_binding.hpp"
#include "storage_scan_ports.hpp"

namespace db::engine::detail {

namespace {

[[maybe_unused]] auto bound_matches(
    [[maybe_unused]] const execution::IndexScanRequest& request,
    [[maybe_unused]] const std::vector<execution::IndexScanBound>& bounds,
    [[maybe_unused]] const std::span<const types::Value> row
) -> bool {
    // LAB-S07.Q05:R09-BEGIN
    base::lab::missing_algorithm("S07.Q05:R09");
    // LAB-S07.Q05:R09-END
}

// The planner validates this shape before it reaches execution: an equality
// prefix followed by, at most, one lower/upper range pair. Keep a defensive
// translation here so the storage adapter neither trusts a malformed port
// caller nor leaks B-tree-specific bound concepts into execution.
struct IndexBtreeBounds final {
    std::vector<types::Value> equality_prefix;
    std::vector<types::Value> lower;
    std::vector<types::Value> upper;
    bool lower_exclusive{false};
    bool upper_exclusive{false};

    [[maybe_unused]] auto has_range() const noexcept -> bool {
        return !lower.empty() || !upper.empty();
    }
};

struct IndexKeyBounds final {
    std::optional<types::Value> equality;
    std::optional<types::Value> lower;
    std::optional<types::Value> upper;
    bool lower_exclusive{false};
    bool upper_exclusive{false};
};

[[maybe_unused]] auto has_any_bound([[maybe_unused]] const IndexKeyBounds& bounds) noexcept
    -> bool {
    // LAB-S07.Q05:R10-BEGIN
    base::lab::missing_algorithm("S07.Q05:R10");
    // LAB-S07.Q05:R10-END
}

[[maybe_unused]] auto apply_index_bound(
    [[maybe_unused]] const types::DataType& key_type,
    [[maybe_unused]] IndexKeyBounds& key_bounds,
    [[maybe_unused]] const execution::IndexScanBound& bound
) -> base::Status {
    // LAB-S07.Q05:R11-BEGIN
    base::lab::missing_algorithm("S07.Q05:R11");
    // LAB-S07.Q05:R11-END
}

[[maybe_unused]] auto
assemble_index_bounds([[maybe_unused]] const std::vector<IndexKeyBounds>& by_key)
    -> base::Result<IndexBtreeBounds> {
    // LAB-S07.Q05:R12-BEGIN
    base::lab::missing_algorithm("S07.Q05:R12");
    // LAB-S07.Q05:R12-END
}

[[maybe_unused]] auto translate_index_bounds(
    [[maybe_unused]] const execution::IndexScanRequest& request,
    [[maybe_unused]] const std::span<const execution::IndexScanBound> bounds
) -> base::Result<IndexBtreeBounds> {
    // LAB-S07.Q05:R13-BEGIN
    base::lab::missing_algorithm("S07.Q05:R13");
    // LAB-S07.Q05:R13-END
}

[[maybe_unused]] auto btree_range([[maybe_unused]] const IndexBtreeBounds& bounds) noexcept
    -> access::BtreeScanRange {
    // LAB-S07.Q05:R14-BEGIN
    base::lab::missing_algorithm("S07.Q05:R14");
    // LAB-S07.Q05:R14-END
}

[[maybe_unused]] auto open_btree_scan(
    [[maybe_unused]] const access::BtreeRelation& relation,
    [[maybe_unused]] const IndexBtreeBounds& bounds,
    [[maybe_unused]] const access::BtreeScanContext context
) -> base::Result<access::BtreeScan> {
    // LAB-S07.Q05:R15-BEGIN
    base::lab::missing_algorithm("S07.Q05:R15");
    // LAB-S07.Q05:R15-END
}

[[maybe_unused]] auto rebind_btree_scan(
    [[maybe_unused]] access::BtreeScan& scan,
    [[maybe_unused]] const IndexBtreeBounds& bounds
) -> base::Status {
    // LAB-S07.Q05:R16-BEGIN
    base::lab::missing_algorithm("S07.Q05:R16");
    // LAB-S07.Q05:R16-END
}

using IndexRowDecoder = base::Status (*)(
    const access::TupleSchema&,
    base::Bytes,
    std::span<const access::TupleColumnUsage>,
    std::vector<types::Value>&
);

[[maybe_unused]] auto decode_full_index_row(
    [[maybe_unused]] const access::TupleSchema& schema,
    [[maybe_unused]] const base::Bytes payload,
    [[maybe_unused]] const std::span<const access::TupleColumnUsage> column_usage,
    [[maybe_unused]] std::vector<types::Value>& values
) -> base::Status {
    // LAB-S02.Q07:R35-BEGIN
    base::lab::missing_algorithm("S02.Q07:R35");
    // LAB-S02.Q07:R35-END
}

[[maybe_unused]] auto decode_projected_index_row(
    [[maybe_unused]] const access::TupleSchema& schema,
    [[maybe_unused]] const base::Bytes payload,
    [[maybe_unused]] const std::span<const access::TupleColumnUsage> column_usage,
    [[maybe_unused]] std::vector<types::Value>& values
) -> base::Status {
    // LAB-S02.Q07:R36-BEGIN
    base::lab::missing_algorithm("S02.Q07:R36");
    // LAB-S02.Q07:R36-END
}

class HeapIndexScanPort final : public execution::IndexScanPort {
public:
    HeapIndexScanPort(
        QueryExecutionContext& context,
        std::optional<execution::ExpressionProgram> logical_filter,
        const bool projected
    ) noexcept
        : context_{context},
          logical_filter_{std::move(logical_filter)},
          decode_row_{projected ? decode_projected_index_row : decode_full_index_row},
          projected_{projected} {}

    [[maybe_unused]] auto configure([[maybe_unused]] execution::IndexScanRequest request)
        -> base::Status override {
        // LAB-S02.Q07:R37-BEGIN
        base::lab::missing_algorithm("S02.Q07:R37");
        // LAB-S02.Q07:R37-END
    }

    [[maybe_unused]] auto bind([[maybe_unused]] std::span<const execution::IndexScanBound> bounds)
        -> base::Status override {
        // LAB-S07.Q05:R17-BEGIN
        base::lab::missing_algorithm("S07.Q05:R17");
        // LAB-S07.Q05:R17-END
    }

    [[maybe_unused]] auto open([[maybe_unused]] execution::ExecContext& context)
        -> base::Status override {
        // LAB-S02.Q07:R38-BEGIN
        base::lab::missing_algorithm("S02.Q07:R38");
        // LAB-S02.Q07:R38-END
    }

    [[maybe_unused]] auto next(
        [[maybe_unused]] execution::ExecContext& context,
        [[maybe_unused]] std::vector<types::Value> recycled_values
    ) -> base::Result<std::optional<execution::Tuple>> override {
        // LAB-S02.Q07:R39-BEGIN
        base::lab::missing_algorithm("S02.Q07:R39");
        // LAB-S02.Q07:R39-END
    }

    [[maybe_unused]] auto rescan([[maybe_unused]] execution::ExecContext& context)
        -> base::Status override {
        // LAB-S07.Q05:R18-BEGIN
        base::lab::missing_algorithm("S07.Q05:R18");
        // LAB-S07.Q05:R18-END
    }

    [[maybe_unused]] auto close(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        scan_.reset();
        relation_.reset();
        heap_.reset();
        descriptor_.reset();
        column_usage_.clear();
        tuple_schema_.reset();
        snapshot_.reset();
        table_.reset();
        btree_bounds_.reset();
        bounds_.clear();
        return {};
    }

private:
    [[maybe_unused]] auto prepare_decoder([[maybe_unused]] const access::TupleSchema& tuple_schema)
        -> base::Status {
        // LAB-S02.Q07:R40-BEGIN
        base::lab::missing_algorithm("S02.Q07:R40");
        // LAB-S02.Q07:R40-END
    }

    [[maybe_unused]] auto record_logical_siread(
        const transaction::TransactionBackend& backend,
        const catalog::TableMetadata& table,
        const execution::ExecContext& context,
        const execution::TupleDescriptorHandle& descriptor
    ) const -> base::Status {
        auto* const conflicts = serializable_conflicts(context_, backend);
        if (conflicts == nullptr) {
            return {};
        }
        auto logical_ssi = make_logical_scan_ssi(logical_filter_, context, descriptor);
        if (!logical_ssi) {
            return base::fail(std::move(logical_ssi).error());
        }
        auto recorded = conflicts->record_predicate_read(
            backend.xid(),
            table.relation_id(),
            std::move(logical_ssi->predicate)
        );
        if (!recorded) {
            return base::fail(
                std::move(recorded).error().with_context(
                    "engine.query",
                    "record logical index scan SIREAD"
                )
            );
        }
        return {};
    }

    QueryExecutionContext& context_;
    execution::IndexScanRequest request_{};
    std::optional<execution::ExpressionProgram> logical_filter_;
    IndexRowDecoder decode_row_;
    bool projected_;
    std::vector<execution::IndexScanBound> bounds_;
    std::optional<IndexBtreeBounds> btree_bounds_;
    std::optional<catalog::TableMetadata> table_;
    std::optional<access::TupleSchema> tuple_schema_;
    execution::TupleDescriptorHandle descriptor_;
    std::vector<access::TupleColumnUsage> column_usage_;
    std::optional<access::HeapTable> heap_;
    std::optional<access::BtreeRelation> relation_;
    std::optional<access::BtreeScan> scan_;
    std::optional<transaction::Snapshot> snapshot_;
    bool configured_{false};
};

}  // namespace

auto make_heap_index_scan_port(
    QueryExecutionContext& context,
    std::optional<execution::ExpressionProgram> logical_filter,
    const bool projected
) -> std::unique_ptr<execution::IndexScanPort> {
    return std::make_unique<HeapIndexScanPort>(context, std::move(logical_filter), projected);
}

}  // namespace db::engine::detail
