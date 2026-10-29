#include "maintenance_executor.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "db/access/btree_relation.hpp"
#include "db/access/heap_page.hpp"
#include "db/access/heap_scan.hpp"
#include "db/access/heap_table.hpp"
#include "db/base/lab_missing.hpp"
#include "db/catalog/analyze.hpp"
#include "db/catalog/store.hpp"
#include "db/transaction/xid.hpp"

#include "utility_execution_state.hpp"

namespace db::engine::detail {

namespace {

[[maybe_unused]] auto maintenance_tables(
    [[maybe_unused]] catalog::CatalogStore& catalog,
    [[maybe_unused]] const std::optional<base::Oid> table_oid
) -> base::Result<std::vector<catalog::TableMetadata>> {
    // LAB-S09.Q07:R100901-BEGIN
    base::lab::missing_algorithm("S09.Q07:R100901");
    // LAB-S09.Q07:R100901-END
}

[[maybe_unused]] auto count_dead_rows_in_page(
    [[maybe_unused]] const UtilityExecutionState& state,
    [[maybe_unused]] const access::HeapPage& page
) -> base::Result<std::uint64_t> {
    // LAB-S09.Q07:R100902-BEGIN
    base::lab::missing_algorithm("S09.Q07:R100902");
    // LAB-S09.Q07:R100902-END
}

[[maybe_unused]] auto count_known_dead_rows(
    [[maybe_unused]] const UtilityExecutionState& state,
    [[maybe_unused]] const catalog::TableMetadata& metadata
) -> base::Result<std::uint64_t> {
    // LAB-S09.Q07:R100903-BEGIN
    base::lab::missing_algorithm("S09.Q07:R100903");
    // LAB-S09.Q07:R100903-END
}

[[maybe_unused]] auto analyze_table(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const catalog::TableMetadata& metadata
) -> base::Result<catalog::TableStatistics> {
    // LAB-S04.Q07:R15-BEGIN
    base::lab::missing_algorithm("S04.Q07:R15");
    // LAB-S04.Q07:R15-END
}

[[maybe_unused]] auto vacuum_index(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const catalog::TableMetadata& table,
    [[maybe_unused]] const catalog::IndexMetadata& index,
    [[maybe_unused]] const access::DeadTidStore& dead_tids
) -> base::Status {
    // LAB-S09.Q07:R100904-BEGIN
    base::lab::missing_algorithm("S09.Q07:R100904");
    // LAB-S09.Q07:R100904-END
}

[[maybe_unused]] auto vacuum_table(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const catalog::TableMetadata& table,
    [[maybe_unused]] const base::Xid oldest_xmin
) -> base::Status {
    // LAB-S09.Q07:R100905-BEGIN
    base::lab::missing_algorithm("S09.Q07:R100905");
    // LAB-S09.Q07:R100905-END
}
}  // namespace

[[maybe_unused]] auto execute_bound_utility(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const sql::BoundAnalyzeStatement& node
) -> base::Result<DispatchExecutionResult> {
    // LAB-S09.Q07:R100906-BEGIN
    base::lab::missing_algorithm("S09.Q07:R100906");
    // LAB-S09.Q07:R100906-END
}

[[maybe_unused]] auto execute_bound_utility(
    [[maybe_unused]] UtilityExecutionState& state,
    [[maybe_unused]] const sql::BoundVacuumStatement& node
) -> base::Result<DispatchExecutionResult> {
    // LAB-S09.Q07:R100907-BEGIN
    base::lab::missing_algorithm("S09.Q07:R100907");
    // LAB-S09.Q07:R100907-END
}
}  // namespace db::engine::detail
