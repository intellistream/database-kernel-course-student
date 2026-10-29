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
#include "db/planner/explain.hpp"
#include "db/planner/plan.hpp"
#include "db/types/coercion.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {
namespace {

auto account_schema() -> PlanSchema {
    return PlanSchema{
        .columns = {
            {.name = "id",
             .data_type = types::kInt64Type,
             .origin =
                 PlanColumnOrigin{
                     .relation_oid = base::Oid{42U},
                     .relation_column = PlanColumnIndex{0U},
                 }},
            {.name = "balance",
             .data_type = types::kFloat32Type,
             .origin = PlanColumnOrigin{
                 .relation_oid = base::Oid{42U},
                 .relation_column = PlanColumnIndex{1U},
             }},
        },
    };
}

auto metadata(const PlanSchema& schema) -> PlanOperatorMetadata {
    return PlanOperatorMetadata{
        .output = schema,
    };
}

auto account_relation() -> PlanRelation {
    return PlanRelation{
        .oid = base::Oid{42U},
        .name = "account",
        .alias = std::nullopt,
        .schema = account_schema(),
    };
}

auto integer_literal(const std::int64_t value) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        types::kInt64Type,
        PlanLiteralExpression{.value = types::Value::integer(value)}
    );
}

auto floating_literal(const float value) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        types::kFloat32Type,
        PlanLiteralExpression{.value = types::Value::floating(value)}
    );
}

auto input_column(const PlanColumnIndex column, const types::DataType type) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        type,
        PlanInputColumnReference{.column = column}
    );
}

auto subplan_reference(const PlanSubPlanIndex index, const types::DataType type)
    -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        type,
        PlanSubPlanReference{.index = index}
    );
}

auto equal_id_to(const std::int64_t value) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        types::kBoolType,
        PlanBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = input_column(PlanColumnIndex{0U}, types::kInt64Type),
            .right = integer_literal(value),
        }
    );
}

auto make_seq_scan(PlanExpressionPtr filter = nullptr) -> Plan {
    const PlanSchema schema = account_schema();
    return Plan{SeqScanPlan{
        .metadata = metadata(schema),
        .relation = account_relation(),
        .required_columns = std::vector{PlanColumnIndex{0U}, PlanColumnIndex{1U}},
        .filter = std::move(filter),
    }};
}

auto make_index_scan(PlanExpressionPtr filter = nullptr) -> Plan {
    const PlanSchema schema = account_schema();
    std::vector<PlanIndexBound> bounds;
    bounds.push_back(
        PlanIndexBound{
            .key_index = PlanIndexKeyIndex{0U},
            .comparison = types::BinaryOperator::kEqual,
            .value = integer_literal(7),
        }
    );
    return Plan{IndexScanPlan{
        .metadata = metadata(schema),
        .relation = account_relation(),
        .index =
            PlanIndex{
                .oid = base::Oid{73U},
                .name = "account_id_idx",
                .keys =
                    {{.relation_column = PlanColumnIndex{0U},
                      .column_name = "id",
                      .data_type = types::kInt64Type}},
                .unique = true,
            },
        .bounds = std::move(bounds),
        .required_columns = std::vector{PlanColumnIndex{0U}, PlanColumnIndex{1U}},
        .filter = std::move(filter),
    }};
}

TEST(ParameterNamespaceTest, PreservesDenseSqlSlotsThenAllocatesExecutorSlots) {
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
    auto parameters = ParameterNamespace::from_external_slots(external);
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    EXPECT_EQ(parameters->external_parameter_count(), 2U);
    ASSERT_EQ(parameters->parameters().size(), 2U);

    const auto first = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(first) << first.error().to_string();
    EXPECT_EQ(first->id, PlanParameterId{0U});
    EXPECT_EQ(first->kind, PlanParameterKind::kExternal);
    EXPECT_EQ(first->data_type, types::kInt64Type);

    const auto executor = parameters->allocate_executor_parameter(types::kBoolType);
    ASSERT_TRUE(executor) << executor.error().to_string();
    EXPECT_EQ(executor->id, PlanParameterId{2U});
    EXPECT_EQ(executor->kind, PlanParameterKind::kExecutor);
    EXPECT_EQ(executor->data_type, types::kBoolType);
    ASSERT_EQ(parameters->parameters().size(), 3U);
    EXPECT_FALSE(parameters->parameters().back().external_slot.has_value());

    const auto missing = parameters->external_reference(ExternalParameterNumber{3U});
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kPlan);
}

template <typename Result>
void expect_namespace_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive(ParameterNamespace& parameters) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(parameters.valid());
    EXPECT_TRUE(parameters.parameters().empty());
    EXPECT_EQ(parameters.external_parameter_count(), 0U);
    expect_namespace_unavailable(parameters.external_reference(ExternalParameterNumber{1U}));
    expect_namespace_unavailable(parameters.allocate_executor_parameter(types::kInt64Type));
    expect_namespace_unavailable(parameters.reference(PlanParameterId{0U}));
}

