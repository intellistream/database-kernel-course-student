#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/planner/explain.hpp"
#include "db/planner/lowering.hpp"
#include "db/sql/binder.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/parser.hpp"
#include "db/types/data_type.hpp"

namespace db::planner {
namespace {

class FakeCatalogReadPort final : public sql::CatalogReadPort {
public:
    FakeCatalogReadPort(const std::uint64_t generation, std::vector<sql::CatalogTable> tables)
        : generation_{generation},
          tables_{std::move(tables)} {}

    auto generation() const noexcept -> std::uint64_t override {
        return generation_;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<sql::CatalogTable>> override {
        const auto found =
            std::find_if(tables_.begin(), tables_.end(), [name](const sql::CatalogTable& table) {
                return table.name == name;
            });
        return found == tables_.end() ? std::optional<sql::CatalogTable>{}
                                      : std::optional<sql::CatalogTable>{*found};
    }

private:
    std::uint64_t generation_;
    std::vector<sql::CatalogTable> tables_;
};

class FakeMetadataPort final : public MetadataPort {
public:
    FakeMetadataPort(
        const std::uint64_t generation,
        std::vector<PlanningTable> tables,
        std::vector<PlanningIndex> indexes,
        const bool changes_generation = false
    )
        : generation_{generation},
          tables_{std::move(tables)},
          indexes_{std::move(indexes)},
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
        for (const PlanningIndex& index : indexes_) {
            if (index_table_oid(index) == table_oid) {
                result.push_back(index);
            }
        }
        return result;
    }

    // Test metadata is intentionally tiny. The indexes below follow the same
    // table names as their single compatible planning table, so their OID is
    // kept in this private lookup rather than leaked into MetadataPort.
    void assign_index_table(std::string name, const base::Oid table_oid) {
        index_tables_.emplace_back(std::move(name), table_oid);
    }

private:
    auto index_table_oid(const PlanningIndex& index) const -> base::Oid {
        const auto found =
            std::find_if(index_tables_.begin(), index_tables_.end(), [&index](const auto& entry) {
                return entry.first == index.name;
            });
        return found == index_tables_.end() ? base::kInvalidOid : found->second;
    }

    mutable std::uint64_t generation_;
    std::vector<PlanningTable> tables_;
    std::vector<PlanningIndex> indexes_;
    std::vector<std::pair<std::string, base::Oid>> index_tables_;
    bool changes_generation_;
};

auto accounts_catalog_table() -> sql::CatalogTable {
    return sql::CatalogTable{
        .oid = base::Oid{42U},
        .name = "accounts",
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "enabled", .data_type = types::kBoolType},
            {.name = "score", .data_type = types::kFloat32Type}
        },
    };
}

auto ledger_catalog_table() -> sql::CatalogTable {
    return sql::CatalogTable{
        .oid = base::Oid{43U},
        .name = "ledger",
        .columns = {
            {.name = "account_id", .data_type = types::kInt64Type},
            {.name = "amount", .data_type = types::kInt64Type}
        },
    };
}

auto customer_catalog_table() -> sql::CatalogTable {
    return sql::CatalogTable{
        .oid = base::Oid{44U},
        .name = "customer",
        .columns = {
            {.name = "c_id", .data_type = types::kInt64Type},
            {.name = "c_d_id", .data_type = types::kInt64Type},
            {.name = "c_w_id", .data_type = types::kInt64Type}
        },
    };
}

auto history_catalog_table() -> sql::CatalogTable {
    return sql::CatalogTable{
        .oid = base::Oid{45U},
        .name = "history",
        .columns = {
            {.name = "h_c_id", .data_type = types::kInt64Type},
            {.name = "h_c_d_id", .data_type = types::kInt64Type},
            {.name = "h_c_w_id", .data_type = types::kInt64Type}
        },
    };
}

auto planning_table(const sql::CatalogTable& table) -> PlanningTable {
    PlanningTable result{.oid = table.oid, .columns = {}};
    result.columns.reserve(table.columns.size());
    for (const sql::CatalogColumn& column : table.columns) {
        result.columns.push_back(
            PlanningColumn{.name = column.name, .data_type = column.data_type}
        );
    }
    return result;
}

auto metadata_port(const bool changes_generation = false) -> std::unique_ptr<FakeMetadataPort> {
    const sql::CatalogTable accounts = accounts_catalog_table();
    const sql::CatalogTable ledger = ledger_catalog_table();
    const sql::CatalogTable customer = customer_catalog_table();
    const sql::CatalogTable history = history_catalog_table();
    std::vector<PlanningTable> tables;
    tables.push_back(planning_table(accounts));
    tables.push_back(planning_table(ledger));
    tables.push_back(planning_table(customer));
    tables.push_back(planning_table(history));
    std::vector<PlanningIndex> indexes;
    indexes.push_back(
        PlanningIndex{
            .oid = base::Oid{71U},
            .name = "accounts_id_idx",
            .column_names = {"id"},
            .unique = true,
        }
    );
    indexes.push_back(
        PlanningIndex{
            .oid = base::Oid{72U},
            .name = "accounts_enabled_id_idx",
            .column_names = {"enabled", "id"},
            .unique = false,
        }
    );
    indexes.push_back(
        PlanningIndex{
            .oid = base::Oid{73U},
            .name = "ledger_account_idx",
            .column_names = {"account_id"},
            .unique = false,
        }
    );
    indexes.push_back(
        PlanningIndex{
            .oid = base::Oid{74U},
            .name = "ledger_amount_account_idx",
            .column_names = {"amount", "account_id"},
            .unique = false,
        }
    );
    indexes.push_back(
        PlanningIndex{
            .oid = base::Oid{75U},
            .name = "customer_warehouse_district_id_idx",
            .column_names = {"c_w_id", "c_d_id", "c_id"},
            .unique = true,
        }
    );
    auto result = std::make_unique<FakeMetadataPort>(
        9U,
        std::move(tables),
        std::move(indexes),
        changes_generation
    );
    result->assign_index_table("accounts_id_idx", accounts.oid);
    result->assign_index_table("accounts_enabled_id_idx", accounts.oid);
    result->assign_index_table("ledger_account_idx", ledger.oid);
    result->assign_index_table("ledger_amount_account_idx", ledger.oid);
    result->assign_index_table("customer_warehouse_district_id_idx", customer.oid);
    return result;
}

auto preprocess_sql(const std::string_view text) -> base::Result<PreprocessedQuery> {
    FakeCatalogReadPort catalog{
        9U,
        {accounts_catalog_table(),
         ledger_catalog_table(),
         customer_catalog_table(),
         history_catalog_table()},
    };
    auto parsed = sql::parse_sql(text);
    if (!parsed) {
        return base::fail(std::move(parsed).error());
    }
    auto bound = sql::bind_script(catalog, parsed.value());
    if (!bound) {
        return base::fail(std::move(bound).error());
    }
    if (bound->statements.size() != 1U) {
        return base::fail(base::Error{base::ErrorCode::kPlan, "test expects one statement"});
    }
    sql::BoundStatement statement = std::move(bound->statements.front());
    auto* const select = std::get_if<sql::BoundSelectQuery>(&statement.node);
    if (select == nullptr) {
        return base::fail(base::Error{base::ErrorCode::kPlan, "test expects SELECT"});
    }
    return preprocess_bound_query(sql::BoundQuery{statement.span, std::move(*select)});
}

