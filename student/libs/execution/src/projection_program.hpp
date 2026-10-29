#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "db/base/result.hpp"
#include "db/execution/exec_context.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

namespace db::execution {

// Executor-private equivalent of PostgreSQL's ProjectionInfo. A target list is
// compiled once into direct input-column assignments and expression programs;
// scan and Result states share the same ordered evaluator.
class ProjectionProgram final {
public:
    static auto compile(const planner::ResultPlan& plan, std::span<const TupleColumn> input_columns)
        -> base::Result<std::unique_ptr<ProjectionProgram>>;

    auto identity() const noexcept -> bool;
    auto evaluate(
        const Tuple& input,
        const ExecContext& context,
        std::vector<types::Value> recycled_values
    ) const -> base::Result<std::vector<types::Value>>;
    auto evaluate_movable(
        Tuple& input,
        const ExecContext& context,
        std::vector<types::Value> recycled_values
    ) const -> base::Result<std::vector<types::Value>>;

private:
    enum class Opcode : std::uint8_t {
        kCopyInput,
        kMoveInput,
        kEvaluate,
    };

    struct Step final {
        Opcode opcode{Opcode::kEvaluate};
        std::size_t operand{0U};
    };

    ProjectionProgram(
        std::vector<Step> steps,
        std::vector<ExpressionProgram> programs,
        bool identity
    );
    auto evaluate_impl(
        const Tuple& input,
        std::vector<types::Value>* movable_input_values,
        const ExecContext& context,
        std::vector<types::Value> recycled_values
    ) const -> base::Result<std::vector<types::Value>>;

    std::vector<Step> steps_;
    std::vector<ExpressionProgram> programs_;
    bool identity_{false};
};

}  // namespace db::execution
