#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/planner/join_strategy.hpp"
#include "db/sql/ast_common.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {
namespace {

struct IndexKeySpec final {
    std::uint32_t column_index{0U};
    types::DataType data_type{types::kUnknownType};
};

auto relation(const std::uint32_t index) -> QueryRangeTableIndex {
    return QueryRangeTableIndex{index};
}

auto column(const std::uint32_t index) -> QueryColumnIndex {
    return QueryColumnIndex{index};
}

auto expression_ptr(PreprocessedExpression expression) -> PreprocessedExpressionPtr {
    return std::make_unique<PreprocessedExpression>(std::move(expression));
}

auto source_column(
    const std::uint32_t relation_index,
    const std::uint32_t column_index,
    const types::DataType data_type = types::kInt64Type
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        data_type,
        QueryColumnReference{
            .range_table_index = relation(relation_index),
            .column_index = column(column_index),
            .data_type = data_type,
            .outer_query_levels = 0U,
        },
    };
}

auto source_binary(
    const types::BinaryOperator op,
    PreprocessedExpressionPtr left,
    PreprocessedExpressionPtr right
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryBinaryExpression{
            .op = op,
            .left = std::move(left),
            .right = std::move(right),
        },
    };
}

auto source_equal(PreprocessedExpressionPtr left, PreprocessedExpressionPtr right)
    -> PreprocessedExpression {
    return source_binary(types::BinaryOperator::kEqual, std::move(left), std::move(right));
}

auto source_less(PreprocessedExpressionPtr left, PreprocessedExpressionPtr right)
    -> PreprocessedExpression {
    return source_binary(types::BinaryOperator::kLess, std::move(left), std::move(right));
}

auto base_paths(
    const std::uint32_t relation_index,
    const std::uint32_t oid,
    const std::initializer_list<types::DataType> column_types = {types::kInt64Type}
) -> BaseRelationPathList {
    PlanSchema schema{};
    schema.columns.reserve(column_types.size());
    std::uint32_t column_index = 0U;
    for (const types::DataType data_type : column_types) {
        schema.columns.push_back(
            PlanOutputColumn{
                .name = "c" + std::to_string(column_index),
                .data_type = data_type,
                .origin = PlanColumnOrigin{
                    .relation_oid = base::Oid{oid},
                    .relation_column = PlanColumnIndex{column_index},
                },
            }
        );
        ++column_index;
    }
    return BaseRelationPathList{
        .catalog_generation = 71U,
        .range_table_index = relation(relation_index),
        .relation =
            PlanRelation{
                .oid = base::Oid{oid},
                .name = "t" + std::to_string(relation_index),
                .alias = std::nullopt,
                .schema = std::move(schema),
            },
        .paths = {SequentialScanPath{}},
        .selected_path_index = 0U,
    };
}

auto make_index(
    const std::uint32_t oid,
    std::string name,
    const std::initializer_list<IndexKeySpec> keys,
    const bool unique = false
) -> PlanIndex {
    PlanIndex result{
        .oid = base::Oid{oid},
        .name = std::move(name),
        .keys = {},
        .unique = unique,
    };
    result.keys.reserve(keys.size());
    for (const IndexKeySpec& key : keys) {
        result.keys.push_back(
            PlanIndexKey{
                .relation_column = PlanColumnIndex{key.column_index},
                .column_name = "c" + std::to_string(key.column_index),
                .data_type = key.data_type,
            }
        );
    }
    return result;
}

auto equality_bounds(const std::int64_t value) -> IndexPathBounds {
    return IndexPathBounds{
        .equality_prefix = {IndexPathLiteralBoundValue{.value = types::Value::integer(value)}},
    };
}

void append_index_path(
    BaseRelationPathList& paths,
    PlanIndex index,
    IndexPathBounds bounds = {},
    const std::size_t domain_restriction_count = 0U
) {
    paths.paths.emplace_back(
        IndexScanPath{
            .index = std::move(index),
            .bounds = std::move(bounds),
            .domain_restriction_count = domain_restriction_count,
            .residual_source_clause_count = 0U,
        }
    );
}