auto bind_sql(
    const std::string_view text,
    const std::span<const types::DataType> parameter_types = {}
) -> base::Result<sql::BoundScript> {
    FakeCatalogReadPort catalog{
        9U,
        {accounts_catalog_table(),
         ledger_catalog_table(),
         customer_catalog_table(),
         history_catalog_table()},
    };
    auto parsed = sql::parse_sql(text);
    if (!parsed) {
        return base::fail(std::move(parsed).error());
    }
    return sql::bind_script_with_parameters(catalog, parsed.value(), parameter_types);
}

auto empty_parameters() -> base::Result<ParameterNamespace> {
    return ParameterNamespace::from_external_slots({});
}

TEST(LoweringTest, RejectsAnInactiveParameterNamespaceBeforeReadingMetadata) {
    auto query = preprocess_sql("SELECT 42 AS answer");
    ASSERT_TRUE(query) << query.error().to_string();
    auto created = empty_parameters();
    ASSERT_TRUE(created) << created.error().to_string();
    ParameterNamespace source = std::move(created).value();
    ParameterNamespace moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    auto metadata = metadata_port();

    // Transfer the documented inert sentinel to verify the by-value lowering boundary.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    ParameterNamespace inactive = std::move(source);
    const auto lowered =
        lower_preprocessed_select(std::move(query).value(), *metadata, std::move(inactive));
    ASSERT_FALSE(lowered);
    EXPECT_EQ(lowered.error().code(), base::ErrorCode::kUnavailable);
}

TEST(LoweringTest, LowersConstantQueryIntoValuesThenResult) {
    auto query = preprocess_sql("SELECT 42 AS answer");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    EXPECT_EQ(lowered->catalog_generation, 9U);
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->metadata.output.columns.size(), 1U);
    EXPECT_EQ(result->metadata.output.columns.front().name, "answer");
    ASSERT_NE(result->input, nullptr);
    EXPECT_NE(std::get_if<ValuesPlan>(&result->input->node), nullptr);
}

TEST(LoweringTest, ValidatesARecursiveExpressionBeforeConvertingItsChildren) {
    auto query = preprocess_sql("SELECT id + 2 AS total FROM accounts");
    ASSERT_TRUE(query) << query.error().to_string();
    auto* const binary =
        std::get_if<QueryBinaryExpression>(&query->select.projection.front().expression->node);
    ASSERT_NE(binary, nullptr);
    binary->left.reset();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    const auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_FALSE(lowered);
    EXPECT_EQ(lowered.error().code(), base::ErrorCode::kPlan);
}

TEST(LoweringTest, LowersSelectedIndexBoundsAndExactScanDemand) {
    auto query = preprocess_sql("SELECT id FROM accounts WHERE id = 7");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const scan = std::get_if<IndexScanPlan>(&result->input->node);
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->index.name, "accounts_id_idx");
    ASSERT_EQ(scan->bounds.size(), 1U);
    EXPECT_EQ(scan->bounds.front().comparison, types::BinaryOperator::kEqual);
    ASSERT_TRUE(scan->required_columns.has_value());
    ASSERT_EQ(scan->required_columns->size(), 1U);
    EXPECT_EQ(scan->required_columns->front(), PlanColumnIndex{0U});
}

TEST(LoweringTest, ConsumesBoundQueryAndParameterSlotsAtTheFacade) {
    const std::array parameter_types{types::kInt64Type};
    auto bound = bind_sql("SELECT id FROM accounts WHERE id = $1", parameter_types);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 1U);
    sql::BoundStatement statement = std::move(bound->statements.front());
    auto* const select = std::get_if<sql::BoundSelectQuery>(&statement.node);
    ASSERT_NE(select, nullptr);
    auto metadata = metadata_port();

    auto lowered = lower_bound_select(
        sql::BoundQuery{statement.span, std::move(*select)},
        bound->parameter_slots,
        *metadata
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    ASSERT_EQ(lowered->parameters.parameters().size(), 1U);
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const scan = std::get_if<IndexScanPlan>(&result->input->node);
    ASSERT_NE(scan, nullptr);
    ASSERT_EQ(scan->bounds.size(), 1U);
    const auto* const parameter =
        std::get_if<PlanParameterReference>(&scan->bounds.front().value->node);
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(parameter->kind, PlanParameterKind::kExternal);
    EXPECT_EQ(parameter->id, PlanParameterId{0U});
}

TEST(LoweringTest, LowersBoundDmlIntoReusableModifyTablePlans) {
    const std::array parameter_types{types::kInt64Type, types::kFloat32Type};
    auto bound = bind_sql(
        "INSERT INTO accounts (id, score) VALUES ($1, $2); "
        "INSERT INTO accounts (id, score) SELECT account_id, amount FROM ledger; "
        "UPDATE accounts SET score = $2 WHERE id = $1; "
        "DELETE FROM accounts WHERE id = $1",
        parameter_types
    );
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 4U);
    auto metadata = metadata_port();

    auto insert_values = lower_bound_statement(
        std::move(bound->statements.at(0U)),
        bound->parameter_slots,
        *metadata
    );
    ASSERT_TRUE(insert_values) << insert_values.error().to_string();
    EXPECT_EQ(insert_values->catalog_generation(), 9U);
    ASSERT_EQ(insert_values->parameter_namespace().parameters().size(), 2U);
    const auto* const values_modify = std::get_if<ModifyTablePlan>(&insert_values->root().node);
    ASSERT_NE(values_modify, nullptr);
    EXPECT_EQ(values_modify->operation, ModifyOperation::kInsert);
    EXPECT_EQ(values_modify->target.name, "accounts");
    ASSERT_EQ(values_modify->target_columns.size(), 2U);
    EXPECT_EQ(values_modify->target_columns.at(0U), PlanColumnIndex{0U});
    EXPECT_EQ(values_modify->target_columns.at(1U), PlanColumnIndex{2U});
    const auto* const values = std::get_if<PlanValuesModificationSource>(&values_modify->source);
    ASSERT_NE(values, nullptr);
    ASSERT_EQ(values->rows.size(), 1U);
    ASSERT_EQ(values->rows.front().size(), 2U);
    const auto* const first_parameter =
        std::get_if<PlanParameterReference>(&values->rows.front().at(0U)->node);
    ASSERT_NE(first_parameter, nullptr);
    EXPECT_EQ(first_parameter->id, PlanParameterId{0U});

    auto insert_query = lower_bound_statement(
        std::move(bound->statements.at(1U)),
        bound->parameter_slots,
        *metadata
    );
    ASSERT_TRUE(insert_query) << insert_query.error().to_string();
    const auto* const query_modify = std::get_if<ModifyTablePlan>(&insert_query->root().node);
    ASSERT_NE(query_modify, nullptr);
    const auto* const query_source =
        std::get_if<PlanQueryModificationSource>(&query_modify->source);
    ASSERT_NE(query_source, nullptr);
    ASSERT_NE(query_source->input, nullptr);
    const auto* const result = std::get_if<ResultPlan>(&query_source->input->node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const ledger_scan = std::get_if<SeqScanPlan>(&result->input->node);
    ASSERT_NE(ledger_scan, nullptr);
    ASSERT_TRUE(ledger_scan->required_columns.has_value());
    ASSERT_EQ(ledger_scan->required_columns->size(), 2U);

    auto update = lower_bound_statement(
        std::move(bound->statements.at(2U)),
        bound->parameter_slots,
        *metadata
    );
    ASSERT_TRUE(update) << update.error().to_string();
    const auto* const update_modify = std::get_if<ModifyTablePlan>(&update->root().node);
    ASSERT_NE(update_modify, nullptr);
    EXPECT_EQ(update_modify->operation, ModifyOperation::kUpdate);
    ASSERT_EQ(update_modify->assignments.size(), 1U);
    EXPECT_EQ(update_modify->assignments.front().target_column, PlanColumnIndex{2U});
    const auto* const update_source =
        std::get_if<PlanQueryModificationSource>(&update_modify->source);
    ASSERT_NE(update_source, nullptr);
    ASSERT_NE(update_source->input, nullptr);
    const auto* const update_scan = std::get_if<IndexScanPlan>(&update_source->input->node);
    ASSERT_NE(update_scan, nullptr);
    EXPECT_FALSE(update_scan->required_columns.has_value());
    ASSERT_NE(update_scan->filter, nullptr);

    auto deleted = lower_bound_statement(
        std::move(bound->statements.at(3U)),
        bound->parameter_slots,
        *metadata
    );
    ASSERT_TRUE(deleted) << deleted.error().to_string();
    const auto* const delete_modify = std::get_if<ModifyTablePlan>(&deleted->root().node);
    ASSERT_NE(delete_modify, nullptr);
    EXPECT_EQ(delete_modify->operation, ModifyOperation::kDelete);
    EXPECT_TRUE(delete_modify->assignments.empty());
    const auto* const delete_source =
        std::get_if<PlanQueryModificationSource>(&delete_modify->source);
    ASSERT_NE(delete_source, nullptr);
    ASSERT_NE(delete_source->input, nullptr);
    const auto* const delete_scan = std::get_if<IndexScanPlan>(&delete_source->input->node);
    ASSERT_NE(delete_scan, nullptr);
    EXPECT_FALSE(delete_scan->required_columns.has_value());
}