TEST(ParameterNamespaceTest, MoveTransfersSlotsAndLeavesTheSourceInert) {
    const std::vector external{
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
    };
    auto created = ParameterNamespace::from_external_slots(external);
    ASSERT_TRUE(created) << created.error().to_string();
    ParameterNamespace source = std::move(created).value();

    ParameterNamespace moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.external_parameter_count(), 1U);
    EXPECT_EQ(moved.parameters().size(), 1U);
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(source);

    auto empty = ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(empty) << empty.error().to_string();
    ParameterNamespace assigned = std::move(empty).value();
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(assigned.external_parameter_count(), 1U);
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(moved);

    Plan plan = make_seq_scan(equal_id_to(7));
    // Passing the documented inert source verifies the validating consumer boundary.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_namespace_unavailable(validate_plan(plan, moved));
}

TEST(ParameterNamespaceTest, RejectsSparseAndUnknownExternalDefinitions) {
    const std::vector sparse{
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{2U},
            .data_type = types::kInt64Type,
        },
    };
    const auto sparse_namespace = ParameterNamespace::from_external_slots(sparse);
    ASSERT_FALSE(sparse_namespace);
    EXPECT_EQ(sparse_namespace.error().code(), base::ErrorCode::kPlan);

    const std::vector unknown{
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{1U},
            .data_type = types::kUnknownType,
        },
    };
    const auto unknown_namespace = ParameterNamespace::from_external_slots(unknown);
    ASSERT_FALSE(unknown_namespace);
    EXPECT_EQ(unknown_namespace.error().code(), base::ErrorCode::kPlan);
}

TEST(ParameterNamespaceTest, PreservesTypedExternalSlotsWithoutBoundAst) {
    const std::vector external_slots{
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{2U},
            .data_type = types::kFloat32Type,
        },
    };

    const auto parameters = ParameterNamespace::from_external_slots(external_slots);
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    ASSERT_EQ(parameters->parameters().size(), 2U);
    EXPECT_EQ(parameters->parameters()[0U].id, PlanParameterId{0U});
    EXPECT_EQ(parameters->parameters()[0U].external_slot, ExternalParameterNumber{1U});
    EXPECT_EQ(parameters->parameters()[1U].id, PlanParameterId{1U});
    EXPECT_EQ(parameters->parameters()[1U].external_slot, ExternalParameterNumber{2U});
}

TEST(PlanTest, ValidatesTypedScanAndReportsOutputSchema) {
    Plan plan = make_seq_scan(equal_id_to(7));

    const auto valid = validate_plan(plan);
    ASSERT_TRUE(valid) << valid.error().to_string();
    EXPECT_EQ(plan_operator_kind(plan), PlanOperatorKind::kSeqScan);
    EXPECT_EQ(plan_metadata(plan).output, account_schema());
}

TEST(PlanTest, RejectsInvalidPhysicalSortEnums) {
    const PlanSchema schema = account_schema();
    Plan plan{SortPlan{
        .metadata = metadata(schema),
        .input = std::make_unique<Plan>(make_seq_scan()),
        .order_by = {},
    }};
    auto* const sort = std::get_if<SortPlan>(&plan.node);
    ASSERT_NE(sort, nullptr);
    sort->order_by.push_back(
        PlanSortItem{
            .expression = input_column(PlanColumnIndex{0U}, types::kInt64Type),
            // The validator must reject enum values decoded outside the public domain.
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            .direction = static_cast<PlanSortDirection>(255U),
            .null_order = PlanNullOrder::kDefault,
        }
    );

    const auto invalid_direction = validate_plan(plan);
    ASSERT_FALSE(invalid_direction);
    EXPECT_EQ(invalid_direction.error().code(), base::ErrorCode::kPlan);

    sort->order_by.front().direction = PlanSortDirection::kAscending;
    // The validator must reject enum values decoded outside the public domain.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    sort->order_by.front().null_order = static_cast<PlanNullOrder>(255U);
    const auto invalid_null_order = validate_plan(plan);
    ASSERT_FALSE(invalid_null_order);
    EXPECT_EQ(invalid_null_order.error().code(), base::ErrorCode::kPlan);
}

TEST(PlanTest, RejectsLogicalOperatorsEncodedAsScalarBinaryExpressions) {
    const PlanSchema output{
        .columns = {{.name = "invalid", .data_type = types::kInt64Type}},
    };
    std::vector<PlanProjectionItem> projection;
    projection.push_back(
        PlanProjectionItem{
            .expression = std::make_unique<PlanExpression>(
                PlanSourceSpan{},
                types::kInt64Type,
                PlanBinaryExpression{
                    .op = types::BinaryOperator::kOr,
                    .left = input_column(PlanColumnIndex{0U}, types::kInt64Type),
                    .right = integer_literal(1),
                }
            ),
            .output_name = "invalid",
        }
    );
    Plan plan{ResultPlan{
        .metadata = metadata(output),
        .input = std::make_unique<Plan>(make_seq_scan()),
        .projection = std::move(projection),
    }};

    const auto valid = validate_plan(plan);
    ASSERT_FALSE(valid);
    EXPECT_EQ(valid.error().code(), base::ErrorCode::kPlan);
}

