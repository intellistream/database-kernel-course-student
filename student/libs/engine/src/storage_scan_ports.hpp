#pragma once

#include <memory>
#include <optional>

#include "db/execution/expression.hpp"
#include "db/execution/scan_port.hpp"

namespace db::engine::detail {

struct QueryExecutionContext;

// Returned ports borrow the query context and services through tree destruction.
auto make_heap_seq_scan_port(
    QueryExecutionContext& context,
    std::optional<execution::ExpressionProgram> logical_filter,
    bool projected
) -> std::unique_ptr<execution::SeqScanPort>;
auto make_heap_index_scan_port(
    QueryExecutionContext& context,
    std::optional<execution::ExpressionProgram> logical_filter,
    bool projected
) -> std::unique_ptr<execution::IndexScanPort>;

}  // namespace db::engine::detail
