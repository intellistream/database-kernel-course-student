#pragma once

#include <memory>
#include <optional>

#include "db/access/heap_scan.hpp"
#include "db/access/tuple_codec.hpp"
#include "db/execution/exec_context.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/transaction/ssi.hpp"

namespace db::engine::detail {

struct QueryExecutionContext;

struct LogicalScanSsi final {
    transaction::SsiPredicate predicate;
    std::shared_ptr<const transaction::SsiPredicateMatcher> matcher;
};

auto compile_logical_scan_filter(
    const planner::PlanExpressionPtr& filter,
    const QueryExecutionContext& context
) -> base::Result<std::optional<execution::ExpressionProgram>>;
auto make_logical_scan_ssi(
    const std::optional<execution::ExpressionProgram>& logical_filter,
    const execution::ExecContext& context,
    const execution::TupleDescriptorHandle& descriptor
) -> base::Result<LogicalScanSsi>;
auto make_heap_scan_qualification(
    access::TupleSchema tuple_schema,
    std::shared_ptr<const transaction::SsiPredicateMatcher> matcher
) -> std::unique_ptr<access::HeapScanQualificationPort>;
auto reject_doomed_serializable_transaction(QueryExecutionContext& context) -> base::Status;

}  // namespace db::engine::detail