TEST(PlanTest, KeepsFullRowAndRequiredColumnMaterializationDistinct) {
    Plan full_row_scan = make_seq_scan();
    auto* const full_row = std::get_if<SeqScanPlan>(&full_row_scan.node);
    ASSERT_NE(full_row, nullptr);
    full_row->required_columns = std::nullopt;
    const auto full_row_valid = validate_plan(full_row_scan);
    ASSERT_TRUE(full_row_valid) << full_row_valid.error().to_string();

    Plan projected_scan = make_seq_scan();
    auto* const projected = std::get_if<SeqScanPlan>(&projected_scan.node);
    ASSERT_NE(projected, nullptr);
    projected->required_columns = std::vector{PlanColumnIndex{0U}};
    const auto projected_valid = validate_plan(projected_scan);
    ASSERT_TRUE(projected_valid) << projected_valid.error().to_string();
}

TEST(PlanTest, ValidatesLeadingIndexBoundsWithOneLowerAndOneUpperEndpoint) {
    Plan plan = make_index_scan();
    auto* const scan = std::get_if<IndexScanPlan>(&plan.node);
    ASSERT_NE(scan, nullptr);
    scan->index.keys.push_back(
        PlanIndexKey{
            .relation_column = PlanColumnIndex{1U},
            .column_name = "balance",
            .data_type = types::kFloat32Type,
        }
    );
    scan->bounds.front().comparison = types::BinaryOperator::kEqual;
    scan->bounds.push_back(
        PlanIndexBound{
            .key_index = PlanIndexKeyIndex{1U},
            .comparison = types::BinaryOperator::kGreater,
            .value = floating_literal(10.0F),
        }
    );
    scan->bounds.push_back(
        PlanIndexBound{
            .key_index = PlanIndexKeyIndex{1U},
            .comparison = types::BinaryOperator::kLessEqual,
            .value = floating_literal(20.0F),
        }
    );

    const auto valid = validate_plan(plan);
    ASSERT_TRUE(valid) << valid.error().to_string();
}

TEST(PlanTest, RejectsNonleadingAndDuplicatePhysicalIndexBounds) {
    Plan plan = make_index_scan();
    auto* const scan = std::get_if<IndexScanPlan>(&plan.node);
    ASSERT_NE(scan, nullptr);
    scan->index.keys.push_back(
        PlanIndexKey{
            .relation_column = PlanColumnIndex{1U},
            .column_name = "balance",
            .data_type = types::kFloat32Type,
        }
    );
    scan->bounds.front().key_index = PlanIndexKeyIndex{1U};
    scan->bounds.front().value = floating_literal(7.0F);
    const auto nonleading = validate_plan(plan);
    ASSERT_FALSE(nonleading);
    EXPECT_EQ(nonleading.error().code(), base::ErrorCode::kPlan);

    scan->bounds.front().key_index = PlanIndexKeyIndex{0U};
    scan->bounds.front().comparison = types::BinaryOperator::kGreater;
    scan->bounds.push_back(
        PlanIndexBound{
            .key_index = PlanIndexKeyIndex{0U},
            .comparison = types::BinaryOperator::kGreaterEqual,
            .value = integer_literal(9),
        }
    );
    const auto duplicate_lower = validate_plan(plan);
    ASSERT_FALSE(duplicate_lower);
    EXPECT_EQ(duplicate_lower.error().code(), base::ErrorCode::kPlan);

    scan->bounds.pop_back();
    scan->bounds.front().comparison = types::BinaryOperator::kEqual;
    scan->bounds.push_back(
        PlanIndexBound{
            .key_index = PlanIndexKeyIndex{0U},
            .comparison = types::BinaryOperator::kLess,
            .value = integer_literal(9),
        }
    );
    const auto equality_and_range = validate_plan(plan);
    ASSERT_FALSE(equality_and_range);
    EXPECT_EQ(equality_and_range.error().code(), base::ErrorCode::kPlan);

    scan->bounds.pop_back();
    scan->bounds.push_back(
        PlanIndexBound{
            .key_index = PlanIndexKeyIndex{0U},
            .comparison = types::BinaryOperator::kEqual,
            .value = integer_literal(9),
        }
    );
    const auto duplicate_equality = validate_plan(plan);
    ASSERT_FALSE(duplicate_equality);
    EXPECT_EQ(duplicate_equality.error().code(), base::ErrorCode::kPlan);
}

