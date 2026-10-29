#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/planner/path.hpp"
#include "db/planner/preprocess.hpp"
#include "db/sql/ast_common.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {
namespace {

class FakeMetadataPort final : public MetadataPort {
public:
    FakeMetadataPort(
        const std::uint64_t generation,
        PlanningTable table,
        std::vector<PlanningIndex> indexes,
        const bool changes_generation = false
    )
        : generation_{generation},
          table_{std::move(table)},
          indexes_{std::move(indexes)},
          changes_generation_{changes_generation} {}

    auto generation() const noexcept -> std::uint64_t override {
        return generation_;
    }

    auto lookup_table(const base::Oid oid) const
        -> base::Result<std::optional<PlanningTable>> override {
        return oid == table_.oid ? std::optional<PlanningTable>{table_}
                                 : std::optional<PlanningTable>{};
    }

    auto indexes_for_table(const base::Oid table_oid) const
        -> base::Result<std::vector<PlanningIndex>> override {
        if (changes_generation_) {
            ++generation_;
        }
        return table_oid == table_.oid ? indexes_ : std::vector<PlanningIndex>{};
    }

private:
    mutable std::uint64_t generation_;
    PlanningTable table_;
    std::vector<PlanningIndex> indexes_;
    bool changes_generation_;
};

enum class MetadataExceptionPoint : std::uint8_t {
    kTableLookup,
    kIndexLookup,
};

class ThrowingMetadataPort final : public MetadataPort {
public:
    ThrowingMetadataPort(PlanningTable table, const MetadataExceptionPoint exception_point)
        : table_{std::move(table)},
          exception_point_{exception_point} {}

    auto generation() const noexcept -> std::uint64_t override {
        return 72U;
    }

    auto lookup_table(const base::Oid oid) const
        -> base::Result<std::optional<PlanningTable>> override {
        static_cast<void>(oid);
        if (exception_point_ == MetadataExceptionPoint::kTableLookup) {
            throw std::runtime_error{"injected metadata table failure"};
        }
        return std::optional<PlanningTable>{table_};
    }

    auto indexes_for_table(const base::Oid table_oid) const
        -> base::Result<std::vector<PlanningIndex>> override {
        static_cast<void>(table_oid);
        if (exception_point_ == MetadataExceptionPoint::kIndexLookup) {
            throw std::bad_alloc{};
        }
        return std::vector<PlanningIndex>{};
    }

private:
    PlanningTable table_;
    MetadataExceptionPoint exception_point_;
};

auto planning_table() -> PlanningTable {
    return PlanningTable{
        .oid = base::Oid{42U},
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "enabled", .data_type = types::kBoolType}
        },
    };
}

auto base_table() -> PreprocessedBaseTable {
    return PreprocessedBaseTable{
        .oid = base::Oid{42U},
        .name = "accounts",
        .alias = std::optional<std::string>{"a"},
        .columns =
            {{.name = "id", .data_type = types::kInt64Type},
             {.name = "enabled", .data_type = types::kBoolType}},
        .range_table_index = QueryRangeTableIndex{0U},
    };
}

auto expression_ptr(PreprocessedExpression expression) -> PreprocessedExpressionPtr {
    return std::make_unique<PreprocessedExpression>(std::move(expression));
}

auto make_column(
    const std::uint32_t range_table_index,
    const std::uint32_t column_index,
    const types::DataType data_type
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        data_type,
        QueryColumnReference{
            .range_table_index = QueryRangeTableIndex{range_table_index},
            .column_index = QueryColumnIndex{column_index},
            .data_type = data_type,
            .outer_query_levels = 0U,
        },
    };
}

auto make_parameter(const std::uint32_t slot, const types::DataType data_type)
    -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        data_type,
        QueryParameterReference{
            .slot = sql::ParameterSlot{slot},
            .data_type = data_type,
        },
    };
}

