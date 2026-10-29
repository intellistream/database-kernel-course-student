#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/planner/equivalence.hpp"
#include "db/sql/ast_common.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {
namespace {

auto domain(std::initializer_list<std::uint32_t> indexes) -> InnerJoinDomain {
    InnerJoinDomain result{};
    result.range_table_indexes.reserve(indexes.size());
    for (const std::uint32_t index : indexes) {
        result.range_table_indexes.emplace_back(index);
    }
    return result;
}

auto column(
    const std::uint32_t range_table_index,
    const std::uint32_t column_index,
    const types::DataType data_type = types::kInt64Type
) -> EquivalenceMember {
    return EquivalenceColumn{
        .range_table_index = QueryRangeTableIndex{range_table_index},
        .column_index = QueryColumnIndex{column_index},
        .data_type = data_type,
    };
}

auto integer_literal(const std::int64_t value) -> EquivalenceMember {
    return EquivalenceLiteral{.value = types::Value::integer(value)};
}

auto integer_pseudoconstant(const std::int64_t value) -> EquivalencePseudoconstant {
    return EquivalenceLiteral{.value = types::Value::integer(value)};
}

auto parameter(const PlanParameterReference reference) -> EquivalenceMember {
    return EquivalenceParameter{.reference = reference};
}

auto parameter_pseudoconstant(const PlanParameterReference reference) -> EquivalencePseudoconstant {
    return EquivalenceParameter{.reference = reference};
}

auto parameter_namespace() -> base::Result<ParameterNamespace> {
    const std::vector external{
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{2U},
            .data_type = types::kFloat32Type,
        },
    };
    return ParameterNamespace::from_external_slots(external);
}

TEST(EquivalenceTest, RejectsAnInactiveParameterNamespaceAtBothPublicBoundaries) {
    auto created = parameter_namespace();
    ASSERT_TRUE(created) << created.error().to_string();
    ParameterNamespace source = std::move(created).value();
    ParameterNamespace moved{std::move(source)};
    EXPECT_TRUE(moved.valid());

    const std::vector<EquivalenceEquality> equalities;
    // Passing the documented inert source verifies the public consumer boundary.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    const auto built = build_inner_domain_equivalences(domain({0U}), equalities, source);
    ASSERT_FALSE(built);
    EXPECT_EQ(built.error().code(), base::ErrorCode::kUnavailable);

    const std::vector<const PreprocessedExpression*> source_clauses;
    // Passing the documented inert source verifies the second public consumer boundary.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    const auto derived = derive_inner_domain_equivalences(domain({0U}), source_clauses, source);
    ASSERT_FALSE(derived);
    EXPECT_EQ(derived.error().code(), base::ErrorCode::kUnavailable);
}

auto expression_ptr(PreprocessedExpression expression) -> PreprocessedExpressionPtr {
    return std::make_unique<PreprocessedExpression>(std::move(expression));
}

auto source_column(
    const std::uint32_t range_table_index,
    const std::uint32_t column_index,
    const types::DataType data_type = types::kInt64Type,
    const std::uint32_t outer_query_levels = 0U
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        data_type,
        QueryColumnReference{
            .range_table_index = QueryRangeTableIndex{range_table_index},
            .column_index = QueryColumnIndex{column_index},
            .data_type = data_type,
            .outer_query_levels = outer_query_levels,
        },
    };
}

auto source_integer_literal(const std::int64_t value) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kInt64Type,
        QueryLiteralExpression{.value = types::Value::integer(value)},
    };
}

auto source_null_literal() -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kInt64Type,
        QueryLiteralExpression{.value = types::Value::null(types::kInt64Type)},
    };
}

auto source_parameter(const std::uint32_t slot) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kInt64Type,
        QueryParameterReference{
            .slot = sql::ParameterSlot{slot},
            .data_type = types::kInt64Type,
        },
    };
}

auto source_equal(PreprocessedExpressionPtr left, PreprocessedExpressionPtr right)
    -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = std::move(left),
            .right = std::move(right),
        },
    };
}

auto source_boolean(
    const types::BooleanOperator operation,
    std::vector<PreprocessedExpressionPtr> operands
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryBooleanExpression{
            .op = operation,
            .operands = std::move(operands),
        },
    };
}

