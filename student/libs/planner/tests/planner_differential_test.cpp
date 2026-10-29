#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/planner/explain.hpp"
#include "db/planner/lowering.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/planner/plan.hpp"
#include "db/sql/binder.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/parser.hpp"
#include "db/types/data_type.hpp"

namespace db::planner {
namespace {

class FixtureCatalogReadPort final : public sql::CatalogReadPort {
public:
    explicit FixtureCatalogReadPort(std::vector<sql::CatalogTable> tables)
        : tables_{std::move(tables)} {}

    auto generation() const noexcept -> std::uint64_t override {
        return 41U;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<sql::CatalogTable>> override {
        const auto found =
            std::find_if(tables_.begin(), tables_.end(), [name](const sql::CatalogTable& table) {
                return table.name == name;
            });
        if (found == tables_.end()) {
            return std::optional<sql::CatalogTable>{};
        }
        return std::optional<sql::CatalogTable>{*found};
    }

private:
    std::vector<sql::CatalogTable> tables_;
};

class FixtureMetadataPort final : public MetadataPort {
public:
    FixtureMetadataPort(std::vector<PlanningTable> tables, std::vector<PlanningIndex> indexes)
        : tables_{std::move(tables)},
          indexes_{std::move(indexes)} {}

    auto generation() const noexcept -> std::uint64_t override {
        return 41U;
    }

    auto lookup_table(const base::Oid oid) const
        -> base::Result<std::optional<PlanningTable>> override {
        const auto found =
            std::find_if(tables_.begin(), tables_.end(), [oid](const PlanningTable& table) {
                return table.oid == oid;
            });
        if (found == tables_.end()) {
            return std::optional<PlanningTable>{};
        }
        return std::optional<PlanningTable>{*found};
    }

    auto indexes_for_table(const base::Oid table_oid) const
        -> base::Result<std::vector<PlanningIndex>> override {
        std::vector<PlanningIndex> result;
        for (const PlanningIndex& index : indexes_) {
            const auto found = std::find_if(
                index_tables_.begin(),
                index_tables_.end(),
                [&index](const auto& entry) {
                    return entry.first == index.name;
                }
            );
            if (found != index_tables_.end() && found->second == table_oid) {
                result.push_back(index);
            }
        }
        return result;
    }