TEST(LoweringTest, AssemblesSelectAndRejectsDmlMetadataGenerationDrift) {
    auto query = preprocess_sql("SELECT id FROM accounts");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();
    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    auto assembled = assemble_planned_select(std::move(lowered).value());
    ASSERT_TRUE(assembled) << assembled.error().to_string();
    EXPECT_EQ(assembled->catalog_generation(), 9U);

    auto bound = bind_sql("DELETE FROM accounts WHERE id = 7");
    ASSERT_TRUE(bound) << bound.error().to_string();
    auto changing_metadata = metadata_port(true);
    auto deleted = lower_bound_statement(
        std::move(bound->statements.front()),
        bound->parameter_slots,
        *changing_metadata
    );
    ASSERT_FALSE(deleted);
    EXPECT_EQ(deleted.error().code(), base::ErrorCode::kUnavailable);
}

TEST(LoweringTest, KeepsCompletedCorrelatedPlansIndependentFromPlanningInputs) {
    const std::array parameter_types{types::kInt64Type};
    auto planned = [&]() -> base::Result<PlannedStatement> {
        auto bound = bind_sql(
            "SELECT a.id FROM accounts AS a "
            "WHERE a.id = $1 "
            "AND EXISTS (SELECT 1 FROM ledger AS l WHERE l.account_id = a.id)",
            parameter_types
        );
        if (!bound) {
            return base::fail(std::move(bound).error());
        }
        if (bound->statements.size() != 1U) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kPlan,
                    "test expects one bound statement",
                }
            );
        }
        sql::BoundStatement statement = std::move(bound->statements.front());
        auto metadata = metadata_port();
        return lower_bound_statement(std::move(statement), bound->parameter_slots, *metadata);
    }();
    ASSERT_TRUE(planned) << planned.error().to_string();

    const std::span<const PlanParameter> parameters = planned->parameter_namespace().parameters();
    ASSERT_EQ(parameters.size(), 2U);
    EXPECT_EQ(parameters[0U].id, PlanParameterId{0U});
    EXPECT_EQ(parameters[0U].kind, PlanParameterKind::kExternal);
    ASSERT_TRUE(parameters[0U].external_slot.has_value());
    EXPECT_EQ(*parameters[0U].external_slot, ExternalParameterNumber{1U});
    EXPECT_EQ(parameters[1U].id, PlanParameterId{1U});
    EXPECT_EQ(parameters[1U].kind, PlanParameterKind::kExecutor);
    EXPECT_FALSE(parameters[1U].external_slot.has_value());

    const auto* const result = std::get_if<ResultPlan>(&planned->root().node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_EQ(evaluation->subplans.size(), 1U);
    ASSERT_EQ(evaluation->subplans.front().parameters.size(), 1U);
    EXPECT_EQ(evaluation->subplans.front().parameters.front().id, PlanParameterId{1U});

    const auto first_validation = validate_planned_statement(*planned);
    ASSERT_TRUE(first_validation) << first_validation.error().to_string();
    const auto first_explain = render_explain(*planned);
    ASSERT_TRUE(first_explain) << first_explain.error().to_string();
    const auto second_validation = validate_planned_statement(*planned);
    ASSERT_TRUE(second_validation) << second_validation.error().to_string();
    const auto second_explain = render_explain(*planned);
    ASSERT_TRUE(second_explain) << second_explain.error().to_string();
    EXPECT_EQ(first_explain.value(), second_explain.value());
}

TEST(LoweringTest, PreservesSelfJoinRangeTableIdentityInPhysicalColumns) {
    auto query = preprocess_sql(
        "SELECT a.id, b.id FROM accounts AS a INNER JOIN accounts AS b ON a.id = b.id"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const join = std::get_if<HashJoinPlan>(&result->input->node);
    ASSERT_NE(join, nullptr);
    ASSERT_EQ(result->projection.size(), 2U);
    const auto* const left =
        std::get_if<PlanInputColumnReference>(&result->projection.at(0U).expression->node);
    const auto* const right =
        std::get_if<PlanInputColumnReference>(&result->projection.at(1U).expression->node);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->column, PlanColumnIndex{0U});
    EXPECT_EQ(right->column, PlanColumnIndex{3U});
}