auto make_integer_literal(const std::int64_t value) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kInt64Type,
        QueryLiteralExpression{.value = types::Value::integer(value)},
    };
}

auto make_boolean_literal(const bool value) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryLiteralExpression{.value = types::Value::boolean(value)},
    };
}

auto make_floating_literal(const float value) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kFloat32Type,
        QueryLiteralExpression{.value = types::Value::floating(value)},
    };
}

auto make_comparison(
    const types::BinaryOperator operation,
    PreprocessedExpressionPtr left,
    PreprocessedExpressionPtr right
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryBinaryExpression{
            .op = operation,
            .left = std::move(left),
            .right = std::move(right),
        },
    };
}

auto make_is_null(PreprocessedExpressionPtr operand, const bool negated = false)
    -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryIsNullExpression{
            .negated = negated,
            .operand = std::move(operand),
        },
    };
}

auto make_between(
    PreprocessedExpressionPtr operand,
    PreprocessedExpressionPtr lower,
    PreprocessedExpressionPtr upper,
    const bool negated = false
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryBetweenExpression{
            .negated = negated,
            .operand = std::move(operand),
            .lower = std::move(lower),
            .upper = std::move(upper),
        },
    };
}

auto make_in_list(PreprocessedExpressionPtr operand, std::vector<PreprocessedExpressionPtr> values)
    -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryInListExpression{
            .negated = false,
            .operand = std::move(operand),
            .values = std::move(values),
        },
    };
}

auto make_and(std::vector<PreprocessedExpressionPtr> operands) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryBooleanExpression{
            .op = types::BooleanOperator::kAnd,
            .operands = std::move(operands),
        },
    };
}

auto find_index_path(const BaseRelationPathList& paths, const base::Oid oid)
    -> const IndexScanPath* {
    const auto found =
        std::find_if(paths.paths.begin(), paths.paths.end(), [oid](const BaseRelationPath& path) {
            const auto* const index_path = std::get_if<IndexScanPath>(&path);
            return index_path != nullptr && index_path->index.oid == oid;
        });
    return found == paths.paths.end() ? nullptr : std::get_if<IndexScanPath>(&*found);
}

auto literal_bound_value(const IndexPathBoundValue& value) -> const types::Value* {
    const auto* const literal = std::get_if<IndexPathLiteralBoundValue>(&value);
    return literal == nullptr ? nullptr : &literal->value;
}

auto equality_to_parameter(const std::uint32_t range_table_index = 0U) -> PreprocessedExpression {
    return make_comparison(
        types::BinaryOperator::kEqual,
        expression_ptr(make_column(range_table_index, 0U, types::kInt64Type)),
        expression_ptr(make_parameter(1U, types::kInt64Type))
    );
}

TEST(PathRelationTest, BuildsMetadataOnlyTypedAlternativesAndSelectsUsableIndex) {
    const std::vector indexes{
        PlanningIndex{
            .oid = base::Oid{9U},
            .name = "accounts_enabled_idx",
            .column_names = {"enabled"},
            .unique = false,
        },
        PlanningIndex{
            .oid = base::Oid{4U},
            .name = "accounts_id_idx",
            .column_names = {"id"},
            .unique = true,
        },
    };
    const FakeMetadataPort metadata_port{42U, planning_table(), indexes};
    PreprocessedExpression filter = equality_to_parameter();

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    EXPECT_EQ(paths->catalog_generation, 42U);
    EXPECT_EQ(paths->relation.oid, base::Oid{42U});
    EXPECT_EQ(paths->relation.name, "accounts");
    EXPECT_EQ(paths->relation.alias, std::optional<std::string>{"a"});
    ASSERT_EQ(paths->paths.size(), 3U);
    EXPECT_EQ(base_relation_path_kind(paths->paths[0U]), BaseRelationPathKind::kSequentialScan);
    const auto* const first_index = std::get_if<IndexScanPath>(&paths->paths[1U]);
    const auto* const second_index = std::get_if<IndexScanPath>(&paths->paths[2U]);
    ASSERT_NE(first_index, nullptr);
    ASSERT_NE(second_index, nullptr);
    EXPECT_EQ(first_index->index.oid, base::Oid{4U});
    EXPECT_EQ(second_index->index.oid, base::Oid{9U});
    EXPECT_EQ(first_index->domain_restriction_count, 1U);
    EXPECT_EQ(first_index->residual_source_clause_count, 1U);
    ASSERT_EQ(first_index->bounds.equality_prefix.size(), 1U);
    const auto* const parameter =
        std::get_if<IndexPathParameterBoundValue>(&first_index->bounds.equality_prefix.front());
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(parameter->slot, sql::ParameterSlot{1U});
    EXPECT_EQ(paths->selected_path_index, 1U);

    EXPECT_TRUE(second_index->bounds.equality_prefix.empty());
    EXPECT_EQ(second_index->domain_restriction_count, 0U);
}

