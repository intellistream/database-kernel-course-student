#include "db/execution/modify_table.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "state_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto modify_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "execution.modify_table",
        "construct"
    );
}

[[maybe_unused]] auto modify_runtime_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kExecution, std::move(message)}.with_context(
        "execution.modify_table",
        "run"
    );
}

[[maybe_unused]] auto make_empty_input() -> base::Result<Tuple> {
    auto descriptor = TupleDescriptor::create({});
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return Tuple::create(std::move(descriptor).value(), {});
}

[[maybe_unused]] auto compile_expressions(
    const std::vector<planner::PlanExpressionPtr>& expressions,
    const std::string_view operation
) -> base::Result<std::vector<ExpressionProgram>> {
    std::vector<ExpressionProgram> compiled;
    compiled.reserve(expressions.size());
    for (const planner::PlanExpressionPtr& expression : expressions) {
        if (expression == nullptr) {
            return base::fail(modify_error(
                base::ErrorCode::kPlan,
                std::string{operation} + " contains a null expression"
            ));
        }
        auto program = ExpressionProgram::compile(*expression);
        if (!program) {
            return base::fail(std::move(program).error());
        }
        compiled.push_back(std::move(program).value());
    }
    return compiled;
}

[[maybe_unused]] auto validate_schema(
    const TupleDescriptorHandle& actual,
    const TupleDescriptorHandle& expected,
    const std::string_view operation
) -> base::Status {
    if (actual == nullptr || expected == nullptr || actual->size() != expected->size()) {
        return base::fail(modify_runtime_error(std::string{operation} + " returned invalid arity"));
    }
    const auto actual_columns = actual->columns();
    const auto expected_columns = expected->columns();
    for (std::size_t index = 0U; index < expected_columns.size(); ++index) {
        if (actual_columns[index].data_type != expected_columns[index].data_type) {
            return base::fail(
                modify_runtime_error(std::string{operation} + " returned invalid types")
            );
        }
    }
    return {};
}

[[maybe_unused]] auto is_identity_mapping(
    const std::vector<std::size_t>& columns,
    const std::size_t target_width
) noexcept -> bool {
    if (columns.size() != target_width) {
        return false;
    }
    for (std::size_t index = 0U; index < columns.size(); ++index) {
        if (columns[index] != index) {
            return false;
        }
    }
    return true;
}

}  // namespace

// Construction records the complete immutable DML contract before execution can observe the state.
// NOLINTNEXTLINE(readability-function-size)
ModifyTableState::ModifyTableState(
    TupleSlot output_slot,
    const Operation operation,
    ModifyTarget target,
    std::vector<std::size_t> target_columns,
    std::vector<std::size_t> assignment_columns,
    std::vector<ExpressionProgram> compiled_assignments,
    const bool query_insert_identity_mapping,
    Source source,
    Tuple empty_input,
    ModifyTablePort& port
)
    : PlanState{std::move(output_slot)},
      operation_{operation},
      target_{std::move(target)},
      target_columns_{std::move(target_columns)},
      assignment_columns_{std::move(assignment_columns)},
      compiled_assignments_{std::move(compiled_assignments)},
      query_insert_identity_mapping_{query_insert_identity_mapping},
      source_{std::move(source)},
      empty_input_{std::move(empty_input)},
      port_{&port} {}

auto ModifyTableState::collect_target_columns(const planner::ModifyTablePlan& plan)
    -> base::Result<std::vector<std::size_t>> {
    std::vector<std::size_t> columns;
    columns.reserve(plan.target_columns.size());
    for (const planner::PlanColumnIndex column : plan.target_columns) {
        const std::size_t index = column.zero_based();
        if (index >= plan.target.schema.columns.size()) {
            return base::fail(modify_error(
                base::ErrorCode::kPlan,
                "ModifyTable target column is outside target schema"
            ));
        }
        if (std::find(columns.begin(), columns.end(), index) != columns.end()) {
            return base::fail(
                modify_error(base::ErrorCode::kPlan, "ModifyTable target column repeats")
            );
        }
        columns.push_back(index);
    }
    return columns;
}