TEST(LoweringTest, RoutesTwoTableWhereIntoLocalIndexPath) {
    auto query = preprocess_sql(
        "SELECT a.score FROM accounts AS a INNER JOIN accounts AS b ON a.id = b.id "
        "WHERE a.enabled = TRUE"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const join = std::get_if<NestedLoopPlan>(&result->input->node);
    ASSERT_NE(join, nullptr);
    ASSERT_NE(join->join_filter, nullptr);
    EXPECT_EQ(join->join_filter->data_type, types::kBoolType);
    ASSERT_NE(join->outer, nullptr);
    const auto* const outer_scan = std::get_if<IndexScanPlan>(&join->outer->node);
    ASSERT_NE(outer_scan, nullptr);
    EXPECT_EQ(outer_scan->index.name, "accounts_enabled_id_idx");
    ASSERT_TRUE(outer_scan->required_columns.has_value());
    ASSERT_EQ(outer_scan->required_columns->size(), 3U);
    EXPECT_EQ(outer_scan->required_columns->at(0U), PlanColumnIndex{0U});
    EXPECT_EQ(outer_scan->required_columns->at(1U), PlanColumnIndex{1U});
    EXPECT_EQ(outer_scan->required_columns->at(2U), PlanColumnIndex{2U});
    ASSERT_NE(join->inner, nullptr);
    const auto* const inner_scan = std::get_if<IndexScanPlan>(&join->inner->node);
    ASSERT_NE(inner_scan, nullptr);
    EXPECT_EQ(inner_scan->index.name, "accounts_id_idx");
    ASSERT_TRUE(inner_scan->required_columns.has_value());
    ASSERT_EQ(inner_scan->required_columns->size(), 1U);
    EXPECT_EQ(inner_scan->required_columns->front(), PlanColumnIndex{0U});
}

TEST(LoweringTest, BuildsAggregateHavingAndPostAggregateProjection) {
    auto query = preprocess_sql(
        "SELECT enabled, COUNT(*) AS total FROM accounts GROUP BY enabled HAVING COUNT(*) > 0 "
        "ORDER BY enabled"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const sort = std::get_if<SortPlan>(&lowered->root.node);
    ASSERT_NE(sort, nullptr);
    ASSERT_NE(sort->input, nullptr);
    const auto* const result = std::get_if<ResultPlan>(&sort->input->node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const aggregate = std::get_if<AggregatePlan>(&result->input->node);
    ASSERT_NE(aggregate, nullptr);
    ASSERT_EQ(aggregate->aggregates.size(), 1U);
    ASSERT_NE(aggregate->having, nullptr);
    EXPECT_EQ(aggregate->metadata.output.columns.size(), 2U);
}

TEST(LoweringTest, PreservesAggregateGroupMatchRegistrationAndDeduplicationOrder) {
    auto query = preprocess_sql(
        "SELECT (id + 1) * (SUM(id) + COUNT(*)) AS summary FROM accounts "
        "GROUP BY id + 1 HAVING COUNT(*) > 0 ORDER BY SUM(id)"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const sort = std::get_if<SortPlan>(&result->input->node);
    ASSERT_NE(sort, nullptr);
    ASSERT_NE(sort->input, nullptr);
    const auto* const aggregate = std::get_if<AggregatePlan>(&sort->input->node);
    ASSERT_NE(aggregate, nullptr);
    ASSERT_EQ(aggregate->group_by.size(), 1U);
    EXPECT_NE(std::get_if<PlanBinaryExpression>(&aggregate->group_by.front()->node), nullptr);
    ASSERT_EQ(aggregate->aggregates.size(), 2U);
    EXPECT_EQ(aggregate->aggregates.at(0U).kind, PlanAggregateKind::kSum);
    EXPECT_EQ(aggregate->aggregates.at(1U).kind, PlanAggregateKind::kCount);
    ASSERT_EQ(aggregate->aggregates.front().arguments.size(), 1U);
    const auto* const argument = std::get_if<PlanInputColumnReference>(
        &aggregate->aggregates.front().arguments.front()->node
    );
    ASSERT_NE(argument, nullptr);
    EXPECT_EQ(argument->column, PlanColumnIndex{0U});

    ASSERT_EQ(result->projection.size(), 1U);
    const auto* const product =
        std::get_if<PlanBinaryExpression>(&result->projection.front().expression->node);
    ASSERT_NE(product, nullptr);
    ASSERT_NE(product->left, nullptr);
    ASSERT_NE(product->right, nullptr);
    const auto* const group = std::get_if<PlanInputColumnReference>(&product->left->node);
    const auto* const sum = std::get_if<PlanBinaryExpression>(&product->right->node);
    ASSERT_NE(group, nullptr);
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(group->column, PlanColumnIndex{0U});
    ASSERT_NE(sum->left, nullptr);
    ASSERT_NE(sum->right, nullptr);
    const auto* const sum_reference = std::get_if<PlanInputColumnReference>(&sum->left->node);
    const auto* const count_reference = std::get_if<PlanInputColumnReference>(&sum->right->node);
    ASSERT_NE(sum_reference, nullptr);
    ASSERT_NE(count_reference, nullptr);
    EXPECT_EQ(sum_reference->column, PlanColumnIndex{1U});
    EXPECT_EQ(count_reference->column, PlanColumnIndex{2U});
    ASSERT_NE(aggregate->having, nullptr);
    const auto* const having = std::get_if<PlanBinaryExpression>(&aggregate->having->node);
    ASSERT_NE(having, nullptr);
    ASSERT_NE(having->left, nullptr);
    const auto* const count = std::get_if<PlanInputColumnReference>(&having->left->node);
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->column, PlanColumnIndex{2U});
    ASSERT_EQ(sort->order_by.size(), 1U);
    const auto* const order =
        std::get_if<PlanInputColumnReference>(&sort->order_by.front().expression->node);
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->column, PlanColumnIndex{1U});
}

TEST(LoweringTest, KeepsCountStarScanDemandExactlyEmpty) {
    auto query = preprocess_sql("SELECT COUNT(*) FROM accounts");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const aggregate = std::get_if<AggregatePlan>(&result->input->node);
    ASSERT_NE(aggregate, nullptr);
    ASSERT_NE(aggregate->input, nullptr);
    const auto* const scan = std::get_if<SeqScanPlan>(&aggregate->input->node);
    ASSERT_NE(scan, nullptr);
    ASSERT_TRUE(scan->required_columns.has_value());
    EXPECT_TRUE(scan->required_columns->empty());
}

TEST(LoweringTest, UsesTopNForUnorderedSortWithLimitAndOffset) {
    auto query = preprocess_sql("SELECT score FROM accounts ORDER BY score DESC LIMIT 5 OFFSET 2");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const top_n = std::get_if<TopNPlan>(&lowered->root.node);
    ASSERT_NE(top_n, nullptr);
    ASSERT_NE(top_n->limit, nullptr);
    ASSERT_NE(top_n->offset, nullptr);
    ASSERT_NE(top_n->input, nullptr);
    EXPECT_NE(std::get_if<ResultPlan>(&top_n->input->node), nullptr);
}

TEST(LoweringTest, KeepsSortForDefaultAscendingNullOrderOnAnIndex) {
    auto query = preprocess_sql("SELECT id FROM accounts WHERE id > 0 ORDER BY id");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const sort = std::get_if<SortPlan>(&lowered->root.node);
    ASSERT_NE(sort, nullptr);
    ASSERT_NE(sort->input, nullptr);
    const auto* const result = std::get_if<ResultPlan>(&sort->input->node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    EXPECT_NE(std::get_if<IndexScanPlan>(&result->input->node), nullptr);
}

TEST(LoweringTest, UsesUniqueAndProjectionOutputReferencesForDistinctOrdering) {
    auto query = preprocess_sql("SELECT DISTINCT enabled FROM accounts ORDER BY enabled");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const sort = std::get_if<SortPlan>(&lowered->root.node);
    ASSERT_NE(sort, nullptr);
    ASSERT_NE(sort->input, nullptr);
    EXPECT_NE(std::get_if<UniquePlan>(&sort->input->node), nullptr);
    ASSERT_EQ(sort->order_by.size(), 1U);
    const auto* const order_reference =
        std::get_if<PlanInputColumnReference>(&sort->order_by.front().expression->node);
    ASSERT_NE(order_reference, nullptr);
    EXPECT_EQ(order_reference->column, PlanColumnIndex{0U});
}

TEST(LoweringTest, SortsDistinctOutputEvenWhenTheInputIndexIsOrdered) {
    auto query = preprocess_sql("SELECT DISTINCT id FROM accounts WHERE id > 0 ORDER BY id");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const sort = std::get_if<SortPlan>(&lowered->root.node);
    ASSERT_NE(sort, nullptr);
    ASSERT_NE(sort->input, nullptr);
    const auto* const unique = std::get_if<UniquePlan>(&sort->input->node);
    ASSERT_NE(unique, nullptr);
    ASSERT_NE(unique->input, nullptr);
    const auto* const result = std::get_if<ResultPlan>(&unique->input->node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    EXPECT_NE(std::get_if<IndexScanPlan>(&result->input->node), nullptr);
}

TEST(LoweringTest, AppliesOffsetAboveSortWhenNoLimitIsPresent) {
    auto query = preprocess_sql("SELECT score FROM accounts ORDER BY score DESC OFFSET 2");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const limit = std::get_if<LimitPlan>(&lowered->root.node);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->limit, nullptr);
    ASSERT_NE(limit->offset, nullptr);
    ASSERT_NE(limit->input, nullptr);
    EXPECT_NE(std::get_if<SortPlan>(&limit->input->node), nullptr);
}

TEST(LoweringTest, RecursivelyBuildsDerivedSubqueryAndRenamesOutput) {
    auto query = preprocess_sql("SELECT d.key FROM (SELECT id AS key FROM accounts) AS d");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const scan = std::get_if<SubqueryScanPlan>(&result->input->node);
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->alias, "d");
    ASSERT_EQ(scan->metadata.output.columns.size(), 1U);
    EXPECT_EQ(scan->metadata.output.columns.front().name, "key");
}

TEST(LoweringTest, PreservesOuterFilterOnDerivedTable) {
    auto query =
        preprocess_sql("SELECT d.key FROM (SELECT id AS key FROM accounts) AS d WHERE d.key = 7");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const scan = std::get_if<SubqueryScanPlan>(&result->input->node);
    ASSERT_NE(scan, nullptr);
    ASSERT_NE(scan->filter, nullptr);
    const auto* const comparison = std::get_if<PlanBinaryExpression>(&scan->filter->node);
    ASSERT_NE(comparison, nullptr);
    EXPECT_EQ(comparison->op, types::BinaryOperator::kEqual);
}

TEST(LoweringTest, SortsAggregateOutputBeforeDroppingUnprojectedOrderExpression) {
    auto query =
        preprocess_sql("SELECT enabled FROM accounts GROUP BY enabled ORDER BY COUNT(*) DESC");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const sort = std::get_if<SortPlan>(&result->input->node);
    ASSERT_NE(sort, nullptr);
    ASSERT_NE(sort->input, nullptr);
    const auto* const aggregate = std::get_if<AggregatePlan>(&sort->input->node);
    ASSERT_NE(aggregate, nullptr);
    ASSERT_EQ(aggregate->aggregates.size(), 1U);
    ASSERT_EQ(sort->order_by.size(), 1U);
    const auto* const order =
        std::get_if<PlanInputColumnReference>(&sort->order_by.front().expression->node);
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->column, PlanColumnIndex{1U});
}

TEST(LoweringTest, LowersThreeWayEqualityJoinThroughHashPlans) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a "
        "INNER JOIN accounts AS b ON a.id = b.id "
        "INNER JOIN accounts AS c ON b.id = c.id"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const final_join = std::get_if<HashJoinPlan>(&result->input->node);
    ASSERT_NE(final_join, nullptr);
    ASSERT_EQ(final_join->join_columns.size(), 1U);
    ASSERT_NE(final_join->outer, nullptr);
    const auto* const first_join = std::get_if<HashJoinPlan>(&final_join->outer->node);
    ASSERT_NE(first_join, nullptr);
    EXPECT_EQ(first_join->join_columns.size(), 1U);
}

