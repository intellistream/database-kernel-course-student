#include "ddl_executor.hpp"

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/access/btree_key.hpp"
#include "db/access/btree_relation.hpp"
#include "db/access/heap_scan.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/tuple_codec.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/store.hpp"
#include "db/transaction/manager.hpp"

#include "utility_execution_state.hpp"

namespace db::engine::detail {

[[maybe_unused]] auto execute_bound_utility(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const sql::BoundCreateTableStatement& node
) -> base::Result<DispatchExecutionResult> {
    // LAB-S02.Q10:R65-BEGIN
    base::lab::missing_algorithm("S02.Q10:R65");
    // LAB-S02.Q10:R65-END
}

[[maybe_unused]] auto execute_bound_utility(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const sql::BoundDropTableStatement& node
) -> base::Result<DispatchExecutionResult> {
    // LAB-S02.Q10:R66-BEGIN
    base::lab::missing_algorithm("S02.Q10:R66");
    // LAB-S02.Q10:R66-END
}

[[maybe_unused]] auto collect_index_entries(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const catalog::TableMetadata& table_metadata,
    [[maybe_unused]] const access::BtreeKeyLayout& key_layout,
    [[maybe_unused]] const std::vector<std::size_t>& positions,
    [[maybe_unused]] const access::TupleSchema& tuple_schema
) -> base::Result<std::vector<access::BtreeBulkBuildEntry>> {
    // LAB-S03.Q08:R1011-BEGIN
    base::lab::missing_algorithm("S03.Q08:R1011");
    // LAB-S03.Q08:R1011-END
}

[[maybe_unused]] auto publish_index_relation(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const catalog::IndexMetadata& metadata,
    [[maybe_unused]] access::BtreeKeyLayout key_layout,
    [[maybe_unused]] const std::vector<access::BtreeBulkBuildEntry>& entries
) -> base::Result<DispatchExecutionResult> {
    // LAB-S03.Q08:R1012-BEGIN
    base::lab::missing_algorithm("S03.Q08:R1012");
    // LAB-S03.Q08:R1012-END
}

[[maybe_unused]] auto execute_bound_utility(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const sql::BoundCreateIndexStatement& node
) -> base::Result<DispatchExecutionResult> {
    // LAB-S02.Q10:R67-BEGIN
    base::lab::missing_algorithm("S02.Q10:R67");
    // LAB-S02.Q10:R67-END
}

[[maybe_unused]] auto execute_bound_utility(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const sql::BoundDropIndexStatement& node
) -> base::Result<DispatchExecutionResult> {
    // LAB-S02.Q10:R68-BEGIN
    base::lab::missing_algorithm("S02.Q10:R68");
    // LAB-S02.Q10:R68-END
}
}  // namespace db::engine::detail