TEST(PlanTest, RejectsAnInputReferenceOutsideOperatorSchema) {
    Plan plan = make_seq_scan();
    auto* const scan = std::get_if<SeqScanPlan>(&plan.node);
    ASSERT_NE(scan, nullptr);
    scan->filter = std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        types::kBoolType,
        PlanBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = input_column(PlanColumnIndex{9U}, types::kInt64Type),
            .right = integer_literal(7),
        }
    );

    const auto valid = validate_plan(plan);
    ASSERT_FALSE(valid);
    EXPECT_EQ(valid.error().code(), base::ErrorCode::kPlan);
    EXPECT_NE(valid.error().message().find("outside operator input"), std::string::npos);
}

TEST(PlanTest, ValidatesPhysicalParametersAgainstStatementNamespace) {
    const std::vector external{
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
    };
    auto parameters = ParameterNamespace::from_external_slots(external);
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    const auto parameter = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(parameter) << parameter.error().to_string();

    Plan plan = make_seq_scan();
    auto* const scan = std::get_if<SeqScanPlan>(&plan.node);
    ASSERT_NE(scan, nullptr);
    scan->filter = std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        types::kBoolType,
        PlanBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = input_column(PlanColumnIndex{0U}, types::kInt64Type),
            .right = std::make_unique<PlanExpression>(
                PlanSourceSpan{},
                types::kInt64Type,
                parameter.value()
            ),
        }
    );

    // The structural overload permits a self-consistent parameter reference;
    // the statement boundary overload additionally proves that its ID belongs
    // to this exact prepared-plan namespace.
    EXPECT_TRUE(validate_plan(plan));
    const auto valid = validate_plan(plan, *parameters);
    ASSERT_TRUE(valid) << valid.error().to_string();

    auto* const comparison = std::get_if<PlanBinaryExpression>(&scan->filter->node);
    ASSERT_NE(comparison, nullptr);
    auto* const right = std::get_if<PlanParameterReference>(&comparison->right->node);
    ASSERT_NE(right, nullptr);
    right->id = PlanParameterId{7U};
    const auto absent = validate_plan(plan, *parameters);
    ASSERT_FALSE(absent);
    EXPECT_EQ(absent.error().code(), base::ErrorCode::kPlan);
}

TEST(PlanTest, ValidatesAReusablePlannedStatementAgainstItsOwnNamespace) {
    const std::vector external{
        ExternalParameterSlot{
            .slot = ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
    };
    auto parameters = ParameterNamespace::from_external_slots(external);
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    const auto parameter = parameters->external_reference(ExternalParameterNumber{1U});
    ASSERT_TRUE(parameter) << parameter.error().to_string();

    Plan root = make_seq_scan();
    auto* const scan = std::get_if<SeqScanPlan>(&root.node);
    ASSERT_NE(scan, nullptr);
    scan->filter = std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        types::kBoolType,
        PlanBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = input_column(PlanColumnIndex{0U}, types::kInt64Type),
            .right = std::make_unique<PlanExpression>(
                PlanSourceSpan{},
                types::kInt64Type,
                parameter.value()
            ),
        }
    );
    auto statement = PlannedStatement::create(88U, std::move(root), std::move(parameters).value());
    ASSERT_TRUE(statement) << statement.error().to_string();

    EXPECT_EQ(statement->catalog_generation(), 88U);
    EXPECT_EQ(plan_operator_kind(statement->root()), PlanOperatorKind::kSeqScan);
    ASSERT_EQ(statement->parameter_namespace().parameters().size(), 1U);

    const auto valid = validate_planned_statement(*statement);
    ASSERT_TRUE(valid) << valid.error().to_string();
    const auto text = render_explain(*statement);
    ASSERT_TRUE(text) << text.error().to_string();
    EXPECT_NE(text->find("$1"), std::string::npos);
}

TEST(PlanTest, RejectsMalformedPlanAtCompletedStatementBoundary) {
    auto parameters = ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(parameters) << parameters.error().to_string();

    Plan root = make_seq_scan();
    auto* const scan = std::get_if<SeqScanPlan>(&root.node);
    ASSERT_NE(scan, nullptr);
    scan->filter = std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        types::kBoolType,
        PlanBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = input_column(PlanColumnIndex{9U}, types::kInt64Type),
            .right = integer_literal(7),
        }
    );

    const auto statement =
        PlannedStatement::create(88U, std::move(root), std::move(parameters).value());
    ASSERT_FALSE(statement);
    EXPECT_EQ(statement.error().code(), base::ErrorCode::kPlan);
    ASSERT_FALSE(statement.error().contexts().empty());
    EXPECT_EQ(statement.error().contexts().front().operation, "create planned statement");
}