TEST(LoweringTest, PropagatesOneConstantAcrossAThreeWayEqualityJoin) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a "
        "INNER JOIN accounts AS b ON a.id = b.id "
        "INNER JOIN accounts AS c ON b.id = c.id WHERE a.id = 7"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    EXPECT_TRUE(lowered->parameters.parameters().empty());
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const final_join = std::get_if<HashJoinPlan>(&result->input->node);
    ASSERT_NE(final_join, nullptr);
    ASSERT_NE(final_join->outer, nullptr);
    const auto* const merge = std::get_if<MergeJoinPlan>(&final_join->outer->node);
    ASSERT_NE(merge, nullptr);
    ASSERT_NE(merge->inner, nullptr);
    const auto* const scan = std::get_if<IndexScanPlan>(&merge->inner->node);
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->index.name, "accounts_id_idx");
    ASSERT_EQ(scan->bounds.size(), 1U);
    const auto* const literal =
        std::get_if<PlanLiteralExpression>(&scan->bounds.front().value->node);
    ASSERT_NE(literal, nullptr);
    ASSERT_NE(literal->value.as_integer(), nullptr);
    EXPECT_EQ(*literal->value.as_integer(), 7);
}

TEST(LoweringTest, PropagatesASelectiveOuterConstantIntoTheInnerIndex) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a, ledger AS l "
        "WHERE a.id = 7 AND l.account_id = a.id"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    EXPECT_TRUE(lowered->parameters.parameters().empty());

    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const join = std::get_if<MergeJoinPlan>(&result->input->node);
    ASSERT_NE(join, nullptr);

    ASSERT_NE(join->outer, nullptr);
    const auto* const outer_scan = std::get_if<IndexScanPlan>(&join->outer->node);
    ASSERT_NE(outer_scan, nullptr);
    EXPECT_EQ(outer_scan->index.name, "accounts_id_idx");

    ASSERT_NE(join->inner, nullptr);
    const auto* const inner_scan = std::get_if<IndexScanPlan>(&join->inner->node);
    ASSERT_NE(inner_scan, nullptr);
    EXPECT_EQ(inner_scan->index.name, "ledger_account_idx");
    ASSERT_EQ(inner_scan->bounds.size(), 1U);
    const auto* const literal =
        std::get_if<PlanLiteralExpression>(&inner_scan->bounds.front().value->node);
    ASSERT_NE(literal, nullptr);
    ASSERT_NE(literal->value.as_integer(), nullptr);
    EXPECT_EQ(*literal->value.as_integer(), 7);
}