auto qualification(
    const std::size_t source_clause_index,
    const std::size_t conjunct_index,
    const std::initializer_list<std::uint32_t> relation_indexes
) -> JoinQualification {
    JoinQualification result{};
    result.source = JoinQualificationSource{
        .source_clause_index = source_clause_index,
        .conjunct_index = conjunct_index,
    };
    result.required_relations.reserve(relation_indexes.size());
    for (const std::uint32_t relation_index : relation_indexes) {
        result.required_relations.push_back(relation(relation_index));
    }
    return result;
}

auto empty_parameter_namespace() -> ParameterNamespace {
    auto result = ParameterNamespace::from_external_slots({});
    EXPECT_TRUE(result) << result.error().to_string();
    return std::move(result).value();
}

TEST(JoinStrategyTest, RejectsAnInactiveParameterNamespaceBeforeChoosingAnOrder) {
    ParameterNamespace source = empty_parameter_namespace();
    ParameterNamespace moved{std::move(source)};
    EXPECT_TRUE(moved.valid());

    const std::vector<BaseRelationPathList> relations;
    const std::vector<JoinQualification> qualifications;
    const std::vector<const PreprocessedExpression*> source_clauses;
    // Passing the documented inert source verifies the public consumer boundary.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    const auto result =
        choose_small_inner_join_strategies(relations, qualifications, source_clauses, source);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

TEST(JoinStrategyTest, SelectsParameterizedIndexNestedLoopAndRetainsLookupEquality) {
    auto outer = base_paths(0U, 100U);
    append_index_path(
        outer,
        make_index(10U, "outer_c0_idx", {{0U, types::kInt64Type}}),
        equality_bounds(7),
        1U
    );
    outer.selected_path_index = 1U;

    auto inner = base_paths(1U, 101U);
    append_index_path(inner, make_index(11U, "inner_c0_idx", {{0U, types::kInt64Type}}));
    const auto third = base_paths(2U, 102U);
    const std::vector paths{outer, inner, third};

    PreprocessedExpression first =
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)));
    PreprocessedExpression second =
        source_equal(expression_ptr(source_column(1U, 0U)), expression_ptr(source_column(2U, 0U)));
    const std::vector<const PreprocessedExpression*> sources{&first, &second};
    const std::vector qualifications{
        qualification(0U, 0U, {0U, 1U}),
        qualification(1U, 0U, {1U, 2U}),
    };
    ParameterNamespace parameters = empty_parameter_namespace();

    const auto result =
        choose_small_inner_join_strategies(paths, qualifications, sources, parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->steps.size(), 3U);
    ASSERT_TRUE(result->steps[1U].strategy.has_value());
    EXPECT_EQ(
        join_strategy_kind(*result->steps[1U].strategy),
        JoinStrategyKind::kParameterizedIndexNestedLoop
    );
    const auto* const index_nested_loop =
        std::get_if<ParameterizedIndexNestedLoopJoinStrategy>(&*result->steps[1U].strategy);
    ASSERT_NE(index_nested_loop, nullptr);
    EXPECT_EQ(index_nested_loop->inner_path_index, 1U);
    EXPECT_EQ(index_nested_loop->inner_path.index.oid, base::Oid{11U});
    ASSERT_EQ(index_nested_loop->inner_path.bounds.equality_prefix.size(), 1U);
    const auto* const path_parameter = std::get_if<IndexPathPlanParameterBoundValue>(
        &index_nested_loop->inner_path.bounds.equality_prefix.front()
    );
    ASSERT_NE(path_parameter, nullptr);
    EXPECT_EQ(path_parameter->reference.kind, PlanParameterKind::kExecutor);
    EXPECT_EQ(index_nested_loop->inner_path.domain_restriction_count, 1U);
    ASSERT_EQ(index_nested_loop->keys.size(), 1U);
    EXPECT_EQ(index_nested_loop->keys[0U].inner_key_index, PlanIndexKeyIndex{0U});
    EXPECT_EQ(index_nested_loop->keys[0U].parameter.kind, PlanParameterKind::kExecutor);
    EXPECT_EQ(index_nested_loop->keys[0U].parameter.id, PlanParameterId{0U});
    const std::vector<JoinQualificationSource> expected_index_nested_loop_residuals{
        JoinQualificationSource{0U, 0U}
    };
    EXPECT_EQ(result->steps[1U].residual_qualifications, expected_index_nested_loop_residuals);
    ASSERT_EQ(parameters.parameters().size(), 1U);
    EXPECT_EQ(parameters.parameters()[0U].kind, PlanParameterKind::kExecutor);
    ASSERT_TRUE(result->steps[2U].strategy.has_value());
    EXPECT_EQ(join_strategy_kind(*result->steps[2U].strategy), JoinStrategyKind::kHashJoin);
}