auto has_implied(
    const InnerDomainEquivalenceResult& result,
    const EquivalenceColumn expected_column,
    const EquivalencePseudoconstant& expected_pseudoconstant
) -> bool {
    return std::any_of(
        result.implied_base_equalities.begin(),
        result.implied_base_equalities.end(),
        [&expected_column, &expected_pseudoconstant](const ImpliedBaseEquality& equality) {
            return equality.column == expected_column
                   && equality.pseudoconstant == expected_pseudoconstant;
        }
    );
}

TEST(EquivalenceTest, PropagatesExternalPseudoconstantAcrossCurrentLevelColumns) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto external = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(external) << external.error().to_string();

    const EquivalenceColumn first{
        .range_table_index = QueryRangeTableIndex{0U},
        .column_index = QueryColumnIndex{0U},
        .data_type = types::kInt64Type,
    };
    const EquivalenceColumn second{
        .range_table_index = QueryRangeTableIndex{1U},
        .column_index = QueryColumnIndex{0U},
        .data_type = types::kInt64Type,
    };
    const std::vector equalities{
        EquivalenceEquality{.left = first, .right = second},
        EquivalenceEquality{.left = second, .right = parameter(external.value())},
    };

    auto result = build_inner_domain_equivalences(domain({0U, 1U}), equalities, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->classes.size(), 1U);
    ASSERT_EQ(result->classes.front().members.size(), 3U);
    ASSERT_TRUE(result->classes.front().preferred_pseudoconstant.has_value());
    EXPECT_EQ(
        *result->classes.front().preferred_pseudoconstant,
        parameter_pseudoconstant(external.value())
    );
    EXPECT_EQ(result->implied_base_equalities.size(), 1U);
    EXPECT_TRUE(has_implied(*result, first, parameter_pseudoconstant(external.value())));
    EXPECT_FALSE(has_implied(*result, second, parameter_pseudoconstant(external.value())));
}

TEST(EquivalenceTest, PrefersLiteralOverExternalParameterAsStableAnchor) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto external = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(external) << external.error().to_string();

    const EquivalenceColumn value_column{
        .range_table_index = QueryRangeTableIndex{0U},
        .column_index = QueryColumnIndex{2U},
        .data_type = types::kInt64Type,
    };
    const std::vector equalities{
        EquivalenceEquality{.left = value_column, .right = parameter(external.value())},
        EquivalenceEquality{.left = parameter(external.value()), .right = integer_literal(7)},
    };

    auto result = build_inner_domain_equivalences(domain({0U}), equalities, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->classes.size(), 1U);
    ASSERT_TRUE(result->classes.front().preferred_pseudoconstant.has_value());
    EXPECT_EQ(*result->classes.front().preferred_pseudoconstant, integer_pseudoconstant(7));
    EXPECT_TRUE(has_implied(*result, value_column, integer_pseudoconstant(7)));
}

TEST(EquivalenceTest, SupportsExecutorPseudoconstantsWithoutSqlAstOwnership) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto executor = parameters->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor) << executor.error().to_string();

    const EquivalenceColumn outer_column{
        .range_table_index = QueryRangeTableIndex{0U},
        .column_index = QueryColumnIndex{1U},
        .data_type = types::kInt64Type,
    };
    const EquivalenceColumn inner_column{
        .range_table_index = QueryRangeTableIndex{1U},
        .column_index = QueryColumnIndex{1U},
        .data_type = types::kInt64Type,
    };
    const std::vector equalities{
        EquivalenceEquality{.left = outer_column, .right = inner_column},
        EquivalenceEquality{.left = inner_column, .right = parameter(executor.value())},
    };

    auto result = build_inner_domain_equivalences(domain({0U, 1U}), equalities, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->implied_base_equalities.size(), 1U);
    EXPECT_TRUE(has_implied(*result, outer_column, parameter_pseudoconstant(executor.value())));
}

TEST(EquivalenceTest, RetainsRelationOnlyClassesWithoutInventingBaseRestrictions) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();

    const std::vector equalities{
        EquivalenceEquality{.left = column(0U, 0U), .right = column(1U, 0U)},
    };
    auto result = build_inner_domain_equivalences(domain({0U, 1U}), equalities, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->classes.size(), 1U);
    EXPECT_FALSE(result->classes.front().preferred_pseudoconstant.has_value());
    EXPECT_TRUE(result->implied_base_equalities.empty());
}