TEST(PlanTest, ValidatesNestedLoopExecutorParameterAndChildOwnership) {
    auto parameters = ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    const auto executor = parameters->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor) << executor.error().to_string();

    Plan outer = make_seq_scan();
    Plan inner = make_seq_scan();
    const PlanSchema joined{
        .columns = {
            account_schema().columns.at(0U),
            account_schema().columns.at(1U),
            account_schema().columns.at(0U),
            account_schema().columns.at(1U),
        },
    };
    Plan plan{NestedLoopPlan{
        .metadata = metadata(joined),
        .outer = std::make_unique<Plan>(std::move(outer)),
        .inner = std::make_unique<Plan>(std::move(inner)),
        .join_filter = nullptr,
        .parameters = {{.parameter = executor.value(), .outer_column = PlanColumnIndex{0U}}},
    }};

    const auto valid = validate_plan(plan, *parameters);
    ASSERT_TRUE(valid) << valid.error().to_string();

    auto* const nested_loop = std::get_if<NestedLoopPlan>(&plan.node);
    ASSERT_NE(nested_loop, nullptr);
    nested_loop->parameters.push_back(
        NestedLoopParameter{
            .parameter = executor.value(),
            .outer_column = PlanColumnIndex{0U},
        }
    );
    const auto duplicate = validate_plan(plan, *parameters);
    ASSERT_FALSE(duplicate);
    EXPECT_NE(duplicate.error().message().find("duplicate"), std::string::npos);
}

TEST(PlanTest, RejectsJoinOutputThatDoesNotMatchItsOwnedChildren) {
    Plan outer = make_seq_scan();
    Plan inner = make_seq_scan();
    Plan plan{HashJoinPlan{
        .metadata = metadata(account_schema()),
        .outer = std::make_unique<Plan>(std::move(outer)),
        .inner = std::make_unique<Plan>(std::move(inner)),
        .join_columns =
            {{.outer_column = PlanColumnIndex{0U}, .inner_column = PlanColumnIndex{0U}}},
        .residual_filter = nullptr,
    }};

    const auto valid = validate_plan(plan);
    ASSERT_FALSE(valid);
    EXPECT_EQ(valid.error().code(), base::ErrorCode::kPlan);
    EXPECT_NE(valid.error().message().find("joined input schema"), std::string::npos);
}

TEST(PlanTest, AcceptsInsertQueryWithDifferentOutputNamesButMappedTypes) {
    Plan source = make_seq_scan();
    std::vector<PlanColumnIndex> target_columns{PlanColumnIndex{0U}, PlanColumnIndex{1U}};
    Plan plan{ModifyTablePlan{
        .metadata = metadata(PlanSchema{}),
        .operation = ModifyOperation::kInsert,
        .target = account_relation(),
        .target_columns = std::move(target_columns),
        .assignments = {},
        .source = PlanQueryModificationSource{.input = std::make_unique<Plan>(std::move(source))},
    }};

    const auto valid = validate_plan(plan);
    ASSERT_TRUE(valid) << valid.error().to_string();

    auto* const modify = std::get_if<ModifyTablePlan>(&plan.node);
    ASSERT_NE(modify, nullptr);
    auto* const query = std::get_if<PlanQueryModificationSource>(&modify->source);
    ASSERT_NE(query, nullptr);
    ASSERT_NE(query->input, nullptr);
    auto* const scan = std::get_if<SeqScanPlan>(&query->input->node);
    ASSERT_NE(scan, nullptr);
    scan->relation.schema.columns.at(1U).data_type = types::kInt64Type;
    scan->metadata.output.columns.at(1U).data_type = types::kInt64Type;
    const auto invalid = validate_plan(plan);
    ASSERT_FALSE(invalid);
    EXPECT_NE(invalid.error().message().find("mapped target types"), std::string::npos);
}

TEST(PlanTest, ValidatesAggregateOutputTypesAndResolvedCastPaths) {
    Plan input = make_seq_scan();
    const PlanSchema schema{
        .columns = {
            {.name = "total", .data_type = types::kFloat32Type, .origin = std::nullopt},
        },
    };
    std::vector<PlanAggregate> aggregates;
    std::vector<PlanExpressionPtr> aggregate_arguments;
    aggregate_arguments.push_back(input_column(PlanColumnIndex{1U}, types::kFloat32Type));
    aggregates.push_back(
        PlanAggregate{
            .kind = PlanAggregateKind::kSum,
            .arguments = std::move(aggregate_arguments),
            .distinct = false,
            .star = false,
            .output_name = "total",
            .data_type = types::kFloat32Type,
            .aggregate_index = PlanAggregateIndex{0U},
        }
    );
    Plan plan{AggregatePlan{
        .metadata = metadata(schema),
        .input = std::make_unique<Plan>(std::move(input)),
        .group_by = {},
        .aggregates = std::move(aggregates),
        .having = nullptr,
        .ordered_min = false,
    }};

    const auto valid = validate_plan(plan);
    ASSERT_TRUE(valid) << valid.error().to_string();

    auto* const aggregate = std::get_if<AggregatePlan>(&plan.node);
    ASSERT_NE(aggregate, nullptr);
    aggregate->aggregates.front().data_type = types::kInt64Type;
    const auto invalid = validate_plan(plan);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kPlan);
}

