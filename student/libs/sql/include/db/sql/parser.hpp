#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/sql/ast_common.hpp"
#include "db/sql/source_ast.hpp"

namespace db::sql {

struct SourcePosition final {
    std::size_t line;
    std::size_t column;

    constexpr auto operator==(const SourcePosition&) const -> bool = default;
};

// ParseLimits bounds both the third-party parser entry and DB-owned
// projection output. The defaults are deliberately finite because the raw
// parser's allocator is otherwise unbounded by a request contract.
struct ParseLimits final {
    std::size_t maximum_source_bytes{1U << 20U};
    std::size_t maximum_statements{256U};
    std::size_t maximum_identifier_bytes{256U};
};

// Parses through the private raw core and returns only DB-owned data.
// SourceScript is move-only because no parse-tree node may borrow the raw
// libpg_query arena after this function returns.
auto parse_sql(std::string_view sql, const ParseLimits& limits = {}) -> base::Result<SourceScript>;

// Recognizes exactly the standalone extension, including SQL whitespace and
// comments. The engine uses this before opening an implicit transaction.
auto is_static_checkpoint_statement(std::string_view sql) -> bool;

auto source_position(std::string_view sql, std::size_t byte_offset) -> base::Result<SourcePosition>;

}  // namespace db::sql
