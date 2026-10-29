#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/planner/join_order.hpp"
#include "db/sql/ast_common.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {
namespace {

auto relation(const std::uint32_t index) -> QueryRangeTableIndex {
    return QueryRangeTableIndex{index};
}

auto column(const std::uint32_t index) -> QueryColumnIndex {
    return QueryColumnIndex{index};
}

auto domain(std::initializer_list<std::uint32_t> indexes) -> InnerJoinDomain {
    InnerJoinDomain result{};
    result.range_table_indexes.reserve(indexes.size());
    for (const std::uint32_t index : indexes) {
        result.range_table_indexes.push_back(relation(index));
    }
    return result;
}

auto expression_ptr(PreprocessedExpression expression) -> PreprocessedExpressionPtr {
    return std::make_unique<PreprocessedExpression>(std::move(expression));
}

auto source_column(
    const std::uint32_t relation_index,
    const std::uint32_t column_index,
    const types::DataType data_type = types::kInt64Type,
    const std::uint32_t outer_query_levels = 0U
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        data_type,
        QueryColumnReference{
            .range_table_index = relation(relation_index),
            .column_index = column(column_index),
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

auto source_boolean_literal(const bool value) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryLiteralExpression{.value = types::Value::boolean(value)},
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

auto source_and(std::vector<PreprocessedExpressionPtr> operands) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryBooleanExpression{
            .op = types::BooleanOperator::kAnd,
            .operands = std::move(operands),
        },
    };
}

auto table_ptr(PreprocessedTableReference table) -> PreprocessedTableReferencePtr {
    return std::make_unique<PreprocessedTableReference>(std::move(table));
}

auto base_table_reference(const std::uint32_t relation_index, const std::uint32_t oid)
    -> PreprocessedTableReference {
    return PreprocessedTableReference{
        {},
        PreprocessedTableReference::Node{PreprocessedBaseTable{
            .oid = base::Oid{oid},
            .name = "t" + std::to_string(relation_index),
            .alias = std::nullopt,
            .columns = {QueryColumn{.name = "id", .data_type = types::kInt64Type}},
            .range_table_index = relation(relation_index),
        }},
    };
}

auto inner_join_reference(
    PreprocessedTableReferencePtr left,
    PreprocessedTableReferencePtr right,
    PreprocessedExpressionPtr on
) -> PreprocessedTableReference {
    return PreprocessedTableReference{
        {},
        PreprocessedTableReference::Node{PreprocessedJoinTable{
            .kind = sql::JoinKind::kInner,
            .left = std::move(left),
            .right = std::move(right),
            .condition = PreprocessedJoinOnCondition{.expression = std::move(on)},
        }},
    };
}

auto base_paths(
    const std::uint32_t relation_index,
    const std::uint32_t oid,
    const std::uint64_t generation = 17U
) -> BaseRelationPathList {
    return BaseRelationPathList{
        .catalog_generation = generation,
        .range_table_index = relation(relation_index),
        .relation =
            PlanRelation{
                .oid = base::Oid{oid},
                .name = "t" + std::to_string(relation_index),
                .alias = std::nullopt,
                .schema =
                    PlanSchema{
                        .columns = {PlanOutputColumn{
                            .name = "id",
                            .data_type = types::kInt64Type,
                            .origin =
                                PlanColumnOrigin{
                                    .relation_oid = base::Oid{oid},
                                    .relation_column = PlanColumnIndex{0U},
                                },
                        }},
                    },
            },
        .paths = {SequentialScanPath{}},
        .selected_path_index = 0U,
    };
}

auto ordered_relation_indexes(const JoinOrderSearchResult& result) -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> indexes;
    indexes.reserve(result.steps.size());
    for (const JoinOrderStep& step : result.steps) {
        indexes.push_back(step.added_relation.zero_based());
    }
    return indexes;
}

auto has_source(
    const std::vector<JoinQualificationSource>& sources,
    const std::size_t source_clause_index,
    const std::size_t conjunct_index
) -> bool {
    return std::any_of(
        sources.begin(),
        sources.end(),
        [source_clause_index, conjunct_index](const JoinQualificationSource& source) {
            return source.source_clause_index == source_clause_index
                   && source.conjunct_index == conjunct_index;
        }
    );
}

