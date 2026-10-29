#pragma once

#include <utility>
#include <vector>

#include "db/access/heap_table.hpp"
#include "db/access/tuple_codec.hpp"
#include "db/catalog/schema.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/transaction/backend.hpp"
#include "db/transaction/manager.hpp"

#include "utility_executor.hpp"

namespace db::engine::detail {

struct UtilityExecutionState final {
    UtilityExecutionContext& context;
    transaction::TransactionBackend& backend;
    const transaction::Snapshot& snapshot;

    auto mark_write() const -> base::Status {
        return backend.mark_persistent_write();
    }

    auto remove_relation(const base::RelationId relation_id, const bool heap) const
        -> base::Status {
        auto removed = context.services.buffer_pool.remove_relation(relation_id, true);
        if (!removed) {
            return removed;
        }
        if (heap && context.services.heap_space_registry.contains(relation_id)) {
            auto unregistered = context.services.heap_space_registry.remove(relation_id);
            if (!unregistered && unregistered.error().code() != base::ErrorCode::kNotFound) {
                return unregistered;
            }
        }
        return {};
    }
};

inline auto make_tuple_schema(const catalog::Schema& schema) -> base::Result<access::TupleSchema> {
    std::vector<access::TupleColumn> columns;
    columns.reserve(schema.size());
    for (const auto& column : schema.columns()) {
        columns.push_back(
            access::TupleColumn{
                .name = column.name,
                .data_type = column.data_type,
            }
        );
    }
    return access::TupleSchema::create(std::move(columns));
}

}  // namespace db::engine::detail
