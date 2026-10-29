#pragma once

#include <memory>
#include <span>

#include "db/base/result.hpp"
#include "db/types/value.hpp"

namespace db::planner {

struct PlanExpression;

}  // namespace db::planner

namespace db::execution {

class ExecContext;
class Tuple;

// A compiled evaluator for one planner-owned expression. Compilation copies
// literals and parameter references into an immutable postfix instruction
// stream, so prepared plans do not retain an AST pointer. Simple qualification
// shapes borrow their leaf values directly from that same checked program;
// general value evaluation uses its reusable stack. Sequential evaluation caches
// the immutable tuple and parameter descriptor identities it has accepted; one
// program may therefore be reused sequentially, but not called concurrently.
class ExpressionProgram final {
public:
    static auto compile(const planner::PlanExpression& expression)
        -> base::Result<ExpressionProgram>;

    // Retains the immutable bytecode independently so a consumer may keep a
    // qualification beyond a plan state's lifetime without borrowing it.
    auto clone() const -> base::Result<ExpressionProgram>;

    ExpressionProgram(const ExpressionProgram&) = delete;
    auto operator=(const ExpressionProgram&) -> ExpressionProgram& = delete;
    ExpressionProgram(ExpressionProgram&& other) noexcept;
    auto operator=(ExpressionProgram&& other) noexcept -> ExpressionProgram&;
    ~ExpressionProgram();

    // False only for the inert source left by a move. Work operations on an
    // inert program return kUnavailable and never dereference the empty PImpl.
    auto valid() const noexcept -> bool;

    auto evaluate(const Tuple& tuple, const ExecContext& context) const
        -> base::Result<types::Value>;

    auto evaluate(
        const Tuple& tuple,
        const ExecContext& context,
        std::span<const types::Value> subplan_values
    ) const -> base::Result<types::Value>;

    // SQL WHERE/JOIN/HAVING semantics: only TRUE qualifies. FALSE and UNKNOWN
    // (typed NULL) both reject the row; non-boolean expressions are errors.
    auto evaluate_qualification(const Tuple& tuple, const ExecContext& context) const
        -> base::Result<bool>;

    auto evaluate_qualification(
        const Tuple& tuple,
        const ExecContext& context,
        std::span<const types::Value> subplan_values
    ) const -> base::Result<bool>;

private:
    struct Impl;

    explicit ExpressionProgram(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::execution