TEST(JoinOrderTest, PlacesClausesAtTheirLowestLegalInnerDomainNode) {
    const std::vector paths{
        base_paths(0U, 101U),
        base_paths(1U, 102U),
        base_paths(2U, 103U),
    };
    std::vector<PreprocessedExpressionPtr> conjunctions;
    conjunctions.push_back(expression_ptr(source_equal(
        expression_ptr(source_column(0U, 0U)),
        expression_ptr(source_integer_literal(7))
    )));
    conjunctions.push_back(expression_ptr(
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)))
    ));
    conjunctions.push_back(expression_ptr(
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(2U, 0U)))
    ));
    conjunctions.push_back(expression_ptr(source_boolean_literal(true)));
    PreprocessedExpression source = source_and(std::move(conjunctions));
    const std::vector<const PreprocessedExpression*> sources{&source};
    const ImpliedBaseEquality implied{
        .column =
            EquivalenceColumn{
                .range_table_index = relation(1U),
                .column_index = column(0U),
                .data_type = types::kInt64Type,
            },
        .pseudoconstant = EquivalenceLiteral{.value = types::Value::integer(7)},
    };

    const auto placement = derive_inner_join_qualification_placements(
        domain({0U, 1U, 2U}),
        sources,
        paths,
        std::vector<ImpliedBaseEquality>{implied}
    );
    ASSERT_TRUE(placement) << placement.error().to_string();
    ASSERT_EQ(placement->base_placements.size(), 3U);
    EXPECT_TRUE(has_source(placement->base_placements[0U].source_qualifications, 0U, 0U));
    EXPECT_EQ(placement->base_placements[1U].implied_equalities.size(), 1U);
    EXPECT_EQ(placement->join_qualifications.size(), 2U);
    EXPECT_TRUE(has_source(placement->deferred_qualifications, 0U, 3U));
    EXPECT_EQ(placement->join_qualifications[0U].required_relations.size(), 2U);
    EXPECT_EQ(placement->join_qualifications[1U].required_relations.size(), 2U);
}

TEST(JoinOrderTest, DefersOuterReferencesRatherThanMovingThemToABasePath) {
    const std::vector paths{
        base_paths(0U, 101U),
        base_paths(1U, 102U),
        base_paths(2U, 103U),
    };
    PreprocessedExpression source = source_equal(
        expression_ptr(source_column(0U, 0U)),
        expression_ptr(source_column(7U, 0U, types::kInt64Type, 1U))
    );
    const std::vector<const PreprocessedExpression*> sources{&source};

    const auto placement =
        derive_inner_join_qualification_placements(domain({0U, 1U, 2U}), sources, paths);
    ASSERT_TRUE(placement) << placement.error().to_string();
    EXPECT_TRUE(placement->join_qualifications.empty());
    EXPECT_TRUE(placement->base_placements[0U].source_qualifications.empty());
    EXPECT_TRUE(has_source(placement->deferred_qualifications, 0U, 0U));
}

TEST(JoinOrderTest, CollectsInnerJoinTreeOnClausesAndWhereInStableOrder) {
    PreprocessedTableReference tree = inner_join_reference(
        table_ptr(inner_join_reference(
            table_ptr(base_table_reference(0U, 101U)),
            table_ptr(base_table_reference(1U, 102U)),
            expression_ptr(source_equal(
                expression_ptr(source_column(0U, 0U)),
                expression_ptr(source_column(1U, 0U))
            ))
        )),
        table_ptr(base_table_reference(2U, 103U)),
        expression_ptr(source_equal(
            expression_ptr(source_column(1U, 0U)),
            expression_ptr(source_column(2U, 0U))
        ))
    );
    const PreprocessedExpression where_filter = source_equal(
        expression_ptr(source_column(0U, 0U)),
        expression_ptr(source_integer_literal(7))
    );

    const auto input = collect_small_inner_join_input(tree, &where_filter);
    ASSERT_TRUE(input) << input.error().to_string();
    EXPECT_EQ(
        input->domain.range_table_indexes,
        std::vector<QueryRangeTableIndex>({relation(0U), relation(1U), relation(2U)})
    );
    ASSERT_EQ(input->source_clauses.size(), 3U);
    const auto* const first_join =
        std::get_if<PreprocessedJoinTable>(&std::get<PreprocessedJoinTable>(tree.node).left->node);
    ASSERT_NE(first_join, nullptr);
    const auto* const first_on = std::get_if<PreprocessedJoinOnCondition>(&first_join->condition);
    const auto* const second_on = std::get_if<PreprocessedJoinOnCondition>(
        &std::get<PreprocessedJoinTable>(tree.node).condition
    );
    ASSERT_NE(first_on, nullptr);
    ASSERT_NE(second_on, nullptr);
    EXPECT_EQ(input->source_clauses[0U], second_on->expression.get());
    EXPECT_EQ(input->source_clauses[1U], first_on->expression.get());
    EXPECT_EQ(input->source_clauses[2U], &where_filter);
}