auto ModifyTableState::compile_assignments(const planner::ModifyTablePlan& plan)
    -> base::Result<ModifyTableState::CompiledAssignments> {
    if (plan.assignments.empty()) {
        return base::fail(
            modify_error(base::ErrorCode::kPlan, "UPDATE requires at least one assignment")
        );
    }
    CompiledAssignments result;
    result.columns.reserve(plan.assignments.size());
    result.programs.reserve(plan.assignments.size());
    for (const planner::PlanAssignment& assignment : plan.assignments) {
        const std::size_t index = assignment.target_column.zero_based();
        if (index >= plan.target.schema.columns.size() || assignment.expression == nullptr) {
            return base::fail(
                modify_error(base::ErrorCode::kPlan, "ModifyTable assignment is invalid")
            );
        }
        if (std::find(result.columns.begin(), result.columns.end(), index)
            != result.columns.end()) {
            return base::fail(
                modify_error(base::ErrorCode::kPlan, "ModifyTable assignment repeats")
            );
        }
        if (assignment.expression->data_type != plan.target.schema.columns[index].data_type) {
            return base::fail(modify_error(
                base::ErrorCode::kPlan,
                "ModifyTable assignment type differs from target column"
            ));
        }
        auto program = ExpressionProgram::compile(*assignment.expression);
        if (!program) {
            return base::fail(std::move(program).error());
        }
        result.columns.push_back(index);
        result.programs.push_back(std::move(program).value());
    }
    return result;
}

auto ModifyTableState::create_update(
    const planner::ModifyTablePlan& plan,
    PlanStateFactory& factory,
    ModifyTablePort& port,
    ModifyTarget target,
    TupleSlot output_slot,
    Tuple empty_input,
    std::vector<std::size_t> target_columns
) -> base::Result<std::unique_ptr<ModifyTableState>> {
    auto assignments = compile_assignments(plan);
    if (!assignments) {
        return base::fail(std::move(assignments).error());
    }
    const auto* const query = std::get_if<planner::PlanQueryModificationSource>(&plan.source);
    if (query == nullptr || query->input == nullptr) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "UPDATE requires a query source"));
    }
    auto child = factory.create(*query->input);
    if (!child) {
        return base::fail(std::move(child).error());
    }
    if (child.value() == nullptr) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "ModifyTable source is null"));
    }
    const auto source_descriptor = child.value()->output_slot().descriptor();
    auto schema = validate_schema(source_descriptor, target.descriptor, "UPDATE source");
    if (!schema) {
        return base::fail(std::move(schema).error());
    }
    auto compiled = std::move(assignments).value();
    return std::unique_ptr<ModifyTableState>{new ModifyTableState{
        std::move(output_slot),
        Operation::kUpdate,
        std::move(target),
        std::move(target_columns),
        std::move(compiled.columns),
        std::move(compiled.programs),
        false,
        CompiledQuerySource{.state = std::move(child).value()},
        std::move(empty_input),
        port,
    }};
}

auto ModifyTableState::create_insert_values(
    ModifyTablePort& port,
    ModifyTarget target,
    TupleSlot output_slot,
    Tuple empty_input,
    std::vector<std::size_t> target_columns,
    const planner::PlanValuesModificationSource& values
) -> base::Result<std::unique_ptr<ModifyTableState>> {
    std::vector<CompiledInsertValues> rows;
    rows.reserve(values.rows.size());
    for (const std::vector<planner::PlanExpressionPtr>& row : values.rows) {
        if (row.size() != target_columns.size()) {
            return base::fail(
                modify_error(base::ErrorCode::kPlan, "INSERT VALUES width differs from mapping")
            );
        }
        for (std::size_t index = 0U; index < row.size(); ++index) {
            if (row[index] == nullptr
                || row[index]->data_type
                       != target.descriptor->columns()[target_columns[index]].data_type) {
                return base::fail(modify_error(
                    base::ErrorCode::kPlan,
                    "INSERT VALUES expression type differs from target column"
                ));
            }
        }
        auto compiled = compile_expressions(row, "INSERT VALUES row");
        if (!compiled) {
            return base::fail(std::move(compiled).error());
        }
        rows.push_back(CompiledInsertValues{.expressions = std::move(compiled).value()});
    }
    return std::unique_ptr<ModifyTableState>{new ModifyTableState{
        std::move(output_slot),
        Operation::kInsert,
        std::move(target),
        std::move(target_columns),
        {},
        {},
        false,
        std::move(rows),
        std::move(empty_input),
        port,
    }};
}