TEST(PathRelationTest, UsesSequentialScanWhenNoIndexHasAKeyDomain) {
    const std::vector indexes{PlanningIndex{
        .oid = base::Oid{9U},
        .name = "accounts_enabled_idx",
        .column_names = {"enabled"},
        .unique = false,
    }};
    const FakeMetadataPort metadata_port{43U, planning_table(), indexes};
    PreprocessedExpression filter = equality_to_parameter();

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    EXPECT_EQ(paths->selected_path_index, 0U);
    EXPECT_EQ(base_relation_path_kind(paths->paths[0U]), BaseRelationPathKind::kSequentialScan);
}

TEST(PathRelationTest, BuildsCompositeDomainFromOwnedDerivedEqualities) {
    const std::vector indexes{PlanningIndex{
        .oid = base::Oid{9U},
        .name = "accounts_enabled_id_idx",
        .column_names = {"enabled", "id"},
        .unique = true,
    }};
    const FakeMetadataPort metadata_port{42U, planning_table(), indexes};
    const std::vector<BaseEqualityRestriction> derived_equalities{
        BaseEqualityRestriction{
            .range_table_index = QueryRangeTableIndex{0U},
            .column_index = QueryColumnIndex{1U},
            .data_type = types::kBoolType,
            .value = IndexPathLiteralBoundValue{.value = types::Value::boolean(true)},
        },
        BaseEqualityRestriction{
            .range_table_index = QueryRangeTableIndex{0U},
            .column_index = QueryColumnIndex{0U},
            .data_type = types::kInt64Type,
            .value = IndexPathPlanParameterBoundValue{
                .reference = PlanParameterReference{
                    .id = PlanParameterId{4U},
                    .kind = PlanParameterKind::kExecutor,
                    .data_type = types::kInt64Type,
                },
            },
        },
    };

    const auto paths =
        build_base_relation_paths_for_clauses(metadata_port, base_table(), {}, derived_equalities);
    ASSERT_TRUE(paths) << paths.error().to_string();
    ASSERT_EQ(paths->selected_path_index, 1U);
    const auto* const index = std::get_if<IndexScanPath>(&paths->paths[1U]);
    ASSERT_NE(index, nullptr);
    ASSERT_EQ(index->bounds.equality_prefix.size(), 2U);
    EXPECT_NE(
        std::get_if<IndexPathLiteralBoundValue>(index->bounds.equality_prefix.data()),
        nullptr
    );
    const auto* const parameter =
        std::get_if<IndexPathPlanParameterBoundValue>(&index->bounds.equality_prefix[1U]);
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(parameter->reference.id, PlanParameterId{4U});
    EXPECT_EQ(index->domain_restriction_count, 2U);
    EXPECT_EQ(index->residual_source_clause_count, 0U);
}