TEST(EquivalenceTest, KeepsFirstParameterAnchorWhenNoLiteralExists) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto external = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(external) << external.error().to_string();
    auto executor = parameters->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor) << executor.error().to_string();

    const std::vector equalities{
        EquivalenceEquality{.left = column(0U, 0U), .right = parameter(external.value())},
        EquivalenceEquality{
            .left = parameter(external.value()),
            .right = parameter(executor.value())
        },
        EquivalenceEquality{.left = column(1U, 0U), .right = parameter(executor.value())},
    };
    auto result = build_inner_domain_equivalences(domain({0U, 1U}), equalities, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->classes.size(), 1U);
    ASSERT_TRUE(result->classes.front().preferred_pseudoconstant.has_value());
    EXPECT_EQ(
        *result->classes.front().preferred_pseudoconstant,
        parameter_pseudoconstant(external.value())
    );
    EXPECT_TRUE(has_implied(
        *result,
        EquivalenceColumn{
            .range_table_index = QueryRangeTableIndex{1U},
            .column_index = QueryColumnIndex{0U},
            .data_type = types::kInt64Type,
        },
        parameter_pseudoconstant(external.value())
    ));
}

TEST(EquivalenceTest, ExtractsAndFlattenedLogicalClausesAndDoesNotRetainThem) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto external = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(external) << external.error().to_string();

    PreprocessedExpression source = source_boolean(types::BooleanOperator::kAnd, [] {
        std::vector<PreprocessedExpressionPtr> operands;
        operands.push_back(expression_ptr(source_equal(
            expression_ptr(source_column(0U, 0U)),
            expression_ptr(source_column(1U, 0U))
        )));
        operands.push_back(expression_ptr(source_equal(
            expression_ptr(source_column(1U, 0U)),
            expression_ptr(source_parameter(1U))
        )));
        return operands;
    }());
    const std::vector<const PreprocessedExpression*> source_clauses{&source};

    auto result = derive_inner_domain_equivalences(domain({0U, 1U}), source_clauses, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_TRUE(has_implied(
        *result,
        EquivalenceColumn{
            .range_table_index = QueryRangeTableIndex{0U},
            .column_index = QueryColumnIndex{0U},
            .data_type = types::kInt64Type,
        },
        parameter_pseudoconstant(external.value())
    ));

    source = source_integer_literal(99);
    EXPECT_TRUE(has_implied(
        *result,
        EquivalenceColumn{
            .range_table_index = QueryRangeTableIndex{0U},
            .column_index = QueryColumnIndex{0U},
            .data_type = types::kInt64Type,
        },
        parameter_pseudoconstant(external.value())
    ));
}

TEST(EquivalenceTest, IgnoresOuterNullAndOutOfDomainSourceEqualities) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto external = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(external) << external.error().to_string();

    PreprocessedExpression outer = source_equal(
        expression_ptr(source_column(0U, 0U)),
        expression_ptr(source_column(7U, 0U, types::kInt64Type, 1U))
    );
    PreprocessedExpression null =
        source_equal(expression_ptr(source_column(0U, 1U)), expression_ptr(source_null_literal()));
    PreprocessedExpression outside =
        source_equal(expression_ptr(source_column(2U, 0U)), expression_ptr(source_parameter(1U)));
    const std::vector<const PreprocessedExpression*> clauses{&outer, &null, &outside};

    auto result = derive_inner_domain_equivalences(domain({0U}), clauses, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_TRUE(result->classes.empty());
    EXPECT_TRUE(result->implied_base_equalities.empty());
    EXPECT_FALSE(has_implied(
        *result,
        EquivalenceColumn{
            .range_table_index = QueryRangeTableIndex{0U},
            .column_index = QueryColumnIndex{0U},
            .data_type = types::kInt64Type,
        },
        parameter_pseudoconstant(external.value())
    ));
}

TEST(EquivalenceTest, IgnoresMismatchedTypedValueEqualities) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();

    const std::vector equalities{
        EquivalenceEquality{
            .left = column(0U, 0U, types::kInt64Type),
            .right = EquivalenceLiteral{.value = types::Value::floating(1.0F)},
        },
    };
    auto result = build_inner_domain_equivalences(domain({0U}), equalities, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_TRUE(result->classes.empty());
    EXPECT_TRUE(result->implied_base_equalities.empty());
}

