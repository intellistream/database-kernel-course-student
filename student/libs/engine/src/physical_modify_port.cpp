#include "physical_modify_port.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "db/access/btree_relation.hpp"
#include "db/access/heap_delete.hpp"
#include "db/access/heap_index_tid.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/heap_update.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/proc_array.hpp"

#include "query_error.hpp"
#include "storage_query_binding.hpp"

namespace db::engine::detail {

namespace {

class PhysicalModifyPort final : public execution::ModifyTablePort {
public:
    explicit PhysicalModifyPort(QueryExecutionContext& context) noexcept
        : context_{context} {}

    [[maybe_unused]] auto insert(
        [[maybe_unused]] const execution::ModifyTarget& target,
        [[maybe_unused]] std::span<const types::Value> values
    ) -> base::Result<base::Rid> override {
        // LAB-S02.Q09:R10-BEGIN
        base::lab::missing_algorithm("S02.Q09:R10");
        // LAB-S02.Q09:R10-END
    }

    [[maybe_unused]] auto update(
        [[maybe_unused]] const execution::ModifyTarget& target,
        [[maybe_unused]] const base::Rid rid,
        [[maybe_unused]] const std::span<const types::Value> old_values,
        [[maybe_unused]] const std::span<const types::Value> new_values
    ) -> base::Result<execution::ModifyUpdateResult> override {
        // LAB-S02.Q09:R11-BEGIN
        base::lab::missing_algorithm("S02.Q09:R11");
        // LAB-S02.Q09:R11-END
    }

    [[maybe_unused]] auto delete_row(
        [[maybe_unused]] const execution::ModifyTarget& target,
        [[maybe_unused]] const base::Rid rid,
        [[maybe_unused]] std::span<const types::Value> old_values
    ) -> base::Status override {
        // LAB-S02.Q09:R12-BEGIN
        base::lab::missing_algorithm("S02.Q09:R12");
        // LAB-S02.Q09:R12-END
    }

private:
    struct IndexTarget final {
        access::BtreeRelation relation;
        std::vector<std::size_t> positions;
    };

    [[maybe_unused]] static auto validate_target_descriptor(
        [[maybe_unused]] const catalog::TableMetadata& table,
        [[maybe_unused]] const execution::ModifyTarget& target
    ) -> base::Status {
        // LAB-S02.Q09:R2001-BEGIN
        base::lab::missing_algorithm("S02.Q09:R2001");
        // LAB-S02.Q09:R2001-END
    }

    [[maybe_unused]] auto prepare_target([[maybe_unused]] const execution::ModifyTarget& target)
        -> base::Status {
        // LAB-S02.Q09:R2002-BEGIN
        base::lab::missing_algorithm("S02.Q09:R2002");
        // LAB-S02.Q09:R2002-END
    }

    [[maybe_unused]] auto record_insert_ssi_conflicts(
        [[maybe_unused]] const transaction::TransactionBackend& backend,
        [[maybe_unused]] const base::Rid rid,
        [[maybe_unused]] const std::span<const types::Value> values
    ) const -> base::Status {
        // LAB-S09.Q05:R2001-BEGIN
        base::lab::missing_algorithm("S09.Q05:R2001");
        // LAB-S09.Q05:R2001-END
    }

    [[maybe_unused]] auto record_predicate_ssi_conflicts(
        [[maybe_unused]] const transaction::TransactionBackend& backend,
        [[maybe_unused]] const std::optional<std::span<const types::Value>> old_values,
        [[maybe_unused]] const std::optional<std::span<const types::Value>> new_values
    ) const -> base::Status {
        // LAB-S09.Q05:R2002-BEGIN
        base::lab::missing_algorithm("S09.Q05:R2002");
        // LAB-S09.Q05:R2002-END
    }

    [[maybe_unused]] auto maintain_indexes(
        [[maybe_unused]] std::span<const types::Value> values,
        [[maybe_unused]] const base::Rid rid,
        [[maybe_unused]] const transaction::TransactionBackend& backend
    ) -> base::Status {
        // LAB-S03.Q08:R2001-BEGIN
        base::lab::missing_algorithm("S03.Q08:R2001");
        // LAB-S03.Q08:R2001-END
    }

    [[maybe_unused]] auto indexed_columns_unchanged(
        [[maybe_unused]] const std::span<const types::Value> old_values,
        [[maybe_unused]] const std::span<const types::Value> new_values
    ) const noexcept -> bool {
        // LAB-S09.Q04:R13-BEGIN
        base::lab::missing_algorithm("S09.Q04:R13");
        // LAB-S09.Q04:R13-END
    }

    QueryExecutionContext& context_;
    std::optional<catalog::TableMetadata> table_;
    std::optional<access::TupleSchema> tuple_schema_;
    std::optional<access::HeapTable> heap_;
    std::vector<IndexTarget> index_targets_;
    std::optional<access::HeapIndexTidClassifier> classifier_;
    // The statement holds its catalog guard, and the owned descriptor is
    // immutable. Retaining a handle makes identity reuse safe even if a
    // caller replaces its target between synchronous port calls.
    execution::TupleDescriptorHandle validated_descriptor_;
    // Heap mutation and WAL logging consume the bytes before returning.
    std::vector<std::byte> encoded_row_;
};

}  // namespace

auto make_physical_modify_port(QueryExecutionContext& context)
    -> std::unique_ptr<execution::ModifyTablePort> {
    return std::make_unique<PhysicalModifyPort>(context);
}

}  // namespace db::engine::detail