TEST(JoinStrategyTest, SelectsMergeJoinForOrderedIndexesWithOneBoundedInput) {
    auto outer = base_paths(0U, 100U);
    append_index_path(outer, make_index(10U, "outer_c0_idx", {{0U, types::kInt64Type}}));

    auto inner = base_paths(1U, 101U, {types::kInt64Type, types::kInt64Type});
    append_index_path(
        inner,
        make_index(11U, "inner_tenant_c0_idx", {{0U, types::kInt64Type}, {1U, types::kInt64Type}}),
        equality_bounds(7),
        1U
    );
    const auto third = base_paths(2U, 102U);
    const std::vector paths{outer, inner, third};

    PreprocessedExpression first =
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 1U)));
    PreprocessedExpression second =
        source_equal(expression_ptr(source_column(1U, 1U)), expression_ptr(source_column(2U, 0U)));
    const std::vector<const PreprocessedExpression*> sources{&first, &second};
    const std::vector qualifications{
        qualification(0U, 0U, {0U, 1U}),
        qualification(1U, 0U, {1U, 2U}),
    };
    ParameterNamespace parameters = empty_parameter_namespace();

    const auto result =
        choose_small_inner_join_strategies(paths, qualifications, sources, parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_TRUE(result->steps[1U].strategy.has_value());
    EXPECT_EQ(join_strategy_kind(*result->steps[1U].strategy), JoinStrategyKind::kMergeJoin);
    const auto* const merge = std::get_if<MergeJoinStrategy>(&*result->steps[1U].strategy);
    ASSERT_NE(merge, nullptr);
    EXPECT_EQ(merge->outer_path_index, 1U);
    EXPECT_EQ(merge->inner_path_index, 1U);
    EXPECT_EQ(merge->outer_path.index.oid, base::Oid{10U});
    EXPECT_EQ(merge->inner_path.index.oid, base::Oid{11U});
    ASSERT_EQ(merge->inner_path.bounds.equality_prefix.size(), 1U);
    ASSERT_EQ(merge->keys.size(), 1U);
    EXPECT_EQ(merge->keys[0U].outer.column_index, QueryColumnIndex{0U});
    EXPECT_EQ(merge->keys[0U].inner.column_index, QueryColumnIndex{1U});
    EXPECT_TRUE(result->steps[1U].residual_qualifications.empty());
    EXPECT_TRUE(parameters.parameters().empty());
}

