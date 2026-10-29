#pragma once

#include <span>

#include "db/base/result.hpp"
#include "db/sql/bound_ast.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/source_ast.hpp"

namespace db::sql {

// Binds one wholly parser-owned script against one immutable catalog read view.
// The port must remain valid for this call only: BoundScript copies every table
// descriptor it retains and never borrows the port or its catalog generation.
auto bind_script(const CatalogReadPort& catalog, const SourceScript& source)
    -> base::Result<BoundScript>;

// Binds a parser-owned script for a prepared-statement caller. Parameter
// slots retain PostgreSQL's one-based `$n` spelling, while parameter_types is
// indexed by the corresponding zero-based position. The span need only remain
// valid for this call: BoundScript copies every resolved type and occurrence.
auto bind_script_with_parameters(
    const CatalogReadPort& catalog,
    const SourceScript& source,
    std::span<const types::DataType> parameter_types
) -> base::Result<BoundScript>;

}  // namespace db::sql