TEST(PlanTest, ValidatesHavingAgainstAggregateOutputRatherThanBaseInput) {
    Plan input = make_seq_scan();
    const PlanSchema schema{
        .columns = {{.name = "count", .data_type = types::kInt64Type, .origin = std::nullopt}},
    };
    std::vector<PlanAggregate> aggregates;
    aggregates.push_back(
        PlanAggregate{
            .kind = PlanAggregateKind::kCount,
            .arguments = {},
            .distinct = false,
            .star = true,
            .output_name = "count",
            .data_type = types::kInt64Type,
            .aggregate_index = PlanAggregateIndex{0U},
        }
    );
    Plan plan{AggregatePlan{
        .metadata = metadata(schema),
        .input = std::make_unique<Plan>(std::move(input)),
        .group_by = {},
        .aggregates = std::move(aggregates),
        .having = std::make_unique<PlanExpression>(
            PlanSourceSpan{},
            types::kBoolType,
            PlanBinaryExpression{
                .op = types::BinaryOperator::kGreater,
                .left = input_column(PlanColumnIndex{0U}, types::kInt64Type),
                .right = integer_literal(0),
            }
        ),
        .ordered_min = false,
    }};

    const auto valid = validate_plan(plan);
    ASSERT_TRUE(valid) << valid.error().to_string();
}

TEST(PlanTest, RejectsCastWhoseStoredCoercionPathDoesNotMatchItsTypes) {
    const auto mismatched_path = types::find_coercion_path(
        types::kFloat32Type,
        types::kInt64Type,
        types::CoercionContext::kAssignment
    );
    ASSERT_TRUE(mismatched_path);
    Plan plan = make_seq_scan();
    auto* const scan = std::get_if<SeqScanPlan>(&plan.node);
    ASSERT_NE(scan, nullptr);
    scan->filter = std::make_unique<PlanExpression>(
        PlanSourceSpan{},
        types::kBoolType,
        PlanBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = std::make_unique<PlanExpression>(
                PlanSourceSpan{},
                types::kFloat32Type,
                PlanCastExpression{
                    .operand = integer_literal(7),
                    .target_type = types::kFloat32Type,
                    .path = *mismatched_path,
                    .form = PlanCastForm::kImplicit,
                }
            ),
            .right = floating_literal(7.0F),
        }
    );

    const auto valid = validate_plan(plan);
    ASSERT_FALSE(valid);
    EXPECT_EQ(valid.error().code(), base::ErrorCode::kPlan);
    EXPECT_NE(valid.error().message().find("cast path"), std::string::npos);
}

TEST(PlanTest, ValidatesSubplanAndLocalExecutorMappingsInOneNamespace) {
    auto parameters = ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    const auto executor = parameters->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor) << executor.error().to_string();

    Plan input = make_seq_scan();
    Plan subplan_input = make_seq_scan();
    std::vector<PlanExpressionPtr> arguments;
    arguments.push_back(input_column(PlanColumnIndex{0U}, types::kInt64Type));
    std::vector<PlanParameterReference> subplan_parameters;
    subplan_parameters.push_back(executor.value());
    std::vector<PlanSubPlan> subplans;
    subplans.push_back(
        PlanSubPlan{
            .plan = std::make_unique<Plan>(std::move(subplan_input)),
            .strategy = PlanSubPlanStrategy::kExists,
            .input_expression = nullptr,
            .arguments = std::move(arguments),
            .parameters = std::move(subplan_parameters),
            .uses_inherited_parameters = true,
        }
    );
    Plan plan{SubPlanEvalPlan{
        .metadata = metadata(account_schema()),
        .input = std::make_unique<Plan>(std::move(input)),
        .subplans = std::move(subplans),
        .filter = nullptr,
    }};

    const auto valid = validate_plan(plan, *parameters);
    ASSERT_TRUE(valid) << valid.error().to_string();
}

TEST(PlanTest, RejectsSubplanWithIncompatibleExecutorMapping) {
    auto parameters = ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    const auto executor = parameters->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor) << executor.error().to_string();

    Plan input = make_seq_scan();
    Plan subplan_input = make_seq_scan();
    std::vector<PlanExpressionPtr> arguments;
    arguments.push_back(integer_literal(1));
    std::vector<PlanParameterReference> subplan_parameters;
    subplan_parameters.push_back(
        PlanParameterReference{
            .id = executor->id,
            .kind = PlanParameterKind::kExternal,
            .data_type = types::kInt64Type,
        }
    );
    std::vector<PlanSubPlan> subplans;
    subplans.push_back(
        PlanSubPlan{
            .plan = std::make_unique<Plan>(std::move(subplan_input)),
            .strategy = PlanSubPlanStrategy::kExists,
            .input_expression = nullptr,
            .arguments = std::move(arguments),
            .parameters = std::move(subplan_parameters),
            .uses_inherited_parameters = false,
        }
    );
    Plan plan{SubPlanEvalPlan{
        .metadata = metadata(account_schema()),
        .input = std::make_unique<Plan>(std::move(input)),
        .subplans = std::move(subplans),
        .filter = nullptr,
    }};

    const auto valid = validate_plan(plan, *parameters);
    ASSERT_FALSE(valid);
    EXPECT_EQ(valid.error().code(), base::ErrorCode::kPlan);
    EXPECT_NE(valid.error().message().find("executor-owned"), std::string::npos);
}

