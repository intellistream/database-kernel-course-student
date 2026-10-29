#include "storage_query_binding.hpp"

#include <utility>
#include <vector>

#include "db/base/lab_missing.hpp"

#include "query_error.hpp"
#include "session_access.hpp"

namespace db::engine::detail {

[[maybe_unused]] auto
table_for([[maybe_unused]] catalog::CatalogStore& catalog, [[maybe_unused]] const base::Oid oid)
    -> base::Result<catalog::TableMetadata> {
    // LAB-S02.Q11:R71-BEGIN
    base::lab::missing_algorithm("S02.Q11:R71");
    // LAB-S02.Q11:R71-END
}

auto tuple_schema_for(const catalog::Schema& schema) -> base::Result<access::TupleSchema> {
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

[[maybe_unused]] auto descriptor_for([[maybe_unused]] const catalog::Schema& schema)
    -> base::Result<execution::TupleDescriptorHandle> {
    // LAB-S02.Q11:R72-BEGIN
    base::lab::missing_algorithm("S02.Q11:R72");
    // LAB-S02.Q11:R72-END
}

[[maybe_unused]] auto require_backend([[maybe_unused]] QueryExecutionContext& context)
    -> base::Result<std::reference_wrapper<transaction::TransactionBackend>> {
    // LAB-S02.Q11:R73-BEGIN
    base::lab::missing_algorithm("S02.Q11:R73");
    // LAB-S02.Q11:R73-END
}

[[maybe_unused]] auto require_snapshot([[maybe_unused]] QueryExecutionContext& context)
    -> base::Result<std::reference_wrapper<const transaction::Snapshot>> {
    // LAB-S02.Q11:R74-BEGIN
    base::lab::missing_algorithm("S02.Q11:R74");
    // LAB-S02.Q11:R74-END
}
}  // namespace db::engine::detail
