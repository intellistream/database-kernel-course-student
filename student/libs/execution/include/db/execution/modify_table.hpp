#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/modify_port.hpp"
#include "db/execution/plan_state.hpp"
#include "db/execution/plan_state_factory.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// Executes a validated planner ModifyTable node. The output schema is empty:
// each successful mutation emits one empty command-result tuple, while
// affected_rows() exposes the deterministic command count to the engine.
// RETURNING remains an explicit parser/planner rejection. Any future
// result-producing extension must add a non-empty output schema without making
// this state depend on SQL AST or access internals.
class ModifyTableState final : public PlanState {
public:
    ModifyTableState(const ModifyTableState&) = delete;
    auto operator=(const ModifyTableState&) -> ModifyTableState& = delete;
    ModifyTableState(ModifyTableState&&) = delete;
    auto operator=(ModifyTableState&&) -> ModifyTableState& = delete;

    static auto
    create(const planner::ModifyTablePlan& plan, PlanStateFactory& factory, ModifyTablePort& port)
        -> base::Result<std::unique_ptr<ModifyTableState>>;

    ~ModifyTableState() override = default;

    auto affected_rows() const noexcept -> std::size_t {
        return affected_rows_;
    }

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    enum class Operation : std::uint8_t {
        kInsert,
        kUpdate,
        kDelete,
    };

    struct CompiledInsertValues final {
        std::vector<ExpressionProgram> expressions;
    };

    struct CompiledQuerySource final {
        std::unique_ptr<PlanState> state;
    };

    struct CompiledAssignments final {
        std::vector<std::size_t> columns;
        std::vector<ExpressionProgram> programs;
    };

    using Source =
        std::variant<std::monostate, std::vector<CompiledInsertValues>, CompiledQuerySource>;

    static auto collect_target_columns(const planner::ModifyTablePlan& plan)
        -> base::Result<std::vector<std::size_t>>;
    static auto compile_assignments(const planner::ModifyTablePlan& plan)
        -> base::Result<CompiledAssignments>;
    static auto create_update(
        const planner::ModifyTablePlan& plan,
        PlanStateFactory& factory,
        ModifyTablePort& port,
        ModifyTarget target,
        TupleSlot output_slot,
        Tuple empty_input,
        std::vector<std::size_t> target_columns
    ) -> base::Result<std::unique_ptr<ModifyTableState>>;
    static auto create_insert(
        const planner::ModifyTablePlan& plan,
        PlanStateFactory& factory,
        ModifyTablePort& port,
        ModifyTarget target,
        TupleSlot output_slot,
        Tuple empty_input,
        std::vector<std::size_t> target_columns
    ) -> base::Result<std::unique_ptr<ModifyTableState>>;
    static auto create_insert_values(
        ModifyTablePort& port,
        ModifyTarget target,
        TupleSlot output_slot,
        Tuple empty_input,
        std::vector<std::size_t> target_columns,
        const planner::PlanValuesModificationSource& values
    ) -> base::Result<std::unique_ptr<ModifyTableState>>;
    static auto create_delete(
        const planner::ModifyTablePlan& plan,
        PlanStateFactory& factory,
        ModifyTablePort& port,
        ModifyTarget target,
        TupleSlot output_slot,
        Tuple empty_input
    ) -> base::Result<std::unique_ptr<ModifyTableState>>;

    ModifyTableState(
        TupleSlot output_slot,
        Operation operation,
        ModifyTarget target,
        std::vector<std::size_t> target_columns,
        std::vector<std::size_t> assignment_columns,
        std::vector<ExpressionProgram> compiled_assignments,
        bool query_insert_identity_mapping,
        Source source,
        Tuple empty_input,
        ModifyTablePort& port
    );

    auto next_insert_values(ExecContext& context) -> base::Result<std::vector<types::Value>>;
    auto next_source_tuple(ExecContext& context) -> base::Result<std::optional<const Tuple*>>;
    auto execute_insert(ExecContext& context) -> base::Result<bool>;
    auto execute_update(const Tuple& tuple, ExecContext& context) -> base::Result<bool>;
    auto execute_delete(const Tuple& tuple) -> base::Result<bool>;
    auto store_mutation_result(std::optional<base::Rid> rid) -> base::Result<bool>;
    auto close_source(ExecContext& context) -> base::Status;

    Operation operation_;
    ModifyTarget target_;
    std::vector<std::size_t> target_columns_;
    std::vector<std::size_t> assignment_columns_;
    std::vector<ExpressionProgram> compiled_assignments_;
    bool query_insert_identity_mapping_{false};
    Source source_;
    Tuple empty_input_;
    ModifyTablePort* port_;
    std::unordered_set<base::Rid> seen_rids_;
    std::vector<types::Value> row_buffer_;
    std::size_t values_position_{0U};
    std::size_t affected_rows_{0U};
};

}  // namespace db::execution