TEST(LoweringTest, PropagatesJoinConstantsIntoCompositeInnerIndex) {
    auto query = preprocess_sql(
        "SELECT COUNT(*) FROM history AS h, customer AS c "
        "WHERE h.h_c_w_id = 1 AND h.h_c_d_id = 2 AND h.h_c_id = 3 "
        "AND c.c_w_id = h.h_c_w_id AND c.c_d_id = h.h_c_d_id "
        "AND c.c_id = h.h_c_id"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    EXPECT_TRUE(lowered->parameters.parameters().empty());

    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const aggregate = std::get_if<AggregatePlan>(&result->input->node);
    ASSERT_NE(aggregate, nullptr);
    ASSERT_NE(aggregate->input, nullptr);
    const auto* const join = std::get_if<HashJoinPlan>(&aggregate->input->node);
    ASSERT_NE(join, nullptr);
    ASSERT_NE(join->outer, nullptr);
    EXPECT_NE(std::get_if<SeqScanPlan>(&join->outer->node), nullptr);
    ASSERT_NE(join->inner, nullptr);
    const auto* const inner_scan = std::get_if<IndexScanPlan>(&join->inner->node);
    ASSERT_NE(inner_scan, nullptr);
    EXPECT_EQ(inner_scan->index.name, "customer_warehouse_district_id_idx");
    ASSERT_EQ(inner_scan->bounds.size(), 3U);
    const std::array expected_values{1LL, 2LL, 3LL};
    for (std::size_t index = 0U; index < inner_scan->bounds.size(); ++index) {
        EXPECT_EQ(
            inner_scan->bounds[index].key_index,
            PlanIndexKeyIndex{static_cast<std::uint32_t>(index)}
        );
        const auto* const literal =
            std::get_if<PlanLiteralExpression>(&inner_scan->bounds[index].value->node);
        ASSERT_NE(literal, nullptr);
        ASSERT_NE(literal->value.as_integer(), nullptr);
        EXPECT_EQ(*literal->value.as_integer(), expected_values.at(index));
    }
}

TEST(LoweringTest, PropagatesExternalJoinParametersIntoCompositeInnerIndex) {
    const std::array parameter_types{
        types::kInt64Type,
        types::kInt64Type,
        types::kInt64Type,
    };
    auto bound = bind_sql(
        "SELECT COUNT(*) FROM history AS h, customer AS c "
        "WHERE h.h_c_w_id = $1 AND h.h_c_d_id = $2 AND h.h_c_id = $3 "
        "AND c.c_w_id = h.h_c_w_id AND c.c_d_id = h.h_c_d_id "
        "AND c.c_id = h.h_c_id",
        parameter_types
    );
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 1U);
    sql::BoundStatement statement = std::move(bound->statements.front());
    auto* const select = std::get_if<sql::BoundSelectQuery>(&statement.node);
    ASSERT_NE(select, nullptr);
    auto metadata = metadata_port();

    auto lowered = lower_bound_select(
        sql::BoundQuery{statement.span, std::move(*select)},
        bound->parameter_slots,
        *metadata
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    ASSERT_EQ(lowered->parameters.parameters().size(), 3U);

    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const aggregate = std::get_if<AggregatePlan>(&result->input->node);
    ASSERT_NE(aggregate, nullptr);
    ASSERT_NE(aggregate->input, nullptr);
    const auto* const join = std::get_if<HashJoinPlan>(&aggregate->input->node);
    ASSERT_NE(join, nullptr);
    ASSERT_NE(join->inner, nullptr);
    const auto* const inner_scan = std::get_if<IndexScanPlan>(&join->inner->node);
    ASSERT_NE(inner_scan, nullptr);
    EXPECT_EQ(inner_scan->index.name, "customer_warehouse_district_id_idx");
    ASSERT_EQ(inner_scan->bounds.size(), 3U);
    for (std::size_t index = 0U; index < inner_scan->bounds.size(); ++index) {
        const auto* const parameter =
            std::get_if<PlanParameterReference>(&inner_scan->bounds[index].value->node);
        ASSERT_NE(parameter, nullptr);
        EXPECT_EQ(parameter->kind, PlanParameterKind::kExternal);
        EXPECT_EQ(parameter->id, PlanParameterId{static_cast<std::uint32_t>(index)});
    }
}

TEST(LoweringTest, LowersStaticPrefixAndExecutorSuffixAsOneCompositeIndexPath) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a, ledger AS l "
        "WHERE a.enabled = TRUE AND l.amount = 9 AND l.account_id = a.id"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    ASSERT_EQ(lowered->parameters.parameters().size(), 1U);

    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const join = std::get_if<NestedLoopPlan>(&result->input->node);
    ASSERT_NE(join, nullptr);
    ASSERT_EQ(join->parameters.size(), 1U);
    ASSERT_NE(join->inner, nullptr);
    const auto* const inner_scan = std::get_if<IndexScanPlan>(&join->inner->node);
    ASSERT_NE(inner_scan, nullptr);
    EXPECT_EQ(inner_scan->index.name, "ledger_amount_account_idx");
    ASSERT_EQ(inner_scan->bounds.size(), 2U);
    const auto* const static_prefix =
        std::get_if<PlanLiteralExpression>(&inner_scan->bounds[0U].value->node);
    ASSERT_NE(static_prefix, nullptr);
    ASSERT_NE(static_prefix->value.as_integer(), nullptr);
    EXPECT_EQ(*static_prefix->value.as_integer(), 9);
    const auto* const runtime_suffix =
        std::get_if<PlanParameterReference>(&inner_scan->bounds[1U].value->node);
    ASSERT_NE(runtime_suffix, nullptr);
    EXPECT_EQ(runtime_suffix->kind, PlanParameterKind::kExecutor);
    EXPECT_EQ(runtime_suffix->id, join->parameters.front().parameter.id);
}

TEST(LoweringTest, LowersFirstBinaryJoinThroughMergePlan) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a "
        "INNER JOIN accounts AS b ON a.id = b.id "
        "INNER JOIN accounts AS c ON b.id = c.id WHERE b.enabled = TRUE"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const final_join = std::get_if<HashJoinPlan>(&result->input->node);
    ASSERT_NE(final_join, nullptr);
    ASSERT_NE(final_join->outer, nullptr);
    const auto* const merge = std::get_if<MergeJoinPlan>(&final_join->outer->node);
    ASSERT_NE(merge, nullptr);
    ASSERT_EQ(merge->join_columns.size(), 1U);
    ASSERT_NE(merge->outer, nullptr);
    ASSERT_NE(merge->inner, nullptr);
    const auto* const outer_scan = std::get_if<IndexScanPlan>(&merge->outer->node);
    const auto* const inner_scan = std::get_if<IndexScanPlan>(&merge->inner->node);
    ASSERT_NE(outer_scan, nullptr);
    ASSERT_NE(inner_scan, nullptr);
    EXPECT_EQ(outer_scan->index.name, "accounts_id_idx");
    EXPECT_EQ(inner_scan->index.name, "accounts_enabled_id_idx");
}

TEST(LoweringTest, KeepsFloatEqualityOnNestedLoopStrategyPath) {
    auto query = preprocess_sql(
        "SELECT a.score FROM accounts AS a "
        "INNER JOIN accounts AS b ON a.score = b.score "
        "INNER JOIN accounts AS c ON b.score = c.score"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const final_join = std::get_if<NestedLoopPlan>(&result->input->node);
    ASSERT_NE(final_join, nullptr);
    ASSERT_NE(final_join->outer, nullptr);
    EXPECT_NE(std::get_if<NestedLoopPlan>(&final_join->outer->node), nullptr);
}

TEST(LoweringTest, KeepsConstantJoinDomainFilterAsDeferredPlanFilter) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a "
        "INNER JOIN accounts AS b ON a.id = b.id "
        "INNER JOIN accounts AS c ON b.id = c.id WHERE 1 = 1"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_NE(evaluation->filter, nullptr);
    ASSERT_NE(evaluation->input, nullptr);
    EXPECT_NE(std::get_if<HashJoinPlan>(&evaluation->input->node), nullptr);
}

TEST(LoweringTest, RendersStrategyLoweredPhysicalJoinTree) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a "
        "INNER JOIN accounts AS b ON a.id = b.id "
        "INNER JOIN accounts AS c ON b.id = c.id"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    auto explain = render_explain(lowered->root);
    ASSERT_TRUE(explain) << explain.error().to_string();
    EXPECT_NE(explain->find("Hash Join"), std::string::npos);
    EXPECT_NE(explain->find("Hash Cond: (id = id)"), std::string::npos);
}

TEST(LoweringTest, LowersUncorrelatedInSubqueryIntoDeferredSubplan) {
    auto query =
        preprocess_sql("SELECT id FROM accounts WHERE id IN (SELECT account_id FROM ledger)");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_EQ(evaluation->subplans.size(), 1U);
    ASSERT_NE(evaluation->filter, nullptr);
    const auto* const reference = std::get_if<PlanSubPlanReference>(&evaluation->filter->node);
    ASSERT_NE(reference, nullptr);
    EXPECT_EQ(reference->index, PlanSubPlanIndex{0U});
    const PlanSubPlan& subplan = evaluation->subplans.front();
    EXPECT_EQ(subplan.strategy, PlanSubPlanStrategy::kIn);
    ASSERT_NE(subplan.input_expression, nullptr);
    EXPECT_TRUE(subplan.arguments.empty());
    EXPECT_TRUE(subplan.parameters.empty());
    EXPECT_FALSE(subplan.uses_inherited_parameters);
    ASSERT_NE(subplan.plan, nullptr);
    EXPECT_EQ(plan_operator_kind(*subplan.plan), PlanOperatorKind::kResult);
}