TEST(EquivalenceTest, DoesNotJoinThroughNullOrNanLiteralValues) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto external = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(external) << external.error().to_string();

    const std::vector null_equalities{
        EquivalenceEquality{
            .left = column(0U, 0U),
            .right = EquivalenceLiteral{.value = types::Value::null(types::kInt64Type)},
        },
        EquivalenceEquality{
            .left = EquivalenceLiteral{.value = types::Value::null(types::kInt64Type)},
            .right = parameter(external.value()),
        },
    };
    auto null_result = build_inner_domain_equivalences(domain({0U}), null_equalities, *parameters);
    ASSERT_TRUE(null_result) << null_result.error().to_string();
    EXPECT_TRUE(null_result->classes.empty());
    EXPECT_TRUE(null_result->implied_base_equalities.empty());

    const std::vector nan_equalities{
        EquivalenceEquality{
            .left = column(0U, 0U, types::kFloat32Type),
            .right =
                EquivalenceLiteral{
                    .value = types::Value::floating(std::numeric_limits<float>::quiet_NaN()),
                },
        },
        EquivalenceEquality{
            .left =
                EquivalenceLiteral{
                    .value = types::Value::floating(std::numeric_limits<float>::quiet_NaN()),
                },
            .right = column(1U, 0U, types::kFloat32Type),
        },
    };
    auto nan_result =
        build_inner_domain_equivalences(domain({0U, 1U}), nan_equalities, *parameters);
    ASSERT_TRUE(nan_result) << nan_result.error().to_string();
    EXPECT_TRUE(nan_result->classes.empty());
    EXPECT_TRUE(nan_result->implied_base_equalities.empty());
}

TEST(EquivalenceTest, IgnoresUnknownTypedMembersWithoutMakingOptimizerFacts) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();

    const std::vector equalities{
        EquivalenceEquality{
            .left = column(0U, 0U),
            .right = EquivalenceLiteral{.value = types::Value::unknown_string("unknown")},
        },
        EquivalenceEquality{
            .left =
                EquivalenceColumn{
                    .range_table_index = QueryRangeTableIndex{0U},
                    .column_index = QueryColumnIndex{1U},
                    .data_type = types::kUnknownType,
                },
            .right = integer_literal(4),
        },
    };
    auto result = build_inner_domain_equivalences(domain({0U}), equalities, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_TRUE(result->classes.empty());
    EXPECT_TRUE(result->implied_base_equalities.empty());
}

TEST(EquivalenceTest, LeavesOrAndNonEqualityClausesOutsideInference) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();

    PreprocessedExpression source = source_boolean(types::BooleanOperator::kOr, [] {
        std::vector<PreprocessedExpressionPtr> operands;
        operands.push_back(expression_ptr(source_equal(
            expression_ptr(source_column(0U, 0U)),
            expression_ptr(source_integer_literal(4))
        )));
        operands.push_back(expression_ptr(source_equal(
            expression_ptr(source_column(1U, 0U)),
            expression_ptr(source_integer_literal(4))
        )));
        return operands;
    }());
    const std::vector<const PreprocessedExpression*> clauses{&source};

    auto result = derive_inner_domain_equivalences(domain({0U, 1U}), clauses, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_TRUE(result->classes.empty());
    EXPECT_TRUE(result->implied_base_equalities.empty());
}

TEST(EquivalenceTest, RejectsInvalidDomainAndNamespaceMismatch) {
    auto parameters = parameter_namespace();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto external = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(external) << external.error().to_string();

    const std::vector duplicate_domain_equality{
        EquivalenceEquality{.left = column(0U, 0U), .right = integer_literal(3)},
    };
    auto duplicate_domain =
        build_inner_domain_equivalences(domain({0U, 0U}), duplicate_domain_equality, *parameters);
    ASSERT_FALSE(duplicate_domain);
    EXPECT_EQ(duplicate_domain.error().code(), base::ErrorCode::kPlan);

    PlanParameterReference wrong_reference = external.value();
    wrong_reference.data_type = types::kFloat32Type;
    const std::vector mismatched_namespace{
        EquivalenceEquality{.left = column(0U, 0U), .right = parameter(wrong_reference)},
    };
    auto mismatch =
        build_inner_domain_equivalences(domain({0U}), mismatched_namespace, *parameters);
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().code(), base::ErrorCode::kPlan);
}

}  // namespace
}  // namespace db::planner