TEST(JoinOrderTest, RejectsStaleCurrentLevelColumnMetadata) {
    const std::vector paths{
        base_paths(0U, 101U),
        base_paths(1U, 102U),
        base_paths(2U, 103U),
    };
    const PreprocessedExpression qualification =
        source_equal(expression_ptr(source_column(0U, 4U)), expression_ptr(source_column(1U, 0U)));
    const std::vector<const PreprocessedExpression*> sources{&qualification};

    const auto placement =
        derive_inner_join_qualification_placements(domain({0U, 1U, 2U}), sources, paths);
    ASSERT_FALSE(placement);
    EXPECT_EQ(placement.error().code(), base::ErrorCode::kPlan);
}

TEST(JoinOrderTest, UsesStructuralJoinConnectivityThenStableRelationIndex) {
    const std::vector paths{
        base_paths(2U, 103U),
        base_paths(0U, 101U),
        base_paths(1U, 102U),
    };
    const std::vector qualifications{
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 0U},
            .required_relations = {relation(0U), relation(1U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 1U},
            .required_relations = {relation(1U), relation(2U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 2U},
            .required_relations = {relation(0U), relation(1U), relation(2U)},
        },
    };

    const auto result = choose_small_inner_join_order(paths, qualifications);
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_EQ(ordered_relation_indexes(*result), std::vector<std::uint32_t>({0U, 1U, 2U}));
    ASSERT_EQ(result->steps.size(), 3U);
    EXPECT_TRUE(result->steps[0U].applied_qualifications.empty());
    EXPECT_TRUE(has_source(result->steps[1U].applied_qualifications, 0U, 0U));
    EXPECT_TRUE(has_source(result->steps[2U].applied_qualifications, 0U, 1U));
    EXPECT_TRUE(has_source(result->steps[2U].applied_qualifications, 0U, 2U));
    EXPECT_EQ(result->steps[1U].left_relations, std::vector<QueryRangeTableIndex>({relation(0U)}));
}

TEST(JoinOrderTest, UsesStableRelationIndexWhenNoJoinEdgeIsAvailable) {
    const std::vector paths{
        base_paths(5U, 105U),
        base_paths(3U, 103U),
        base_paths(4U, 104U),
    };
    const auto result = choose_small_inner_join_order(paths, {});
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_EQ(ordered_relation_indexes(*result), std::vector<std::uint32_t>({3U, 4U, 5U}));
}