auto ModifyTableState::create_insert(
    const planner::ModifyTablePlan& plan,
    PlanStateFactory& factory,
    ModifyTablePort& port,
    ModifyTarget target,
    TupleSlot output_slot,
    Tuple empty_input,
    std::vector<std::size_t> target_columns
) -> base::Result<std::unique_ptr<ModifyTableState>> {
    if (!plan.assignments.empty()) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "INSERT cannot carry assignments"));
    }
    if (target_columns.empty()) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "INSERT requires target columns"));
    }
    if (const auto* const values =
            std::get_if<planner::PlanValuesModificationSource>(&plan.source)) {
        return create_insert_values(
            port,
            std::move(target),
            std::move(output_slot),
            std::move(empty_input),
            std::move(target_columns),
            *values
        );
    }
    const auto* const query = std::get_if<planner::PlanQueryModificationSource>(&plan.source);
    if (query == nullptr || query->input == nullptr) {
        return base::fail(
            modify_error(base::ErrorCode::kPlan, "INSERT requires VALUES or query source")
        );
    }
    auto child = factory.create(*query->input);
    if (!child) {
        return base::fail(std::move(child).error());
    }
    if (child.value() == nullptr) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "ModifyTable source is null"));
    }
    auto source_descriptor = child.value()->output_slot().descriptor();
    if (source_descriptor == nullptr || source_descriptor->size() != target_columns.size()) {
        return base::fail(modify_error(
            base::ErrorCode::kPlan,
            "INSERT query source width differs from target mapping"
        ));
    }
    for (std::size_t index = 0U; index < target_columns.size(); ++index) {
        if (source_descriptor->columns()[index].data_type
            != target.descriptor->columns()[target_columns[index]].data_type) {
            return base::fail(modify_error(
                base::ErrorCode::kPlan,
                "INSERT query source type differs from target mapping"
            ));
        }
    }
    const bool identity_mapping = is_identity_mapping(target_columns, target.descriptor->size());
    return std::unique_ptr<ModifyTableState>{new ModifyTableState{
        std::move(output_slot),
        Operation::kInsert,
        std::move(target),
        std::move(target_columns),
        {},
        {},
        identity_mapping,
        CompiledQuerySource{.state = std::move(child).value()},
        std::move(empty_input),
        port,
    }};
}

auto ModifyTableState::create_delete(
    const planner::ModifyTablePlan& plan,
    PlanStateFactory& factory,
    ModifyTablePort& port,
    ModifyTarget target,
    TupleSlot output_slot,
    Tuple empty_input
) -> base::Result<std::unique_ptr<ModifyTableState>> {
    if (!plan.assignments.empty()) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "DELETE cannot carry assignments"));
    }
    const auto* const query = std::get_if<planner::PlanQueryModificationSource>(&plan.source);
    if (query == nullptr || query->input == nullptr) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "DELETE requires a query source"));
    }
    auto child = factory.create(*query->input);
    if (!child) {
        return base::fail(std::move(child).error());
    }
    if (child.value() == nullptr) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "ModifyTable source is null"));
    }
    const auto source_descriptor = child.value()->output_slot().descriptor();
    auto schema = validate_schema(source_descriptor, target.descriptor, "DELETE source");
    if (!schema) {
        return base::fail(std::move(schema).error());
    }
    return std::unique_ptr<ModifyTableState>{new ModifyTableState{
        std::move(output_slot),
        Operation::kDelete,
        std::move(target),
        {},
        {},
        {},
        false,
        CompiledQuerySource{.state = std::move(child).value()},
        std::move(empty_input),
        port,
    }};
}

