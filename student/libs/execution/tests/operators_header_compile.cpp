#include <memory>
#include <type_traits>

#include "db/execution/operators.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::ValuesState>);
static_assert(!std::is_copy_constructible_v<db::execution::ResultState>);
static_assert(!std::is_copy_constructible_v<db::execution::SeqScanState>);
static_assert(!std::is_copy_constructible_v<db::execution::IndexScanState>);
static_assert(!std::is_copy_constructible_v<db::execution::ScanPort>);