TEST(JoinStrategyTest, SelectsHashJoinAndLeavesOnlyNonKeyResiduals) {
    const std::vector paths{
        base_paths(0U, 100U),
        base_paths(1U, 101U),
        base_paths(2U, 102U),
    };
    PreprocessedExpression equal =
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)));
    PreprocessedExpression less =
        source_less(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)));
    PreprocessedExpression final_equal =
        source_equal(expression_ptr(source_column(1U, 0U)), expression_ptr(source_column(2U, 0U)));
    const std::vector<const PreprocessedExpression*> sources{&equal, &less, &final_equal};
    const std::vector qualifications{
        qualification(0U, 0U, {0U, 1U}),
        qualification(1U, 0U, {0U, 1U}),
        qualification(2U, 0U, {1U, 2U}),
    };
    ParameterNamespace parameters = empty_parameter_namespace();

    const auto result =
        choose_small_inner_join_strategies(paths, qualifications, sources, parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_TRUE(result->steps[1U].strategy.has_value());
    EXPECT_EQ(join_strategy_kind(*result->steps[1U].strategy), JoinStrategyKind::kHashJoin);
    const auto* const hash = std::get_if<HashJoinStrategy>(&*result->steps[1U].strategy);
    ASSERT_NE(hash, nullptr);
    ASSERT_EQ(hash->keys.size(), 1U);
    EXPECT_EQ(hash->keys[0U].source, JoinQualificationSource({0U, 0U}));
    const std::vector<JoinQualificationSource> expected_hash_residuals{
        JoinQualificationSource{1U, 0U}
    };
    EXPECT_EQ(result->steps[1U].residual_qualifications, expected_hash_residuals);
    ASSERT_TRUE(result->steps[2U].strategy.has_value());
    EXPECT_EQ(join_strategy_kind(*result->steps[2U].strategy), JoinStrategyKind::kHashJoin);
    EXPECT_TRUE(parameters.parameters().empty());
}

TEST(JoinStrategyTest, FallsBackToNestedLoopForFloatEquality) {
    const std::vector paths{
        base_paths(0U, 100U, {types::kFloat32Type}),
        base_paths(1U, 101U, {types::kFloat32Type}),
        base_paths(2U, 102U, {types::kFloat32Type}),
    };
    PreprocessedExpression first = source_equal(
        expression_ptr(source_column(0U, 0U, types::kFloat32Type)),
        expression_ptr(source_column(1U, 0U, types::kFloat32Type))
    );
    PreprocessedExpression second = source_equal(
        expression_ptr(source_column(1U, 0U, types::kFloat32Type)),
        expression_ptr(source_column(2U, 0U, types::kFloat32Type))
    );
    const std::vector<const PreprocessedExpression*> sources{&first, &second};
    const std::vector qualifications{
        qualification(0U, 0U, {0U, 1U}),
        qualification(1U, 0U, {1U, 2U}),
    };
    ParameterNamespace parameters = empty_parameter_namespace();

    const auto result =
        choose_small_inner_join_strategies(paths, qualifications, sources, parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_TRUE(result->steps[1U].strategy.has_value());
    EXPECT_EQ(join_strategy_kind(*result->steps[1U].strategy), JoinStrategyKind::kNestedLoop);
    const std::vector<JoinQualificationSource> expected_nested_loop_residuals{
        JoinQualificationSource{0U, 0U}
    };
    EXPECT_EQ(result->steps[1U].residual_qualifications, expected_nested_loop_residuals);
    ASSERT_TRUE(result->steps[2U].strategy.has_value());
    EXPECT_EQ(join_strategy_kind(*result->steps[2U].strategy), JoinStrategyKind::kNestedLoop);
    EXPECT_TRUE(parameters.parameters().empty());
}

TEST(JoinStrategyTest, PrefersUniqueParameterizedIndexThenStableIdentity) {
    auto outer = base_paths(0U, 100U);
    append_index_path(
        outer,
        make_index(10U, "outer_c0_idx", {{0U, types::kInt64Type}}),
        equality_bounds(7),
        1U
    );
    outer.selected_path_index = 1U;
    auto inner = base_paths(1U, 101U);
    append_index_path(inner, make_index(1U, "inner_non_unique_idx", {{0U, types::kInt64Type}}));
    append_index_path(inner, make_index(9U, "inner_unique_idx", {{0U, types::kInt64Type}}, true));
    const std::vector paths{outer, inner, base_paths(2U, 102U)};
    PreprocessedExpression source =
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)));
    const std::vector<const PreprocessedExpression*> sources{&source};
    const std::vector qualifications{qualification(0U, 0U, {0U, 1U})};
    ParameterNamespace parameters = empty_parameter_namespace();

    const auto result =
        choose_small_inner_join_strategies(paths, qualifications, sources, parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    const auto* const index_nested_loop =
        std::get_if<ParameterizedIndexNestedLoopJoinStrategy>(&*result->steps[1U].strategy);
    ASSERT_NE(index_nested_loop, nullptr);
    EXPECT_EQ(index_nested_loop->inner_path_index, 2U);
    EXPECT_EQ(index_nested_loop->inner_path.index.oid, base::Oid{9U});
}

