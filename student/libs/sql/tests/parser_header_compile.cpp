#include <type_traits>

#include "db/sql/bound_ast.hpp"
#include "db/sql/parser.hpp"
#include "db/sql/source_ast.hpp"

static_assert(std::is_trivially_copyable_v<db::sql::SourceSpan>);
static_assert(std::is_trivially_copyable_v<db::sql::SourcePosition>);
static_assert(std::is_trivially_copyable_v<db::sql::ParameterSlot>);
static_assert(std::is_trivially_copyable_v<db::sql::BoundRangeTableIndex>);
static_assert(std::is_trivially_copyable_v<db::sql::BoundColumnIndex>);
static_assert(std::is_move_constructible_v<db::sql::SourceScript>);
static_assert(!std::is_copy_constructible_v<db::sql::SourceScript>);
static_assert(std::is_move_constructible_v<db::sql::BoundScript>);
static_assert(!std::is_copy_constructible_v<db::sql::BoundScript>);