auto ModifyTableState::create(
    const planner::ModifyTablePlan& plan,
    PlanStateFactory& factory,
    ModifyTablePort& port
) -> base::Result<std::unique_ptr<ModifyTableState>> {
    const bool is_insert = plan.operation == planner::ModifyOperation::kInsert;
    const bool is_update = plan.operation == planner::ModifyOperation::kUpdate;
    const bool is_delete = plan.operation == planner::ModifyOperation::kDelete;
    if (!is_insert && !is_update && !is_delete) {
        return base::fail(modify_error(base::ErrorCode::kPlan, "ModifyTable operation is invalid"));
    }
    if (!plan.metadata.output.columns.empty()) {
        return base::fail(
            modify_error(base::ErrorCode::kPlan, "ModifyTable output schema must be empty")
        );
    }
    if (plan.target.oid == base::kInvalidOid) {
        return base::fail(
            modify_error(base::ErrorCode::kPlan, "ModifyTable target OID is invalid")
        );
    }
    if (plan.target.schema.columns.empty()) {
        return base::fail(modify_error(
            base::ErrorCode::kPlan,
            "ModifyTable target schema must contain at least one column"
        ));
    }
    auto target_descriptor = TupleDescriptor::from_plan_schema(plan.target.schema);
    if (!target_descriptor) {
        return base::fail(std::move(target_descriptor).error());
    }
    ModifyTarget target{
        .relation_oid = plan.target.oid,
        .descriptor = std::move(target_descriptor).value(),
    };
    auto output_slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!output_slot) {
        return base::fail(std::move(output_slot).error());
    }
    auto empty_input = make_empty_input();
    if (!empty_input) {
        return base::fail(std::move(empty_input).error());
    }
    auto target_columns = collect_target_columns(plan);
    if (!target_columns) {
        return base::fail(std::move(target_columns).error());
    }
    if (!is_insert && !target_columns->empty()) {
        return base::fail(modify_error(
            base::ErrorCode::kPlan,
            "UPDATE/DELETE cannot carry an INSERT target-column mapping"
        ));
    }
    if (is_update) {
        return create_update(
            plan,
            factory,
            port,
            std::move(target),
            std::move(output_slot).value(),
            std::move(empty_input).value(),
            std::move(target_columns).value()
        );
    }
    if (is_insert) {
        return create_insert(
            plan,
            factory,
            port,
            std::move(target),
            std::move(output_slot).value(),
            std::move(empty_input).value(),
            std::move(target_columns).value()
        );
    }
    return create_delete(
        plan,
        factory,
        port,
        std::move(target),
        std::move(output_slot).value(),
        std::move(empty_input).value()
    );
}

[[maybe_unused]] auto ModifyTableState::do_open([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S02.Q09:R01-BEGIN
    base::lab::missing_algorithm("S02.Q09:R01");
    // LAB-S02.Q09:R01-END
}

[[maybe_unused]] auto ModifyTableState::next_insert_values([[maybe_unused]] ExecContext& context)
    -> base::Result<std::vector<types::Value>> {
    // LAB-S02.Q09:R02-BEGIN
    base::lab::missing_algorithm("S02.Q09:R02");
    // LAB-S02.Q09:R02-END
}

[[maybe_unused]] auto ModifyTableState::next_source_tuple([[maybe_unused]] ExecContext& context)
    -> base::Result<std::optional<const Tuple*>> {
    // LAB-S02.Q09:R03-BEGIN
    base::lab::missing_algorithm("S02.Q09:R03");
    // LAB-S02.Q09:R03-END
}

[[maybe_unused]] auto ModifyTableState::execute_insert([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S02.Q09:R04-BEGIN
    base::lab::missing_algorithm("S02.Q09:R04");
    // LAB-S02.Q09:R04-END
}

[[maybe_unused]] auto ModifyTableState::execute_update(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] ExecContext& context
) -> base::Result<bool> {
    // LAB-S02.Q09:R05-BEGIN
    base::lab::missing_algorithm("S02.Q09:R05");
    // LAB-S02.Q09:R05-END
}

[[maybe_unused]] auto ModifyTableState::execute_delete([[maybe_unused]] const Tuple& tuple)
    -> base::Result<bool> {
    // LAB-S02.Q09:R06-BEGIN
    base::lab::missing_algorithm("S02.Q09:R06");
    // LAB-S02.Q09:R06-END
}

[[maybe_unused]] auto
ModifyTableState::store_mutation_result([[maybe_unused]] const std::optional<base::Rid> rid)
    -> base::Result<bool> {
    // LAB-S02.Q09:R07-BEGIN
    base::lab::missing_algorithm("S02.Q09:R07");
    // LAB-S02.Q09:R07-END
}

[[maybe_unused]] auto ModifyTableState::do_next([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S02.Q09:R08-BEGIN
    base::lab::missing_algorithm("S02.Q09:R08");
    // LAB-S02.Q09:R08-END
}

[[maybe_unused]] auto ModifyTableState::do_rescan([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S02.Q09:R09-BEGIN
    base::lab::missing_algorithm("S02.Q09:R09");
    // LAB-S02.Q09:R09-END
}

auto ModifyTableState::close_source(ExecContext& context) -> base::Status {
    if (auto* const query = std::get_if<CompiledQuerySource>(&source_); query != nullptr) {
        return query->state->close(context);
    }
    return {};
}

auto ModifyTableState::do_close(ExecContext& context) -> base::Status {
    seen_rids_.clear();
    return close_source(context);
}

}  // namespace db::execution
