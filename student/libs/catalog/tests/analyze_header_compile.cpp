#include <type_traits>

#include "db/catalog/analyze.hpp"

static_assert(std::is_move_constructible_v<db::catalog::AnalyzeAccumulator>);