TEST(JoinOrderTest, RoutesDenseSixRelationQualificationsThroughStablePositions) {
    const std::vector paths{
        base_paths(5U, 105U),
        base_paths(2U, 102U),
        base_paths(0U, 100U),
        base_paths(4U, 104U),
        base_paths(1U, 101U),
        base_paths(3U, 103U),
    };
    const std::vector qualifications{
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 0U},
            .required_relations = {relation(0U), relation(4U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 1U},
            .required_relations = {relation(0U), relation(4U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 2U},
            .required_relations = {relation(0U), relation(1U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 3U},
            .required_relations = {relation(0U), relation(1U), relation(2U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 4U},
            .required_relations = {relation(1U), relation(2U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 5U},
            .required_relations = {relation(2U), relation(3U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 6U},
            .required_relations = {relation(3U), relation(4U)},
        },
        JoinQualification{
            .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 7U},
            .required_relations = {relation(4U), relation(5U)},
        },
    };

    const auto result = choose_small_inner_join_order(paths, qualifications);
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_EQ(
        ordered_relation_indexes(*result),
        std::vector<std::uint32_t>({0U, 4U, 1U, 2U, 3U, 5U})
    );
    ASSERT_EQ(result->steps.size(), 6U);
    EXPECT_TRUE(result->steps.front().applied_qualifications.empty());
    EXPECT_EQ(result->steps[1U].applied_qualifications.size(), 2U);
    EXPECT_EQ(result->steps[3U].applied_qualifications.size(), 2U);
    EXPECT_EQ(result->steps[4U].applied_qualifications.size(), 2U);
    EXPECT_EQ(result->steps[5U].applied_qualifications.size(), 1U);
}

TEST(JoinOrderTest, RejectsInvalidGenerationAndQualificationInputs) {
    const std::vector one_relation{base_paths(0U, 101U)};
    const auto too_small = choose_small_inner_join_order(one_relation, {});
    ASSERT_FALSE(too_small);
    EXPECT_EQ(too_small.error().code(), base::ErrorCode::kPlan);

    const std::vector mixed_generation{
        base_paths(0U, 101U, 17U),
        base_paths(1U, 102U, 18U),
        base_paths(2U, 103U, 17U),
    };
    const auto mixed = choose_small_inner_join_order(mixed_generation, {});
    ASSERT_FALSE(mixed);
    EXPECT_EQ(mixed.error().code(), base::ErrorCode::kUnavailable);

    const std::vector valid_paths{
        base_paths(0U, 101U),
        base_paths(1U, 102U),
        base_paths(2U, 103U),
    };
    const std::vector invalid_qualifications{JoinQualification{
        .source = JoinQualificationSource{},
        .required_relations = {relation(0U)},
    }};
    const auto invalid = choose_small_inner_join_order(valid_paths, invalid_qualifications);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kPlan);

    const std::vector unsorted_qualifications{JoinQualification{
        .source = JoinQualificationSource{.source_clause_index = 0U, .conjunct_index = 0U},
        .required_relations = {relation(2U), relation(0U)},
    }};
    const auto unsorted = choose_small_inner_join_order(valid_paths, unsorted_qualifications);
    ASSERT_FALSE(unsorted);
    EXPECT_EQ(unsorted.error().code(), base::ErrorCode::kPlan);
}

class FakeMetadataPort final : public MetadataPort {
public:
    explicit FakeMetadataPort(
        const std::uint64_t generation,
        std::vector<PlanningTable> tables,
        std::vector<PlanningIndex> indexes = {},
        std::vector<base::Oid> index_table_oids = {},
        const bool changes_generation = false
    )
        : generation_{generation},
          tables_{std::move(tables)},
          indexes_{std::move(indexes)},
          index_table_oids_{std::move(index_table_oids)},
          changes_generation_{changes_generation} {}

    auto generation() const noexcept -> std::uint64_t override {
        return generation_;
    }

    auto lookup_table(const base::Oid oid) const
        -> base::Result<std::optional<PlanningTable>> override {
        const auto found =
            std::find_if(tables_.begin(), tables_.end(), [oid](const PlanningTable& table) {
                return table.oid == oid;
            });
        return found == tables_.end() ? std::optional<PlanningTable>{}
                                      : std::optional<PlanningTable>{*found};
    }

    auto indexes_for_table(const base::Oid table_oid) const
        -> base::Result<std::vector<PlanningIndex>> override {
        if (changes_generation_) {
            ++generation_;
        }
        std::vector<PlanningIndex> result;
        for (std::size_t index = 0U; index < indexes_.size(); ++index) {
            if (index < index_table_oids_.size() && index_table_oids_[index] == table_oid) {
                result.push_back(indexes_[index]);
            }
        }
        return result;
    }

private:
    mutable std::uint64_t generation_;
    std::vector<PlanningTable> tables_;
    std::vector<PlanningIndex> indexes_;
    std::vector<base::Oid> index_table_oids_;
    bool changes_generation_;
};

auto planning_table(const std::uint32_t oid) -> PlanningTable {
    return PlanningTable{
        .oid = base::Oid{oid},
        .columns = {PlanningColumn{.name = "id", .data_type = types::kInt64Type}},
    };
}

auto bound_base_table(const std::uint32_t relation_index, const std::uint32_t oid)
    -> PreprocessedBaseTable {
    return PreprocessedBaseTable{
        .oid = base::Oid{oid},
        .name = "t" + std::to_string(relation_index),
        .alias = std::nullopt,
        .columns = {QueryColumn{.name = "id", .data_type = types::kInt64Type}},
        .range_table_index = relation(relation_index),
    };
}

TEST(JoinOrderTest, CompositionBuildsOnlyLocalBasePathsThenOrdersCrossRelationClauses) {
    const FakeMetadataPort port{
        31U,
        {planning_table(201U), planning_table(202U), planning_table(203U)},
    };
    std::vector<PreprocessedExpressionPtr> conjunctions;
    conjunctions.push_back(expression_ptr(source_equal(
        expression_ptr(source_column(0U, 0U)),
        expression_ptr(source_integer_literal(7))
    )));
    conjunctions.push_back(expression_ptr(
        source_equal(expression_ptr(source_column(0U, 0U)), expression_ptr(source_column(1U, 0U)))
    ));
    conjunctions.push_back(expression_ptr(
        source_equal(expression_ptr(source_column(1U, 0U)), expression_ptr(source_column(2U, 0U)))
    ));
    PreprocessedExpression source = source_and(std::move(conjunctions));
    const std::vector<const PreprocessedExpression*> sources{&source};
    const std::vector base_tables{
        bound_base_table(0U, 201U),
        bound_base_table(1U, 202U),
        bound_base_table(2U, 203U),
    };

    const auto result =
        build_small_inner_join_paths(port, domain({0U, 1U, 2U}), base_tables, sources, {});
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->base_paths.size(), 3U);
    EXPECT_EQ(result->base_paths[0U].selected_path_index, 0U);
    EXPECT_EQ(result->placements.join_qualifications.size(), 2U);
    EXPECT_EQ(result->order.catalog_generation, 31U);
    EXPECT_EQ(ordered_relation_indexes(result->order), std::vector<std::uint32_t>({0U, 1U, 2U}));
}

TEST(JoinOrderTest, TreeAdapterOverloadBuildsOwnedPathsWithoutManualClauseAssembly) {
    const FakeMetadataPort port{
        31U,
        {planning_table(201U), planning_table(202U), planning_table(203U)},
    };
    PreprocessedTableReference tree = inner_join_reference(
        table_ptr(inner_join_reference(
            table_ptr(base_table_reference(0U, 201U)),
            table_ptr(base_table_reference(1U, 202U)),
            expression_ptr(source_equal(
                expression_ptr(source_column(0U, 0U)),
                expression_ptr(source_column(1U, 0U))
            ))
        )),
        table_ptr(base_table_reference(2U, 203U)),
        expression_ptr(source_equal(
            expression_ptr(source_column(1U, 0U)),
            expression_ptr(source_column(2U, 0U))
        ))
    );
    const auto input = collect_small_inner_join_input(tree);
    ASSERT_TRUE(input) << input.error().to_string();
    auto parameters = ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(parameters) << parameters.error().to_string();

    const auto result = build_small_inner_join_paths(port, *input, *parameters);
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_EQ(result->base_paths.size(), 3U);
    EXPECT_EQ(result->placements.join_qualifications.size(), 2U);
    EXPECT_EQ(result->order.catalog_generation, 31U);
}

TEST(JoinOrderTest, CompositionRejectsMetadataGenerationDriftAcrossRelations) {
    const FakeMetadataPort port{
        31U,
        {planning_table(201U), planning_table(202U), planning_table(203U)},
        {},
        {},
        true,
    };
    const std::vector base_tables{
        bound_base_table(0U, 201U),
        bound_base_table(1U, 202U),
        bound_base_table(2U, 203U),
    };
    const std::vector<const PreprocessedExpression*> no_clauses;

    const auto result =
        build_small_inner_join_paths(port, domain({0U, 1U, 2U}), base_tables, no_clauses, {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

}  // namespace
}  // namespace db::planner