TEST(PlanTest, RejectsSubplanArgumentWhoseTypeDiffersFromItsExecutorSlot) {
    auto parameters = ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    const auto executor = parameters->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor) << executor.error().to_string();

    Plan input = make_seq_scan();
    Plan subplan_input = make_seq_scan();
    std::vector<PlanExpressionPtr> arguments;
    arguments.push_back(floating_literal(1.0F));
    std::vector<PlanParameterReference> subplan_parameters;
    subplan_parameters.push_back(executor.value());
    std::vector<PlanSubPlan> subplans;
    subplans.push_back(
        PlanSubPlan{
            .plan = std::make_unique<Plan>(std::move(subplan_input)),
            .strategy = PlanSubPlanStrategy::kExists,
            .input_expression = nullptr,
            .arguments = std::move(arguments),
            .parameters = std::move(subplan_parameters),
            .uses_inherited_parameters = false,
        }
    );
    Plan plan{SubPlanEvalPlan{
        .metadata = metadata(account_schema()),
        .input = std::make_unique<Plan>(std::move(input)),
        .subplans = std::move(subplans),
        .filter = nullptr,
    }};

    const auto valid = validate_plan(plan, *parameters);
    ASSERT_FALSE(valid);
    EXPECT_EQ(valid.error().code(), base::ErrorCode::kPlan);
    EXPECT_NE(valid.error().message().find("argument type"), std::string::npos);
}

