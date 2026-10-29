#include <algorithm>
#include <cstdint>
#include <variant>
#include <vector>

#include "db/base/lab_missing.hpp"
#include "db/planner/preprocess.hpp"

#include "preprocess_internal.hpp"

namespace db::planner {
namespace {

enum class ExpressionFeature : std::uint8_t {
    kAggregate,
    kOuterReference,
    kSubquery,
};

auto expression_has_feature(
    const PreprocessedExpression& expression,
    ExpressionFeature feature
) noexcept -> bool;

[[maybe_unused]] auto expression_pointer_has_feature(
    [[maybe_unused]] const PreprocessedExpressionPtr& expression,
    [[maybe_unused]] const ExpressionFeature feature
) noexcept -> bool {
    // LAB-S04.Q01:R67-BEGIN
    base::lab::missing_algorithm("S04.Q01:R67");
    // LAB-S04.Q01:R67-END
}

[[maybe_unused]] auto expression_list_has_feature(
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& expressions,
    [[maybe_unused]] const ExpressionFeature feature
) noexcept -> bool {
    // LAB-S04.Q01:R68-BEGIN
    base::lab::missing_algorithm("S04.Q01:R68");
    // LAB-S04.Q01:R68-END
}

[[maybe_unused]] auto expression_has_feature(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const ExpressionFeature feature
) noexcept -> bool {
    // LAB-S04.Q01:R69-BEGIN
    base::lab::missing_algorithm("S04.Q01:R69");
    // LAB-S04.Q01:R69-END
}
}  // namespace

namespace detail {

[[maybe_unused]] auto
expression_has_outer_reference([[maybe_unused]] const PreprocessedExpression& expression) noexcept
    -> bool {
    // LAB-S04.Q01:R70-BEGIN
    base::lab::missing_algorithm("S04.Q01:R70");
    // LAB-S04.Q01:R70-END
}
}  // namespace detail

[[maybe_unused]] auto
expression_has_aggregate([[maybe_unused]] const PreprocessedExpression& expression) noexcept
    -> bool {
    // LAB-S04.Q01:R71-BEGIN
    base::lab::missing_algorithm("S04.Q01:R71");
    // LAB-S04.Q01:R71-END
}

[[maybe_unused]] auto
expression_has_subquery([[maybe_unused]] const PreprocessedExpression& expression) noexcept
    -> bool {
    // LAB-S04.Q01:R72-BEGIN
    base::lab::missing_algorithm("S04.Q01:R72");
    // LAB-S04.Q01:R72-END
}
}  // namespace db::planner