    void add_index_table(std::string name, const base::Oid table_oid) {
        index_tables_.emplace_back(std::move(name), table_oid);
    }

private:
    std::vector<PlanningTable> tables_;
    std::vector<PlanningIndex> indexes_;
    std::vector<std::pair<std::string, base::Oid>> index_tables_;
};

auto table(const base::Oid oid, std::string name, std::initializer_list<sql::CatalogColumn> columns)
    -> sql::CatalogTable {
    return sql::CatalogTable{
        .oid = oid,
        .name = std::move(name),
        .columns = columns,
    };
}

auto catalog_tables() -> std::vector<sql::CatalogTable> {
    return {
        table(
            base::Oid{101U},
            "t",
            {{.name = "id", .data_type = types::kInt64Type},
             {.name = "name",
              .data_type =
                  types::DataType{
                      types::kCharTypeId,
                      types::TypeModifier{32},
                  }}}
        ),
        table(
            base::Oid{102U},
            "u",
            {{.name = "x", .data_type = types::kInt64Type},
             {.name = "y", .data_type = types::kInt64Type}}
        ),
        table(base::Oid{103U}, "v", {{.name = "z", .data_type = types::kInt64Type}}),
        table(
            base::Oid{104U},
            "orders",
            {{.name = "tenant_id", .data_type = types::kInt64Type},
             {.name = "bucket_id", .data_type = types::kInt64Type},
             {.name = "order_id", .data_type = types::kInt64Type}}
        ),
    };
}

auto planning_table(const sql::CatalogTable& source) -> PlanningTable {
    PlanningTable result{.oid = source.oid, .columns = {}};
    result.columns.reserve(source.columns.size());
    for (const sql::CatalogColumn& column : source.columns) {
        result.columns.push_back(
            PlanningColumn{
                .name = column.name,
                .data_type = column.data_type,
            }
        );
    }
    return result;
}

auto metadata_port(const bool indexed = false) -> std::unique_ptr<FixtureMetadataPort> {
    const std::vector<sql::CatalogTable> tables = catalog_tables();
    std::vector<PlanningTable> planning_tables;
    planning_tables.reserve(tables.size());
    for (const sql::CatalogTable& source : tables) {
        planning_tables.push_back(planning_table(source));
    }

    std::vector<PlanningIndex> indexes;
    if (indexed) {
        indexes = {
            PlanningIndex{
                .oid = base::Oid{201U},
                .name = "t_id_idx",
                .column_names = {"id"},
                .unique = true,
            },
            PlanningIndex{
                .oid = base::Oid{204U},
                .name = "t_name_idx",
                .column_names = {"name"},
                .unique = false,
            },
            PlanningIndex{
                .oid = base::Oid{202U},
                .name = "u_x_idx",
                .column_names = {"x"},
                .unique = false,
            },
            PlanningIndex{
                .oid = base::Oid{203U},
                .name = "orders_tenant_bucket_order_idx",
                .column_names = {"tenant_id", "bucket_id", "order_id"},
                .unique = true,
            },
        };
    }

    auto result =
        std::make_unique<FixtureMetadataPort>(std::move(planning_tables), std::move(indexes));
    if (indexed) {
        result->add_index_table("t_id_idx", base::Oid{101U});
        result->add_index_table("t_name_idx", base::Oid{101U});
        result->add_index_table("u_x_idx", base::Oid{102U});
        result->add_index_table("orders_tenant_bucket_order_idx", base::Oid{104U});
    }
    return result;
}

auto plan_sql(
    const std::string_view text,
    const bool indexed = false,
    const std::span<const types::DataType> parameter_types = {}
) -> base::Result<PlannedStatement> {
    const FixtureCatalogReadPort catalog{catalog_tables()};
    auto source = sql::parse_sql(text);
    if (!source) {
        return base::fail(std::move(source).error());
    }
    auto bound = sql::bind_script_with_parameters(catalog, source.value(), parameter_types);
    if (!bound) {
        return base::fail(std::move(bound).error());
    }
    if (bound->statements.size() != 1U) {
        return base::fail(
            base::Error{
                base::ErrorCode::kPlan,
                "planner differential corpus expects exactly one statement",
            }
        );
    }
    auto metadata = metadata_port(indexed);
    return lower_bound_statement(
        std::move(bound->statements.front()),
        bound->parameter_slots,
        *metadata
    );
}

auto unwrap_result(const Plan* const plan) -> const Plan* {
    const auto* const result = std::get_if<ResultPlan>(&plan->node);
    return result == nullptr ? plan : result->input.get();
}

auto contains_operator(const Plan& plan, const PlanOperatorKind expected) -> bool {
    if (plan_operator_kind(plan) == expected) {
        return true;
    }
    return std::visit(
        [expected](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (
                std::is_same_v<Node, ResultPlan> || std::is_same_v<Node, SortPlan>
                || std::is_same_v<Node, LimitPlan> || std::is_same_v<Node, TopNPlan>
                || std::is_same_v<Node, UniquePlan> || std::is_same_v<Node, AggregatePlan>
            ) {
                return node.input != nullptr && contains_operator(*node.input, expected);
            } else if constexpr (
                std::is_same_v<Node, NestedLoopPlan> || std::is_same_v<Node, MergeJoinPlan>
                || std::is_same_v<Node, HashJoinPlan>
            ) {
                return (node.outer != nullptr && contains_operator(*node.outer, expected))
                       || (node.inner != nullptr && contains_operator(*node.inner, expected));
            } else if constexpr (std::is_same_v<Node, SubqueryScanPlan>) {
                return node.subquery != nullptr && contains_operator(*node.subquery, expected);
            } else if constexpr (std::is_same_v<Node, SubPlanEvalPlan>) {
                if (node.input != nullptr && contains_operator(*node.input, expected)) {
                    return true;
                }
                return std::any_of(
                    node.subplans.begin(),
                    node.subplans.end(),
                    [expected](const PlanSubPlan& subplan) {
                        return subplan.plan != nullptr
                               && contains_operator(*subplan.plan, expected);
                    }
                );
            } else if constexpr (std::is_same_v<Node, ModifyTablePlan>) {
                const auto* const source = std::get_if<PlanQueryModificationSource>(&node.source);
                return source != nullptr && source->input != nullptr
                       && contains_operator(*source->input, expected);
            } else {
                return false;
            }
        },
        plan.node
    );
}

TEST(PlannerDifferentialTest, PreservesReferenceStatementFamiliesAndRejections) {
    const auto insert = plan_sql("INSERT INTO t (id, name) VALUES (1, 'hello')");
    ASSERT_TRUE(insert) << insert.error().to_string();
    const auto* const insert_plan = std::get_if<ModifyTablePlan>(&insert->root().node);
    ASSERT_NE(insert_plan, nullptr);
    EXPECT_EQ(insert_plan->operation, ModifyOperation::kInsert);

    const auto update = plan_sql("UPDATE t SET name = 'x' WHERE id = 1");
    ASSERT_TRUE(update) << update.error().to_string();
    const auto* const update_plan = std::get_if<ModifyTablePlan>(&update->root().node);
    ASSERT_NE(update_plan, nullptr);
    EXPECT_EQ(update_plan->operation, ModifyOperation::kUpdate);

    const auto deleted = plan_sql("DELETE FROM t WHERE id = 1");
    ASSERT_TRUE(deleted) << deleted.error().to_string();
    const auto* const delete_plan = std::get_if<ModifyTablePlan>(&deleted->root().node);
    ASSERT_NE(delete_plan, nullptr);
    EXPECT_EQ(delete_plan->operation, ModifyOperation::kDelete);

    for (const std::string_view unsupported : {
             "CREATE TABLE w (id INT)",
             "DROP TABLE t",
             "CREATE INDEX t_id_idx ON t (id)",
             "DROP INDEX t_id_idx",
             "BEGIN",
             "VACUUM t",
             "SELECT * FROM t LEFT JOIN u ON t.id = u.x",
         }) {
        const auto planned = plan_sql(unsupported);
        ASSERT_FALSE(planned) << unsupported;
        EXPECT_EQ(planned.error().code(), base::ErrorCode::kUnsupported) << unsupported;
    }
}

TEST(PlannerDifferentialTest, ChoosesEquivalentScanAndProjectionFamilies) {
    const auto sequential = plan_sql("SELECT * FROM t WHERE id = 1");
    ASSERT_TRUE(sequential) << sequential.error().to_string();
    EXPECT_EQ(plan_operator_kind(*unwrap_result(&sequential->root())), PlanOperatorKind::kSeqScan);

    const auto indexed = plan_sql("SELECT id + 1 AS next_id FROM t WHERE id > 5", true);
    ASSERT_TRUE(indexed) << indexed.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&indexed->root().node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    EXPECT_EQ(plan_operator_kind(*result->input), PlanOperatorKind::kIndexScan);
    ASSERT_EQ(result->projection.size(), 1U);
    EXPECT_EQ(result->projection.front().output_name, "next_id");
}

TEST(PlannerDifferentialTest, UsesCharacterIndexWithoutNarrowingPreparedParameter) {
    const std::array parameter_types{types::kCharType};
    const auto planned = plan_sql("SELECT id FROM t WHERE name = $1", true, parameter_types);
    ASSERT_TRUE(planned) << planned.error().to_string();
    const auto* const input = unwrap_result(&planned->root());
    const auto explained = render_explain(*planned);
    ASSERT_TRUE(explained) << explained.error().to_string();
    ASSERT_EQ(plan_operator_kind(*input), PlanOperatorKind::kIndexScan) << explained.value();
    const auto* const scan = std::get_if<IndexScanPlan>(&input->node);
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->index.name, "t_name_idx");
    ASSERT_EQ(scan->bounds.size(), 1U);
    EXPECT_EQ(scan->bounds.front().key_index, PlanIndexKeyIndex{0U});
    EXPECT_EQ(scan->bounds.front().value->data_type, types::kCharType);
}

TEST(PlannerDifferentialTest, PreservesLogicalJoinSubqueryAndAggregateShapes) {
    const auto join = plan_sql("SELECT * FROM t JOIN u ON t.id = u.x");
    ASSERT_TRUE(join) << join.error().to_string();
    EXPECT_TRUE(contains_operator(join->root(), PlanOperatorKind::kHashJoin));

    const auto derived = plan_sql("SELECT d.id FROM (SELECT id FROM t LIMIT 5) AS d");
    ASSERT_TRUE(derived) << derived.error().to_string();
    EXPECT_TRUE(contains_operator(derived->root(), PlanOperatorKind::kSubqueryScan));
    EXPECT_TRUE(contains_operator(derived->root(), PlanOperatorKind::kLimit));

    const auto exists = plan_sql("SELECT * FROM t WHERE EXISTS (SELECT 1 FROM u WHERE u.x = t.id)");
    ASSERT_TRUE(exists) << exists.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&exists->root().node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_EQ(evaluation->subplans.size(), 1U);
    EXPECT_EQ(evaluation->subplans.front().strategy, PlanSubPlanStrategy::kExists);
    EXPECT_EQ(evaluation->subplans.front().parameters.size(), 1U);

    const auto aggregate = plan_sql("SELECT name, COUNT(*) FROM t GROUP BY name");
    ASSERT_TRUE(aggregate) << aggregate.error().to_string();
    EXPECT_TRUE(contains_operator(aggregate->root(), PlanOperatorKind::kAggregate));

    const auto transparent = plan_sql(
        "SELECT sub.id FROM (SELECT id, name FROM t WHERE id > 1) AS sub WHERE sub.id = 7"
    );
    ASSERT_TRUE(transparent) << transparent.error().to_string();
    EXPECT_FALSE(contains_operator(transparent->root(), PlanOperatorKind::kSubqueryScan));

    const auto transparent_join = plan_sql(
        "SELECT t.id, sub.x FROM t, (SELECT x, y FROM u WHERE x > 5) AS sub "
        "WHERE t.id = sub.x"
    );
    ASSERT_TRUE(transparent_join) << transparent_join.error().to_string();
    EXPECT_FALSE(contains_operator(transparent_join->root(), PlanOperatorKind::kSubqueryScan));
    EXPECT_TRUE(contains_operator(transparent_join->root(), PlanOperatorKind::kHashJoin));

    const auto multi_relation = plan_sql("SELECT * FROM t, u, v WHERE t.id = u.x AND u.y = v.z");
    ASSERT_TRUE(multi_relation) << multi_relation.error().to_string();
    EXPECT_TRUE(contains_operator(multi_relation->root(), PlanOperatorKind::kHashJoin));
}

TEST(PlannerDifferentialTest, PreservesOrderLimitAndDistinctOperatorBoundaries) {
    const auto ordered = plan_sql("SELECT * FROM t ORDER BY id DESC NULLS LAST");
    ASSERT_TRUE(ordered) << ordered.error().to_string();
    EXPECT_EQ(plan_operator_kind(ordered->root()), PlanOperatorKind::kSort);

    const auto index_order =
        plan_sql("SELECT id FROM t WHERE id > 0 ORDER BY id ASC NULLS FIRST", true);
    ASSERT_TRUE(index_order) << index_order.error().to_string();
    EXPECT_EQ(
        plan_operator_kind(*unwrap_result(&index_order->root())),
        PlanOperatorKind::kIndexScan
    );

    const auto top_n = plan_sql("SELECT * FROM t ORDER BY id LIMIT 5");
    ASSERT_TRUE(top_n) << top_n.error().to_string();
    EXPECT_EQ(plan_operator_kind(top_n->root()), PlanOperatorKind::kTopN);

    const auto offset = plan_sql("SELECT * FROM t ORDER BY id OFFSET 5");
    ASSERT_TRUE(offset) << offset.error().to_string();
    EXPECT_EQ(plan_operator_kind(offset->root()), PlanOperatorKind::kLimit);

    const auto distinct = plan_sql("SELECT DISTINCT name FROM t ORDER BY name");
    ASSERT_TRUE(distinct) << distinct.error().to_string();
    EXPECT_EQ(plan_operator_kind(distinct->root()), PlanOperatorKind::kSort);
    EXPECT_TRUE(contains_operator(distinct->root(), PlanOperatorKind::kUnique));
}

TEST(PlannerDifferentialTest, UnwrapsExplainAndRendersStablePhysicalText) {
    const auto explained = plan_sql("EXPLAIN SELECT id FROM t WHERE id = 7", true);
    ASSERT_TRUE(explained) << explained.error().to_string();
    EXPECT_EQ(explained->catalog_generation(), 41U);
    EXPECT_EQ(plan_operator_kind(*unwrap_result(&explained->root())), PlanOperatorKind::kIndexScan);

    const auto first = render_explain(*explained);
    ASSERT_TRUE(first) << first.error().to_string();
    const auto second = render_explain(*explained);
    ASSERT_TRUE(second) << second.error().to_string();
    EXPECT_EQ(first.value(), second.value());
    EXPECT_NE(first->find("Index Scan using t_id_idx on t"), std::string::npos);
    EXPECT_NE(first->find("Index Cond: (id = 7)"), std::string::npos);
}

TEST(PlannerDifferentialTest, KeepsExternalParametersAndCompositeIndexBoundsTyped) {
    const std::array parameter_types{
        types::kInt64Type,
        types::kInt64Type,
        types::kInt64Type,
    };
    const auto planned = plan_sql(
        "SELECT order_id FROM orders "
        "WHERE tenant_id = $1 AND bucket_id = $2 AND order_id = $3",
        true,
        parameter_types
    );
    ASSERT_TRUE(planned) << planned.error().to_string();
    ASSERT_EQ(planned->parameter_namespace().parameters().size(), 3U);
    EXPECT_EQ(
        planned->parameter_namespace().parameters().front().kind,
        PlanParameterKind::kExternal
    );
    const auto* const root = unwrap_result(&planned->root());
    const auto* const scan = std::get_if<IndexScanPlan>(&root->node);
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->index.name, "orders_tenant_bucket_order_idx");
    ASSERT_EQ(scan->bounds.size(), 3U);
    for (std::size_t index = 0U; index < scan->bounds.size(); ++index) {
        const auto* const parameter =
            std::get_if<PlanParameterReference>(&scan->bounds.at(index).value->node);
        ASSERT_NE(parameter, nullptr);
        EXPECT_EQ(parameter->kind, PlanParameterKind::kExternal);
        EXPECT_EQ(parameter->id, PlanParameterId{static_cast<std::uint32_t>(index)});
    }
}

TEST(PlannerDifferentialTest, PreservesCompositeIndexOrderedMinimumBoundary) {
    const auto complete =
        plan_sql("SELECT MIN(order_id) FROM orders WHERE tenant_id = 1 AND bucket_id = 2", true);
    ASSERT_TRUE(complete) << complete.error().to_string();
    const auto* const complete_result = std::get_if<ResultPlan>(&complete->root().node);
    ASSERT_NE(complete_result, nullptr);
    ASSERT_NE(complete_result->input, nullptr);
    const auto* const complete_aggregate =
        std::get_if<AggregatePlan>(&complete_result->input->node);
    ASSERT_NE(complete_aggregate, nullptr);
    EXPECT_TRUE(complete_aggregate->ordered_min);
    ASSERT_NE(complete_aggregate->input, nullptr);
    EXPECT_EQ(plan_operator_kind(*complete_aggregate->input), PlanOperatorKind::kIndexScan);

    const auto incomplete = plan_sql("SELECT MIN(order_id) FROM orders WHERE tenant_id = 1", true);
    ASSERT_TRUE(incomplete) << incomplete.error().to_string();
    const auto* const incomplete_result = std::get_if<ResultPlan>(&incomplete->root().node);
    ASSERT_NE(incomplete_result, nullptr);
    ASSERT_NE(incomplete_result->input, nullptr);
    const auto* const incomplete_aggregate =
        std::get_if<AggregatePlan>(&incomplete_result->input->node);
    ASSERT_NE(incomplete_aggregate, nullptr);
    EXPECT_FALSE(incomplete_aggregate->ordered_min);
}

}  // namespace
}  // namespace db::planner
