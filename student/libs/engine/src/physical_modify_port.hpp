#pragma once

#include <memory>

#include "db/execution/modify_table.hpp"

namespace db::engine::detail {

struct QueryExecutionContext;

// The factory owns this port; every ModifyTableState borrows it until destruction.
auto make_physical_modify_port(QueryExecutionContext& context)
    -> std::unique_ptr<execution::ModifyTablePort>;

}  // namespace db::engine::detail