TEST(LoweringTest, RewritesCorrelatedSubplanColumnsIntoStatementExecutorParameters) {
    auto query = preprocess_sql(
        "SELECT a.score FROM accounts AS a "
        "WHERE EXISTS (SELECT 1 FROM ledger AS l WHERE l.account_id = a.id)"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    ASSERT_EQ(lowered->parameters.parameters().size(), 1U);
    const PlanParameter& executor = lowered->parameters.parameters().front();
    EXPECT_EQ(executor.kind, PlanParameterKind::kExecutor);
    EXPECT_EQ(executor.data_type, types::kInt64Type);

    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_EQ(evaluation->subplans.size(), 1U);
    ASSERT_NE(evaluation->input, nullptr);
    const auto* const outer_scan = std::get_if<SeqScanPlan>(&evaluation->input->node);
    ASSERT_NE(outer_scan, nullptr);
    ASSERT_TRUE(outer_scan->required_columns.has_value());
    ASSERT_EQ(outer_scan->required_columns->size(), 2U);
    EXPECT_EQ(outer_scan->required_columns->at(0U), PlanColumnIndex{0U});
    EXPECT_EQ(outer_scan->required_columns->at(1U), PlanColumnIndex{2U});
    const PlanSubPlan& subplan = evaluation->subplans.front();
    EXPECT_EQ(subplan.strategy, PlanSubPlanStrategy::kExists);
    ASSERT_EQ(subplan.arguments.size(), 1U);
    ASSERT_EQ(subplan.parameters.size(), 1U);
    EXPECT_EQ(subplan.parameters.front().id, executor.id);
    EXPECT_EQ(subplan.parameters.front().kind, PlanParameterKind::kExecutor);
    const auto* const argument =
        std::get_if<PlanInputColumnReference>(&subplan.arguments.front()->node);
    ASSERT_NE(argument, nullptr);
    EXPECT_EQ(argument->column, PlanColumnIndex{0U});
    ASSERT_NE(subplan.plan, nullptr);
    const auto* const child_result = std::get_if<ResultPlan>(&subplan.plan->node);
    ASSERT_NE(child_result, nullptr);
    ASSERT_NE(child_result->input, nullptr);
    const auto* const child_scan = std::get_if<IndexScanPlan>(&child_result->input->node);
    ASSERT_NE(child_scan, nullptr);
    EXPECT_EQ(child_scan->index.name, "ledger_account_idx");
    ASSERT_TRUE(child_scan->required_columns.has_value());
    ASSERT_EQ(child_scan->required_columns->size(), 1U);
    EXPECT_EQ(child_scan->required_columns->front(), PlanColumnIndex{0U});
    ASSERT_EQ(child_scan->bounds.size(), 1U);
    const auto* const index_parameter =
        std::get_if<PlanParameterReference>(&child_scan->bounds.front().value->node);
    ASSERT_NE(index_parameter, nullptr);
    EXPECT_EQ(*index_parameter, subplan.parameters.front());
    ASSERT_NE(child_scan->filter, nullptr);
    const auto* const comparison = std::get_if<PlanBinaryExpression>(&child_scan->filter->node);
    ASSERT_NE(comparison, nullptr);
    ASSERT_NE(comparison->right, nullptr);
    const auto* const child_parameter =
        std::get_if<PlanParameterReference>(&comparison->right->node);
    ASSERT_NE(child_parameter, nullptr);
    EXPECT_EQ(*child_parameter, subplan.parameters.front());
    EXPECT_FALSE(subplan.uses_inherited_parameters);
}

TEST(LoweringTest, PreservesAncestorExecutorParametersAcrossNestedSubplans) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a WHERE EXISTS ("
        "SELECT 1 FROM ledger AS l WHERE EXISTS ("
        "SELECT 1 FROM accounts AS i WHERE i.id = a.id AND i.id = l.account_id))"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    ASSERT_EQ(lowered->parameters.parameters().size(), 2U);
    EXPECT_EQ(lowered->parameters.parameters()[0U].kind, PlanParameterKind::kExecutor);
    EXPECT_EQ(lowered->parameters.parameters()[1U].kind, PlanParameterKind::kExecutor);

    const auto* const outer_result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(outer_result, nullptr);
    ASSERT_NE(outer_result->input, nullptr);
    const auto* const outer_evaluation = std::get_if<SubPlanEvalPlan>(&outer_result->input->node);
    ASSERT_NE(outer_evaluation, nullptr);
    ASSERT_EQ(outer_evaluation->subplans.size(), 1U);
    const PlanSubPlan& outer_subplan = outer_evaluation->subplans.front();
    ASSERT_EQ(outer_subplan.parameters.size(), 1U);
    EXPECT_EQ(outer_subplan.parameters.front().id, PlanParameterId{0U});
    EXPECT_FALSE(outer_subplan.uses_inherited_parameters);

    ASSERT_NE(outer_subplan.plan, nullptr);
    const auto* const middle_result = std::get_if<ResultPlan>(&outer_subplan.plan->node);
    ASSERT_NE(middle_result, nullptr);
    ASSERT_NE(middle_result->input, nullptr);
    const auto* const middle_evaluation = std::get_if<SubPlanEvalPlan>(&middle_result->input->node);
    ASSERT_NE(middle_evaluation, nullptr);
    ASSERT_EQ(middle_evaluation->subplans.size(), 1U);
    const PlanSubPlan& nested_subplan = middle_evaluation->subplans.front();
    ASSERT_EQ(nested_subplan.parameters.size(), 1U);
    EXPECT_EQ(nested_subplan.parameters.front().id, PlanParameterId{1U});
    EXPECT_TRUE(nested_subplan.uses_inherited_parameters);
    ASSERT_NE(nested_subplan.plan, nullptr);
    EXPECT_EQ(plan_operator_kind(*nested_subplan.plan), PlanOperatorKind::kResult);
}

TEST(LoweringTest, RewritesOuterReferencesInsideNonLateralDerivedSubqueryInputs) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a WHERE EXISTS ("
        "SELECT q.outer_id FROM (SELECT a.id AS outer_id) AS q)"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    ASSERT_EQ(lowered->parameters.parameters().size(), 1U);
    EXPECT_EQ(lowered->parameters.parameters().front().kind, PlanParameterKind::kExecutor);

    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_EQ(evaluation->subplans.size(), 1U);
    const PlanSubPlan& subplan = evaluation->subplans.front();
    ASSERT_EQ(subplan.parameters.size(), 1U);
    EXPECT_EQ(subplan.parameters.front().id, PlanParameterId{0U});
    ASSERT_NE(subplan.plan, nullptr);
    const auto* const child_result = std::get_if<ResultPlan>(&subplan.plan->node);
    ASSERT_NE(child_result, nullptr);
    ASSERT_NE(child_result->input, nullptr);
    const auto* const derived_scan = std::get_if<SubqueryScanPlan>(&child_result->input->node);
    ASSERT_NE(derived_scan, nullptr);
    ASSERT_NE(derived_scan->subquery, nullptr);
    const auto* const derived_result = std::get_if<ResultPlan>(&derived_scan->subquery->node);
    ASSERT_NE(derived_result, nullptr);
    ASSERT_EQ(derived_result->projection.size(), 1U);
    const auto* const parameter =
        std::get_if<PlanParameterReference>(&derived_result->projection.front().expression->node);
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(*parameter, subplan.parameters.front());
}

TEST(LoweringTest, LowersJoinQualificationSubplansAtTheJoinBoundary) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a INNER JOIN ledger AS l "
        "ON EXISTS (SELECT 1 FROM accounts AS i WHERE i.id = a.id)"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_NE(evaluation->input, nullptr);
    EXPECT_EQ(plan_operator_kind(*evaluation->input), PlanOperatorKind::kNestedLoop);
    ASSERT_NE(evaluation->filter, nullptr);
    EXPECT_NE(std::get_if<PlanSubPlanReference>(&evaluation->filter->node), nullptr);
    ASSERT_EQ(evaluation->subplans.size(), 1U);
    ASSERT_EQ(evaluation->subplans.front().parameters.size(), 1U);
    EXPECT_EQ(evaluation->subplans.front().parameters.front().kind, PlanParameterKind::kExecutor);
}