TEST(PathRelationTest, PrefersLongerEqualityPrefixThenRangeShapeThenIndexIdentity) {
    const std::vector indexes{
        PlanningIndex{
            .oid = base::Oid{20U},
            .name = "accounts_id_idx",
            .column_names = {"id"},
            .unique = false,
        },
        PlanningIndex{
            .oid = base::Oid{10U},
            .name = "accounts_id_enabled_idx",
            .column_names = {"id", "enabled"},
            .unique = false,
        },
    };
    const FakeMetadataPort metadata_port{44U, planning_table(), indexes};
    std::vector<PreprocessedExpressionPtr> clauses;
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kEqual,
        expression_ptr(make_column(0U, 0U, types::kInt64Type)),
        expression_ptr(make_integer_literal(7))
    )));
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kEqual,
        expression_ptr(make_column(0U, 1U, types::kBoolType)),
        expression_ptr(make_boolean_literal(true))
    )));
    PreprocessedExpression filter = make_and(std::move(clauses));

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    const auto* const selected =
        std::get_if<IndexScanPath>(&paths->paths[paths->selected_path_index]);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->index.oid, base::Oid{10U});
    EXPECT_EQ(selected->bounds.equality_prefix.size(), 2U);
}

TEST(PathRelationTest, MatchesCompositePrefixAndKeepsClausesAfterTheRangeResidual) {
    const std::vector indexes{PlanningIndex{
        .oid = base::Oid{12U},
        .name = "accounts_id_enabled_idx",
        .column_names = {"id", "enabled"},
        .unique = false,
    }};
    const FakeMetadataPort metadata_port{51U, planning_table(), indexes};
    std::vector<PreprocessedExpressionPtr> clauses;
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kEqual,
        expression_ptr(make_column(0U, 0U, types::kInt64Type)),
        expression_ptr(make_integer_literal(7))
    )));
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kGreater,
        expression_ptr(make_column(0U, 1U, types::kBoolType)),
        expression_ptr(make_boolean_literal(false))
    )));
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kEqual,
        expression_ptr(make_column(0U, 0U, types::kInt64Type)),
        expression_ptr(make_integer_literal(9))
    )));
    PreprocessedExpression filter = make_and(std::move(clauses));

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    const IndexScanPath* const index = find_index_path(*paths, base::Oid{12U});
    ASSERT_NE(index, nullptr);
    ASSERT_EQ(index->bounds.equality_prefix.size(), 1U);
    const types::Value* const equal = literal_bound_value(index->bounds.equality_prefix.front());
    ASSERT_NE(equal, nullptr);
    ASSERT_NE(equal->as_integer(), nullptr);
    EXPECT_EQ(*equal->as_integer(), 7);
    ASSERT_TRUE(index->bounds.lower.has_value());
    EXPECT_TRUE(index->bounds.lower->exclusive);
    EXPECT_EQ(index->domain_restriction_count, 2U);
    EXPECT_EQ(index->residual_source_clause_count, 1U);
}

