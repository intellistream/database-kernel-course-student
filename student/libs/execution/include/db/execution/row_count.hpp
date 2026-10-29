#pragma once

#include <cstddef>
#include <optional>
#include <utility>

#include "db/base/result.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/tuple.hpp"

namespace db::planner {

struct PlanExpression;

}  // namespace db::planner

namespace db::execution {

class ExecContext;

struct RowCountValues final {
    std::optional<std::size_t> limit;
    std::size_t offset{0U};
};

// Compiles row-independent LIMIT/OFFSET expressions without retaining the
// planner tree. Values are resolved for each open/rescan so executor-owned
// parameters can be rebound by a parent operator.
class RowCountSupport final {
public:
    RowCountSupport(const RowCountSupport&) = delete;
    auto operator=(const RowCountSupport&) -> RowCountSupport& = delete;
    RowCountSupport(RowCountSupport&& other) noexcept;
    auto operator=(RowCountSupport&& other) noexcept -> RowCountSupport&;
    ~RowCountSupport() = default;

    static auto compile(const planner::PlanExpression* limit, const planner::PlanExpression* offset)
        -> base::Result<RowCountSupport>;

    auto evaluate(const ExecContext& context) const -> base::Result<RowCountValues>;

    // Moving transfers the compiled programs and leaves the source inert.
    auto valid() const noexcept -> bool;
    auto has_limit() const noexcept -> bool;

private:
    RowCountSupport(
        Tuple input,
        std::optional<ExpressionProgram> limit,
        std::optional<ExpressionProgram> offset
    )
        : input_{std::move(input)},
          limit_{std::move(limit)},
          offset_{std::move(offset)} {}

    Tuple input_;
    std::optional<ExpressionProgram> limit_;
    std::optional<ExpressionProgram> offset_;
};

}  // namespace db::execution
