#pragma once

#include <functional>

#include "db/access/tuple_codec.hpp"
#include "db/catalog/store.hpp"
#include "db/execution/tuple.hpp"
#include "db/transaction/backend.hpp"
#include "db/transaction/manager.hpp"

#include "query_executor.hpp"

namespace db::engine::detail {

auto table_for(catalog::CatalogStore& catalog, base::Oid oid)
    -> base::Result<catalog::TableMetadata>;

auto tuple_schema_for(const catalog::Schema& schema) -> base::Result<access::TupleSchema>;

auto descriptor_for(const catalog::Schema& schema)
    -> base::Result<execution::TupleDescriptorHandle>;

auto require_backend(QueryExecutionContext& context)
    -> base::Result<std::reference_wrapper<transaction::TransactionBackend>>;

auto require_snapshot(QueryExecutionContext& context)
    -> base::Result<std::reference_wrapper<const transaction::Snapshot>>;

inline auto serializable_conflicts(
    QueryExecutionContext& context,
    const transaction::TransactionBackend& backend
) noexcept -> transaction::SerializableConflictManager* {
    return backend.isolation() == transaction::IsolationLevel::kSerializable
               ? &context.services.transaction_manager.ssi_manager()
               : nullptr;
}

}  // namespace db::engine::detail
