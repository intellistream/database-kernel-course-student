#include <cstddef>
#include <type_traits>
#include <variant>
#include <vector>

#include "db/base/lab_missing.hpp"
#include "db/planner/preprocess.hpp"

namespace db::planner {
namespace {

auto equal_expression_ptr(
    const PreprocessedExpressionPtr& left,
    const PreprocessedExpressionPtr& right
) -> bool;
auto equal_query_ptr(const PreprocessedQueryPtr& left, const PreprocessedQueryPtr& right) -> bool;
auto equal_table_ptr(
    const PreprocessedTableReferencePtr& left,
    const PreprocessedTableReferencePtr& right
) -> bool;

[[maybe_unused]] auto equal_expression_vector(
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& left,
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& right
) -> bool {
    // LAB-S04.Q01:R45-BEGIN
    base::lab::missing_algorithm("S04.Q01:R45");
    // LAB-S04.Q01:R45-END
}

[[maybe_unused]] auto equal_expression_ptr(
    [[maybe_unused]] const PreprocessedExpressionPtr& left,
    [[maybe_unused]] const PreprocessedExpressionPtr& right
) -> bool {
    // LAB-S04.Q01:R46-BEGIN
    base::lab::missing_algorithm("S04.Q01:R46");
    // LAB-S04.Q01:R46-END
}

[[maybe_unused]] auto equal_query_columns(
    [[maybe_unused]] const std::vector<QueryColumn>& left,
    [[maybe_unused]] const std::vector<QueryColumn>& right
) -> bool {
    // LAB-S04.Q01:R47-BEGIN
    base::lab::missing_algorithm("S04.Q01:R47");
    // LAB-S04.Q01:R47-END
}

[[maybe_unused]] auto equal_table_ptr(
    [[maybe_unused]] const PreprocessedTableReferencePtr& left,
    [[maybe_unused]] const PreprocessedTableReferencePtr& right
) -> bool {
    // LAB-S04.Q01:R48-BEGIN
    base::lab::missing_algorithm("S04.Q01:R48");
    // LAB-S04.Q01:R48-END
}

[[maybe_unused]] auto equal_query_ptr(
    [[maybe_unused]] const PreprocessedQueryPtr& left,
    [[maybe_unused]] const PreprocessedQueryPtr& right
) -> bool {
    // LAB-S04.Q01:R49-BEGIN
    base::lab::missing_algorithm("S04.Q01:R49");
    // LAB-S04.Q01:R49-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryLiteralExpression& left,
    [[maybe_unused]] const QueryLiteralExpression& right
) -> bool {
    // LAB-S04.Q01:R50-BEGIN
    base::lab::missing_algorithm("S04.Q01:R50");
    // LAB-S04.Q01:R50-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryColumnReference& left,
    [[maybe_unused]] const QueryColumnReference& right
) -> bool {
    // LAB-S04.Q01:R51-BEGIN
    base::lab::missing_algorithm("S04.Q01:R51");
    // LAB-S04.Q01:R51-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryParameterReference& left,
    [[maybe_unused]] const QueryParameterReference& right
) -> bool {
    // LAB-S04.Q01:R52-BEGIN
    base::lab::missing_algorithm("S04.Q01:R52");
    // LAB-S04.Q01:R52-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryExecutorParameterReference& left,
    [[maybe_unused]] const QueryExecutorParameterReference& right
) -> bool {
    // LAB-S04.Q01:R53-BEGIN
    base::lab::missing_algorithm("S04.Q01:R53");
    // LAB-S04.Q01:R53-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryUnaryExpression& left,
    [[maybe_unused]] const QueryUnaryExpression& right
) -> bool {
    // LAB-S04.Q01:R54-BEGIN
    base::lab::missing_algorithm("S04.Q01:R54");
    // LAB-S04.Q01:R54-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryBinaryExpression& left,
    [[maybe_unused]] const QueryBinaryExpression& right
) -> bool {
    // LAB-S04.Q01:R55-BEGIN
    base::lab::missing_algorithm("S04.Q01:R55");
    // LAB-S04.Q01:R55-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryBooleanExpression& left,
    [[maybe_unused]] const QueryBooleanExpression& right
) -> bool {
    // LAB-S04.Q01:R56-BEGIN
    base::lab::missing_algorithm("S04.Q01:R56");
    // LAB-S04.Q01:R56-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryIsNullExpression& left,
    [[maybe_unused]] const QueryIsNullExpression& right
) -> bool {
    // LAB-S04.Q01:R57-BEGIN
    base::lab::missing_algorithm("S04.Q01:R57");
    // LAB-S04.Q01:R57-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryInListExpression& left,
    [[maybe_unused]] const QueryInListExpression& right
) -> bool {
    // LAB-S04.Q01:R58-BEGIN
    base::lab::missing_algorithm("S04.Q01:R58");
    // LAB-S04.Q01:R58-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryBetweenExpression& left,
    [[maybe_unused]] const QueryBetweenExpression& right
) -> bool {
    // LAB-S04.Q01:R59-BEGIN
    base::lab::missing_algorithm("S04.Q01:R59");
    // LAB-S04.Q01:R59-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryCastExpression& left,
    [[maybe_unused]] const QueryCastExpression& right
) -> bool {
    // LAB-S04.Q01:R60-BEGIN
    base::lab::missing_algorithm("S04.Q01:R60");
    // LAB-S04.Q01:R60-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryAggregateExpression& left,
    [[maybe_unused]] const QueryAggregateExpression& right
) -> bool {
    // LAB-S04.Q01:R61-BEGIN
    base::lab::missing_algorithm("S04.Q01:R61");
    // LAB-S04.Q01:R61-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryScalarSubqueryExpression& left,
    [[maybe_unused]] const QueryScalarSubqueryExpression& right
) -> bool {
    // LAB-S04.Q01:R62-BEGIN
    base::lab::missing_algorithm("S04.Q01:R62");
    // LAB-S04.Q01:R62-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryExistsExpression& left,
    [[maybe_unused]] const QueryExistsExpression& right
) -> bool {
    // LAB-S04.Q01:R63-BEGIN
    base::lab::missing_algorithm("S04.Q01:R63");
    // LAB-S04.Q01:R63-END
}

[[maybe_unused]] auto equal_query_expression_node(
    [[maybe_unused]] const QueryInSubqueryExpression& left,
    [[maybe_unused]] const QueryInSubqueryExpression& right
) -> bool {
    // LAB-S04.Q01:R64-BEGIN
    base::lab::missing_algorithm("S04.Q01:R64");
    // LAB-S04.Q01:R64-END
}

[[maybe_unused]] auto expression_node_equal(
    [[maybe_unused]] const PreprocessedExpression& left,
    [[maybe_unused]] const PreprocessedExpression& right
) -> bool {
    // LAB-S04.Q01:R65-BEGIN
    base::lab::missing_algorithm("S04.Q01:R65");
    // LAB-S04.Q01:R65-END
}
}  // namespace

[[maybe_unused]] auto structurally_equal(
    [[maybe_unused]] const PreprocessedExpression& left,
    [[maybe_unused]] const PreprocessedExpression& right
) -> bool {
    // LAB-S04.Q01:R66-BEGIN
    base::lab::missing_algorithm("S04.Q01:R66");
    // LAB-S04.Q01:R66-END
}
}  // namespace db::planner