TEST(PathRelationTest, UsesTwoRangeEndpointsAndReverseComparisonDeterministically) {
    const std::vector indexes{PlanningIndex{
        .oid = base::Oid{13U},
        .name = "accounts_id_idx",
        .column_names = {"id"},
        .unique = false,
    }};
    const FakeMetadataPort metadata_port{52U, planning_table(), indexes};
    std::vector<PreprocessedExpressionPtr> clauses;
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kGreaterEqual,
        expression_ptr(make_column(0U, 0U, types::kInt64Type)),
        expression_ptr(make_integer_literal(3))
    )));
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kGreater,
        expression_ptr(make_column(0U, 0U, types::kInt64Type)),
        expression_ptr(make_integer_literal(8))
    )));
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kLess,
        expression_ptr(make_integer_literal(90)),
        expression_ptr(make_column(0U, 0U, types::kInt64Type))
    )));
    clauses.push_back(expression_ptr(make_comparison(
        types::BinaryOperator::kGreaterEqual,
        expression_ptr(make_integer_literal(50)),
        expression_ptr(make_column(0U, 0U, types::kInt64Type))
    )));
    PreprocessedExpression filter = make_and(std::move(clauses));

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    const IndexScanPath* const index = find_index_path(*paths, base::Oid{13U});
    ASSERT_NE(index, nullptr);
    ASSERT_TRUE(index->bounds.lower.has_value());
    ASSERT_TRUE(index->bounds.upper.has_value());
    EXPECT_TRUE(index->bounds.lower->exclusive);
    EXPECT_FALSE(index->bounds.upper->exclusive);
    const types::Value* const lower = literal_bound_value(index->bounds.lower->value);
    const types::Value* const upper = literal_bound_value(index->bounds.upper->value);
    ASSERT_NE(lower, nullptr);
    ASSERT_NE(upper, nullptr);
    ASSERT_NE(lower->as_integer(), nullptr);
    ASSERT_NE(upper->as_integer(), nullptr);
    EXPECT_EQ(*lower->as_integer(), 90);
    EXPECT_EQ(*upper->as_integer(), 50);
    EXPECT_EQ(index->domain_restriction_count, 2U);
    EXPECT_EQ(index->residual_source_clause_count, 0U);
}

TEST(PathRelationTest, UsesIsNullAndBetweenButPreservesHighLevelResiduals) {
    const std::vector indexes{
        PlanningIndex{
            .oid = base::Oid{14U},
            .name = "accounts_id_idx",
            .column_names = {"id"},
            .unique = false,
        },
        PlanningIndex{
            .oid = base::Oid{15U},
            .name = "accounts_enabled_idx",
            .column_names = {"enabled"},
            .unique = false,
        },
    };
    const FakeMetadataPort metadata_port{53U, planning_table(), indexes};
    std::vector<PreprocessedExpressionPtr> clauses;
    clauses.push_back(expression_ptr(make_between(
        expression_ptr(make_column(0U, 0U, types::kInt64Type)),
        expression_ptr(make_integer_literal(10)),
        expression_ptr(make_integer_literal(20))
    )));
    std::vector<PreprocessedExpressionPtr> singleton_values;
    singleton_values.push_back(expression_ptr(make_boolean_literal(true)));
    clauses.push_back(expression_ptr(make_in_list(
        expression_ptr(make_column(0U, 1U, types::kBoolType)),
        std::move(singleton_values)
    )));
    clauses.push_back(
        expression_ptr(make_is_null(expression_ptr(make_column(0U, 1U, types::kBoolType))))
    );
    PreprocessedExpression filter = make_and(std::move(clauses));

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    const IndexScanPath* const id_index = find_index_path(*paths, base::Oid{14U});
    const IndexScanPath* const enabled_index = find_index_path(*paths, base::Oid{15U});
    ASSERT_NE(id_index, nullptr);
    ASSERT_NE(enabled_index, nullptr);
    ASSERT_TRUE(id_index->bounds.lower.has_value());
    ASSERT_TRUE(id_index->bounds.upper.has_value());
    EXPECT_EQ(id_index->domain_restriction_count, 1U);
    EXPECT_EQ(id_index->residual_source_clause_count, 2U);

    ASSERT_EQ(enabled_index->bounds.equality_prefix.size(), 1U);
    const types::Value* const equality =
        literal_bound_value(enabled_index->bounds.equality_prefix[0U]);
    ASSERT_NE(equality, nullptr);
    ASSERT_NE(equality->as_boolean(), nullptr);
    EXPECT_TRUE(*equality->as_boolean());
    EXPECT_EQ(enabled_index->domain_restriction_count, 1U);
    EXPECT_EQ(enabled_index->residual_source_clause_count, 2U);
}