TEST(JoinStrategyTest, ReusesOneExecutorParameterForRepeatedOuterColumn) {
    auto outer = base_paths(0U, 100U);
    append_index_path(
        outer,
        make_index(10U, "outer_c0_idx", {{0U, types::kInt64Type}}),
        equality_bounds(7),
        1U
    );
    outer.selected_path_index = 1U;
    auto inner = base_paths(1U, 101U, {types::kInt64Type, types::kInt64Type});
    append_index_path(
        inner,
        make_index(11U, "inner_c0_c1_idx", {{0U, types::kInt64Type}, {1U, types::kInt64Type}})
    );
    const std::vector paths{outer, inner, base_paths(2U, 102U)};
    PreprocessedExpression first =
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)));
    PreprocessedExpression second =
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 1U)));
    const std::vector<const PreprocessedExpression*> sources{&first, &second};
    const std::vector qualifications{
        qualification(0U, 0U, {0U, 1U}),
        qualification(1U, 0U, {0U, 1U}),
    };
    ParameterNamespace parameters = empty_parameter_namespace();

    const auto result =
        choose_small_inner_join_strategies(paths, qualifications, sources, parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    const auto* const index_nested_loop =
        std::get_if<ParameterizedIndexNestedLoopJoinStrategy>(&*result->steps[1U].strategy);
    ASSERT_NE(index_nested_loop, nullptr);
    ASSERT_EQ(index_nested_loop->keys.size(), 2U);
    EXPECT_EQ(index_nested_loop->keys[0U].parameter, index_nested_loop->keys[1U].parameter);
    ASSERT_EQ(parameters.parameters().size(), 1U);
}

TEST(JoinStrategyTest, RejectsMismatchedSourceWithoutAllocatingParameters) {
    auto outer = base_paths(0U, 100U);
    append_index_path(
        outer,
        make_index(10U, "outer_c0_idx", {{0U, types::kInt64Type}}),
        equality_bounds(7),
        1U
    );
    outer.selected_path_index = 1U;
    auto inner = base_paths(1U, 101U);
    append_index_path(inner, make_index(11U, "inner_c0_idx", {{0U, types::kInt64Type}}));
    const std::vector paths{outer, inner, base_paths(2U, 102U)};
    PreprocessedExpression source =
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)));
    const std::vector<const PreprocessedExpression*> sources{&source};
    const std::vector qualifications{qualification(0U, 1U, {0U, 1U})};
    ParameterNamespace parameters = empty_parameter_namespace();

    const auto result =
        choose_small_inner_join_strategies(paths, qualifications, sources, parameters);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kPlan);
    EXPECT_TRUE(parameters.parameters().empty());
}

TEST(JoinStrategyTest, RejectsForgedIndexKeyMetadataWithoutAllocatingParameters) {
    auto outer = base_paths(0U, 100U);
    append_index_path(
        outer,
        make_index(10U, "outer_c0_idx", {{0U, types::kInt64Type}}),
        equality_bounds(7),
        1U
    );
    outer.selected_path_index = 1U;
    auto inner = base_paths(1U, 101U);
    PlanIndex forged_inner = make_index(11U, "inner_c0_idx", {{0U, types::kInt64Type}});
    forged_inner.keys[0U].column_name = "forged";
    append_index_path(inner, std::move(forged_inner));
    const std::vector paths{outer, inner, base_paths(2U, 102U)};
    PreprocessedExpression source =
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)));
    const std::vector<const PreprocessedExpression*> sources{&source};
    const std::vector qualifications{qualification(0U, 0U, {0U, 1U})};
    ParameterNamespace parameters = empty_parameter_namespace();

    const auto result =
        choose_small_inner_join_strategies(paths, qualifications, sources, parameters);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kPlan);
    EXPECT_TRUE(parameters.parameters().empty());
}

}  // namespace
}  // namespace db::planner