TEST(LoweringTest, FallsBackFromJoinStrategyForSubqueryQualification) {
    auto query = preprocess_sql(
        "SELECT a.id FROM accounts AS a "
        "INNER JOIN accounts AS b ON a.id = b.id "
        "INNER JOIN ledger AS l "
        "ON EXISTS (SELECT 1 FROM accounts AS i WHERE i.id = b.id)"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_NE(evaluation->input, nullptr);
    EXPECT_EQ(plan_operator_kind(*evaluation->input), PlanOperatorKind::kNestedLoop);
    ASSERT_EQ(evaluation->subplans.size(), 1U);
    EXPECT_EQ(evaluation->subplans.front().strategy, PlanSubPlanStrategy::kExists);
}

TEST(LoweringTest, LowersNegatedSubqueryStrategies) {
    auto query = preprocess_sql(
        "SELECT a.id, "
        "NOT EXISTS (SELECT 1 FROM ledger AS e "
        "            WHERE e.account_id = a.id) AS missing "
        "FROM accounts AS a "
        "WHERE a.id NOT IN (SELECT l.account_id FROM ledger AS l)"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const projection_evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(projection_evaluation, nullptr);
    ASSERT_EQ(projection_evaluation->subplans.size(), 1U);
    EXPECT_EQ(projection_evaluation->subplans.front().strategy, PlanSubPlanStrategy::kNotExists);
    ASSERT_NE(projection_evaluation->input, nullptr);
    const auto* const filter_evaluation =
        std::get_if<SubPlanEvalPlan>(&projection_evaluation->input->node);
    ASSERT_NE(filter_evaluation, nullptr);
    ASSERT_EQ(filter_evaluation->subplans.size(), 1U);
    EXPECT_EQ(filter_evaluation->subplans.front().strategy, PlanSubPlanStrategy::kNotIn);
}

TEST(LoweringTest, LowersProjectionScalarAndExistsSubplansAtTheirResultBoundary) {
    auto query = preprocess_sql(
        "SELECT a.id, (SELECT l.amount FROM ledger AS l WHERE l.account_id = a.id) AS amount, "
        "EXISTS (SELECT 1 FROM ledger AS e WHERE e.account_id = a.id) AS present "
        "FROM accounts AS a"
    );
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_EQ(evaluation->subplans.size(), 2U);
    ASSERT_EQ(result->projection.size(), 3U);
    const auto* const scalar_reference =
        std::get_if<PlanSubPlanReference>(&result->projection.at(1U).expression->node);
    const auto* const exists_reference =
        std::get_if<PlanSubPlanReference>(&result->projection.at(2U).expression->node);
    ASSERT_NE(scalar_reference, nullptr);
    ASSERT_NE(exists_reference, nullptr);
    EXPECT_EQ(scalar_reference->index, PlanSubPlanIndex{0U});
    EXPECT_EQ(exists_reference->index, PlanSubPlanIndex{1U});
    EXPECT_EQ(evaluation->subplans.at(0U).strategy, PlanSubPlanStrategy::kScalar);
    EXPECT_EQ(evaluation->subplans.at(1U).strategy, PlanSubPlanStrategy::kExists);
}

TEST(LoweringTest, AllocatesNestedSubplansInDepthFirstExpressionOrder) {
    auto query =
        preprocess_sql("SELECT ((SELECT 1) + (SELECT 2)) IN (SELECT id FROM accounts) AS matched");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port();

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_TRUE(lowered) << lowered.error().to_string();
    const auto* const result = std::get_if<ResultPlan>(&lowered->root.node);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->input, nullptr);
    const auto* const evaluation = std::get_if<SubPlanEvalPlan>(&result->input->node);
    ASSERT_NE(evaluation, nullptr);
    ASSERT_EQ(evaluation->subplans.size(), 3U);
    EXPECT_EQ(evaluation->subplans.at(0U).strategy, PlanSubPlanStrategy::kScalar);
    EXPECT_EQ(evaluation->subplans.at(1U).strategy, PlanSubPlanStrategy::kScalar);
    const PlanSubPlan& in_subplan = evaluation->subplans.at(2U);
    EXPECT_EQ(in_subplan.strategy, PlanSubPlanStrategy::kIn);
    ASSERT_NE(in_subplan.input_expression, nullptr);
    const auto* const input = std::get_if<PlanBinaryExpression>(&in_subplan.input_expression->node);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->left, nullptr);
    ASSERT_NE(input->right, nullptr);
    const auto* const left = std::get_if<PlanSubPlanReference>(&input->left->node);
    const auto* const right = std::get_if<PlanSubPlanReference>(&input->right->node);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->index, PlanSubPlanIndex{0U});
    EXPECT_EQ(right->index, PlanSubPlanIndex{1U});
    const auto* const result_reference =
        std::get_if<PlanSubPlanReference>(&result->projection.front().expression->node);
    ASSERT_NE(result_reference, nullptr);
    EXPECT_EQ(result_reference->index, PlanSubPlanIndex{2U});
}

TEST(LoweringTest, RejectsDmlExpressionSubqueriesAtTheStatementBoundary) {
    auto bound = bind_sql(
        "INSERT INTO accounts (id) VALUES ((SELECT account_id FROM ledger)); "
        "UPDATE accounts SET id = (SELECT account_id FROM ledger); "
        "DELETE FROM accounts WHERE EXISTS (SELECT 1 FROM ledger)"
    );
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 3U);
    auto metadata = metadata_port();

    for (sql::BoundStatement& statement : bound->statements) {
        auto lowered =
            lower_bound_statement(std::move(statement), bound->parameter_slots, *metadata);
        ASSERT_FALSE(lowered);
        EXPECT_EQ(lowered.error().code(), base::ErrorCode::kUnsupported);
    }
}

TEST(LoweringTest, RejectsMetadataGenerationDrift) {
    auto query = preprocess_sql("SELECT id FROM accounts");
    ASSERT_TRUE(query) << query.error().to_string();
    auto parameters = empty_parameters();
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    auto metadata = metadata_port(true);

    auto lowered = lower_preprocessed_select(
        std::move(query).value(),
        *metadata,
        std::move(parameters).value()
    );
    ASSERT_FALSE(lowered);
    EXPECT_EQ(lowered.error().code(), base::ErrorCode::kUnavailable);
}

}  // namespace
}  // namespace db::planner