TEST(PathRelationTest, KeepsParameterBoundsAsResidualForRuntimeNullSafety) {
    const std::vector indexes{PlanningIndex{
        .oid = base::Oid{18U},
        .name = "accounts_id_idx",
        .column_names = {"id"},
        .unique = false,
    }};
    const FakeMetadataPort metadata_port{54U, planning_table(), indexes};
    PreprocessedExpression filter = equality_to_parameter();

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    const IndexScanPath* const index = find_index_path(*paths, base::Oid{18U});
    ASSERT_NE(index, nullptr);
    ASSERT_EQ(index->bounds.equality_prefix.size(), 1U);
    EXPECT_EQ(index->domain_restriction_count, 1U);
    EXPECT_EQ(index->residual_source_clause_count, 1U);
}

TEST(PathRelationTest, UsesParameterizedBetweenBoundsWithOneRuntimeRecheck) {
    const std::vector indexes{PlanningIndex{
        .oid = base::Oid{21U},
        .name = "accounts_id_idx",
        .column_names = {"id"},
        .unique = false,
    }};
    const FakeMetadataPort metadata_port{56U, planning_table(), indexes};
    PreprocessedExpression filter = make_between(
        expression_ptr(make_column(0U, 0U, types::kInt64Type)),
        expression_ptr(make_parameter(1U, types::kInt64Type)),
        expression_ptr(make_parameter(2U, types::kInt64Type))
    );

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    const IndexScanPath* const index = find_index_path(*paths, base::Oid{21U});
    ASSERT_NE(index, nullptr);
    ASSERT_TRUE(index->bounds.lower.has_value());
    ASSERT_TRUE(index->bounds.upper.has_value());
    const auto* const lower =
        std::get_if<IndexPathParameterBoundValue>(&index->bounds.lower->value);
    const auto* const upper =
        std::get_if<IndexPathParameterBoundValue>(&index->bounds.upper->value);
    ASSERT_NE(lower, nullptr);
    ASSERT_NE(upper, nullptr);
    EXPECT_EQ(lower->slot, sql::ParameterSlot{1U});
    EXPECT_EQ(upper->slot, sql::ParameterSlot{2U});
    EXPECT_EQ(index->domain_restriction_count, 1U);
    EXPECT_EQ(index->residual_source_clause_count, 1U);
}

TEST(PathRelationTest, LeavesSqlFloatComparisonsResidualButUsesFloatIsNull) {
    PlanningTable float_table = planning_table();
    float_table.columns[1U] = PlanningColumn{
        .name = "score",
        .data_type = types::kFloat32Type,
    };
    PreprocessedBaseTable float_base = base_table();
    float_base.columns[1U] = QueryColumn{
        .name = "score",
        .data_type = types::kFloat32Type,
    };
    const std::vector indexes{PlanningIndex{
        .oid = base::Oid{20U},
        .name = "accounts_score_idx",
        .column_names = {"score"},
        .unique = false,
    }};
    const FakeMetadataPort metadata_port{55U, std::move(float_table), indexes};
    PreprocessedExpression equality = make_comparison(
        types::BinaryOperator::kEqual,
        expression_ptr(make_column(0U, 1U, types::kFloat32Type)),
        expression_ptr(make_floating_literal(-0.0F))
    );
    const auto equality_paths = build_base_relation_paths(metadata_port, float_base, &equality);
    ASSERT_TRUE(equality_paths) << equality_paths.error().to_string();
    const IndexScanPath* const equality_index = find_index_path(*equality_paths, base::Oid{20U});
    ASSERT_NE(equality_index, nullptr);
    EXPECT_TRUE(equality_index->bounds.equality_prefix.empty());
    EXPECT_EQ(equality_index->domain_restriction_count, 0U);
    EXPECT_EQ(equality_index->residual_source_clause_count, 1U);

    PreprocessedExpression between = make_between(
        expression_ptr(make_column(0U, 1U, types::kFloat32Type)),
        expression_ptr(make_floating_literal(-1.0F)),
        expression_ptr(make_floating_literal(1.0F))
    );
    const auto between_paths = build_base_relation_paths(metadata_port, float_base, &between);
    ASSERT_TRUE(between_paths) << between_paths.error().to_string();
    const IndexScanPath* const between_index = find_index_path(*between_paths, base::Oid{20U});
    ASSERT_NE(between_index, nullptr);
    EXPECT_FALSE(between_index->bounds.lower.has_value());
    EXPECT_FALSE(between_index->bounds.upper.has_value());
    EXPECT_EQ(between_index->domain_restriction_count, 0U);
    EXPECT_EQ(between_index->residual_source_clause_count, 1U);

    PreprocessedExpression is_null =
        make_is_null(expression_ptr(make_column(0U, 1U, types::kFloat32Type)));
    const auto null_paths = build_base_relation_paths(metadata_port, float_base, &is_null);
    ASSERT_TRUE(null_paths) << null_paths.error().to_string();
    const IndexScanPath* const null_index = find_index_path(*null_paths, base::Oid{20U});
    ASSERT_NE(null_index, nullptr);
    ASSERT_EQ(null_index->bounds.equality_prefix.size(), 1U);
    const types::Value* const null_bound =
        literal_bound_value(null_index->bounds.equality_prefix.front());
    ASSERT_NE(null_bound, nullptr);
    EXPECT_TRUE(null_bound->is_null());
    EXPECT_EQ(null_index->domain_restriction_count, 1U);
    EXPECT_EQ(null_index->residual_source_clause_count, 0U);
}