TEST(PlanTest, RequiresSubplanMappingsToUseOnlyEarlierResults) {
    auto parameters = ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(parameters) << parameters.error().to_string();
    const auto executor = parameters->allocate_executor_parameter(types::kBoolType);
    ASSERT_TRUE(executor) << executor.error().to_string();

    Plan input = make_seq_scan();
    Plan first_subplan_input = make_seq_scan();
    Plan second_subplan_input = make_seq_scan();
    auto* const second_scan = std::get_if<SeqScanPlan>(&second_subplan_input.node);
    ASSERT_NE(second_scan, nullptr);
    second_scan->filter =
        std::make_unique<PlanExpression>(PlanSourceSpan{}, types::kBoolType, executor.value());

    std::vector<PlanExpressionPtr> second_arguments;
    second_arguments.push_back(
        std::make_unique<PlanExpression>(
            PlanSourceSpan{},
            types::kBoolType,
            PlanIsNullExpression{
                .negated = false,
                .operand = subplan_reference(PlanSubPlanIndex{0U}, types::kBoolType),
            }
        )
    );
    std::vector<PlanParameterReference> second_parameters;
    second_parameters.push_back(executor.value());
    std::vector<PlanSubPlan> subplans;
    subplans.push_back(
        PlanSubPlan{
            .plan = std::make_unique<Plan>(std::move(first_subplan_input)),
            .strategy = PlanSubPlanStrategy::kExists,
            .input_expression = nullptr,
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    subplans.push_back(
        PlanSubPlan{
            .plan = std::make_unique<Plan>(std::move(second_subplan_input)),
            .strategy = PlanSubPlanStrategy::kExists,
            .input_expression = nullptr,
            .arguments = std::move(second_arguments),
            .parameters = std::move(second_parameters),
            .uses_inherited_parameters = false,
        }
    );
    Plan plan{SubPlanEvalPlan{
        .metadata = metadata(account_schema()),
        .input = std::make_unique<Plan>(std::move(input)),
        .subplans = std::move(subplans),
        .filter = nullptr,
    }};

    const auto valid = validate_plan(plan, *parameters);
    ASSERT_TRUE(valid) << valid.error().to_string();

    auto* const evaluation = std::get_if<SubPlanEvalPlan>(&plan.node);
    ASSERT_NE(evaluation, nullptr);
    auto* const nested_reference =
        std::get_if<PlanIsNullExpression>(&evaluation->subplans.at(1U).arguments.at(0U)->node);
    ASSERT_NE(nested_reference, nullptr);
    nested_reference->operand = subplan_reference(PlanSubPlanIndex{1U}, types::kBoolType);
    const auto out_of_order = validate_plan(plan, *parameters);
    ASSERT_FALSE(out_of_order);
    EXPECT_EQ(out_of_order.error().code(), base::ErrorCode::kPlan);
    EXPECT_EQ(
        out_of_order.error().message(),
        "subplan reference is outside the enclosing subplan list"
    );
    EXPECT_TRUE(out_of_order.error().contexts().empty());

    evaluation->subplans.at(1U).input_expression =
        subplan_reference(PlanSubPlanIndex{1U}, types::kBoolType);
    evaluation->subplans.at(1U).arguments.emplace_back();
    const auto input_error = validate_plan(plan, *parameters);
    ASSERT_FALSE(input_error);
    EXPECT_EQ(
        input_error.error().message(),
        "subplan reference is outside the enclosing subplan list"
    );

    evaluation->subplans.at(1U).input_expression.reset();
    const auto structural_error = validate_plan(plan, *parameters);
    ASSERT_FALSE(structural_error);
    EXPECT_EQ(structural_error.error().code(), base::ErrorCode::kPlan);
    EXPECT_EQ(structural_error.error().message(), "subplan argument is absent");
    EXPECT_TRUE(structural_error.error().contexts().empty());
}

TEST(PlanTest, RejectsSubplanReferenceOutsideItsEnclosingEvaluationNode) {
    Plan input = make_seq_scan();
    Plan plan{SubPlanEvalPlan{
        .metadata = metadata(account_schema()),
        .input = std::make_unique<Plan>(std::move(input)),
        .subplans = {},
        .filter = std::make_unique<PlanExpression>(
            PlanSourceSpan{},
            types::kInt64Type,
            PlanSubPlanReference{.index = PlanSubPlanIndex{0U}}
        ),
    }};

    const auto type_error = validate_plan(plan);
    ASSERT_FALSE(type_error);
    EXPECT_EQ(type_error.error().message(), "subplan-eval filter must have BOOL type");

    auto* const evaluation = std::get_if<SubPlanEvalPlan>(&plan.node);
    ASSERT_NE(evaluation, nullptr);
    evaluation->filter->data_type = types::kBoolType;
    const auto range_error = validate_plan(plan);
    ASSERT_FALSE(range_error);
    EXPECT_EQ(
        range_error.error().message(),
        "subplan reference is outside the enclosing subplan list"
    );

    auto* const reference = std::get_if<PlanSubPlanReference>(&evaluation->filter->node);
    ASSERT_NE(reference, nullptr);
    reference->index = PlanSubPlanIndex{};
    const auto index_error = validate_plan(plan);
    ASSERT_FALSE(index_error);
    EXPECT_EQ(index_error.error().message(), "subplan reference has an invalid index");
    EXPECT_TRUE(index_error.error().contexts().empty());
}

TEST(ExplainTest, RendersDeterministicScanAndFilter) {
    Plan plan = make_seq_scan(equal_id_to(7));

    const auto text = render_explain(plan);
    ASSERT_TRUE(text) << text.error().to_string();
    EXPECT_EQ(
        text.value(),
        "Seq Scan on account\n"
        "  Filter: (id = 7)\n"
    );
}

TEST(ExplainTest, RendersIndexSortAndChildrenInExecutionOrder) {
    Plan input = make_index_scan(equal_id_to(7));
    const PlanSchema schema = account_schema();
    std::vector<PlanSortItem> order_by;
    order_by.push_back(
        PlanSortItem{
            .expression = input_column(PlanColumnIndex{1U}, types::kFloat32Type),
            .direction = PlanSortDirection::kDescending,
            .null_order = PlanNullOrder::kLast,
        }
    );
    Plan plan{SortPlan{
        .metadata = metadata(schema),
        .input = std::make_unique<Plan>(std::move(input)),
        .order_by = std::move(order_by),
    }};

    const auto text = render_explain(plan);
    ASSERT_TRUE(text) << text.error().to_string();
    EXPECT_EQ(
        text.value(),
        "Sort\n"
        "  Sort Key: balance DESC NULLS LAST\n"
        "  ->  Index Scan using account_id_idx on account\n"
        "        Index Cond: (id = 7)\n"
        "        Filter: (id = 7)\n"
    );
}

TEST(ExplainTest, RejectsMalformedPlansInsteadOfRenderingAnUnsafeTree) {
    Plan plan = make_index_scan();
    auto* const scan = std::get_if<IndexScanPlan>(&plan.node);
    ASSERT_NE(scan, nullptr);
    scan->bounds.front().key_index = PlanIndexKeyIndex{2U};

    const auto text = render_explain(plan);
    ASSERT_FALSE(text);
    EXPECT_EQ(text.error().code(), base::ErrorCode::kPlan);
    ASSERT_EQ(text.error().contexts().size(), 1U);
    EXPECT_EQ(text.error().contexts().front().component, "planner");
    EXPECT_EQ(text.error().contexts().front().operation, "render_explain");
}

}  // namespace
}  // namespace db::planner