TEST(PathRelationTest, LeavesCrossRelationClausesForJoinPlacement) {
    const std::vector indexes{PlanningIndex{
        .oid = base::Oid{18U},
        .name = "accounts_id_idx",
        .column_names = {"id"},
        .unique = false,
    }};
    const FakeMetadataPort metadata_port{57U, planning_table(), indexes};
    PreprocessedExpression filter = make_comparison(
        types::BinaryOperator::kEqual,
        expression_ptr(make_column(0U, 0U, types::kInt64Type)),
        expression_ptr(make_column(1U, 0U, types::kInt64Type))
    );

    const auto paths = build_base_relation_paths(metadata_port, base_table(), &filter);
    ASSERT_TRUE(paths) << paths.error().to_string();
    EXPECT_EQ(paths->selected_path_index, 0U);
}

TEST(PathRelationTest, ExposesOnlySafeForwardNullFirstOrdering) {
    IndexScanPath path{
        .index =
            PlanIndex{
                .oid = base::Oid{16U},
                .name = "accounts_id_enabled_idx",
                .keys =
                    {{.relation_column = PlanColumnIndex{0U},
                      .column_name = "id",
                      .data_type = types::kInt64Type},
                     {.relation_column = PlanColumnIndex{1U},
                      .column_name = "enabled",
                      .data_type = types::kBoolType}},
                .unique = false,
            },
        .bounds = IndexPathBounds{
            .equality_prefix = {IndexPathLiteralBoundValue{.value = types::Value::integer(7)}},
        },
    };
    const std::vector ordered_enabled{IndexPathOrderRequirement{
        .range_table_index = QueryRangeTableIndex{0U},
        .column_index = QueryColumnIndex{1U},
        .direction = sql::SortDirection::kAscending,
        .null_order = sql::NullOrder::kFirst,
    }};
    EXPECT_TRUE(index_path_satisfies_order(path, QueryRangeTableIndex{0U}, ordered_enabled));

    auto descending = ordered_enabled;
    descending.front().direction = sql::SortDirection::kDescending;
    EXPECT_FALSE(index_path_satisfies_order(path, QueryRangeTableIndex{0U}, descending));
}

TEST(PathRelationTest, RejectsMalformedTypedIndexBoundsAndMetadataDrift) {
    const PlanIndex index{
        .oid = base::Oid{17U},
        .name = "accounts_id_idx",
        .keys =
            {{.relation_column = PlanColumnIndex{0U},
              .column_name = "id",
              .data_type = types::kInt64Type}},
        .unique = false,
    };
    const auto complete_key_range = validate_index_path_bounds(
        index,
        IndexPathBounds{
            .equality_prefix = {IndexPathLiteralBoundValue{.value = types::Value::integer(1)}},
            .lower = IndexPathRangeEndpoint{
                .value = IndexPathLiteralBoundValue{.value = types::Value::integer(2)},
            },
        }
    );
    ASSERT_FALSE(complete_key_range);
    EXPECT_EQ(complete_key_range.error().code(), base::ErrorCode::kPlan);

    PlanningTable stale_table = planning_table();
    stale_table.columns[1U].data_type = types::kInt64Type;
    const FakeMetadataPort stale_port{3U, stale_table, {}};
    const auto stale = build_base_relation_paths(stale_port, base_table(), nullptr);
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().code(), base::ErrorCode::kPlan);

    const FakeMetadataPort drifting_port{4U, planning_table(), {}, true};
    const auto drifting = build_base_relation_paths(drifting_port, base_table(), nullptr);
    ASSERT_FALSE(drifting);
    EXPECT_EQ(drifting.error().code(), base::ErrorCode::kUnavailable);
}

TEST(PathRelationTest, RejectsDuplicateColumnsInBoundAndMetadataDescriptors) {
    PreprocessedBaseTable duplicate_bound = base_table();
    duplicate_bound.columns.at(1U).name = duplicate_bound.columns.front().name;
    const FakeMetadataPort valid_port{3U, planning_table(), {}};
    const auto invalid_bound = build_base_relation_paths(valid_port, duplicate_bound, nullptr);
    ASSERT_FALSE(invalid_bound);
    EXPECT_EQ(invalid_bound.error().code(), base::ErrorCode::kPlan);

    PlanningTable duplicate_metadata = planning_table();
    duplicate_metadata.columns.at(1U).name = duplicate_metadata.columns.front().name;
    const FakeMetadataPort invalid_port{3U, std::move(duplicate_metadata), {}};
    const auto invalid_metadata = build_base_relation_paths(invalid_port, base_table(), nullptr);
    ASSERT_FALSE(invalid_metadata);
    EXPECT_EQ(invalid_metadata.error().code(), base::ErrorCode::kPlan);
}

TEST(PathRelationTest, ConvertsMetadataPortExceptionsAtSnapshotReadBoundary) {
    const ThrowingMetadataPort table_failure{
        planning_table(),
        MetadataExceptionPoint::kTableLookup,
    };
    const auto missing_table = build_base_relation_paths(table_failure, base_table(), nullptr);
    ASSERT_FALSE(missing_table);
    EXPECT_EQ(missing_table.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        missing_table.error().message().find("injected metadata table failure"),
        std::string::npos
    );
    ASSERT_EQ(missing_table.error().contexts().size(), 1U);
    EXPECT_EQ(missing_table.error().contexts().front().component, "planner.path");
    EXPECT_EQ(missing_table.error().contexts().front().operation, "lookup base relation");

    const ThrowingMetadataPort index_failure{
        planning_table(),
        MetadataExceptionPoint::kIndexLookup,
    };
    const auto missing_indexes = build_base_relation_paths(index_failure, base_table(), nullptr);
    ASSERT_FALSE(missing_indexes);
    EXPECT_EQ(missing_indexes.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ(
        missing_indexes.error().message(),
        std::string_view{"planner metadata port exhausted available memory"}
    );
    ASSERT_EQ(missing_indexes.error().contexts().size(), 1U);
    EXPECT_EQ(missing_indexes.error().contexts().front().component, "planner.path");
    EXPECT_EQ(missing_indexes.error().contexts().front().operation, "lookup indexes");
}

}  // namespace
}  // namespace db::planner
