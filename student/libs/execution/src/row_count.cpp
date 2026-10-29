#include "db/execution/row_count.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::execution {

// LAB-S02.Q08:R01-BEGIN
namespace {

[[maybe_unused]] auto
row_count_error(const base::ErrorCode code, std::string message, const std::string_view operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "execution.row_count",
        std::string{operation}
    );
}

[[maybe_unused]] auto empty_input_tuple() -> base::Result<Tuple> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R01: create the row-independent evaluation tuple",
        }
    );
}

[[maybe_unused]] auto row_independent([[maybe_unused]] const planner::PlanExpression&) -> bool {
    base::lab::missing_algorithm("S02.Q08:R01");
}

[[maybe_unused]] auto compile_expression(
    [[maybe_unused]] const planner::PlanExpression*,
    [[maybe_unused]] const std::string_view
) -> base::Result<std::optional<ExpressionProgram>> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R01: compile a row-independent count expression",
        }
    );
}

[[maybe_unused]] auto resolve_count(
    [[maybe_unused]] const ExpressionProgram&,
    [[maybe_unused]] const Tuple&,
    [[maybe_unused]] const ExecContext&,
    [[maybe_unused]] const std::string_view
) -> base::Result<std::size_t> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R01: resolve a LIMIT/OFFSET count",
        }
    );
}

}  // namespace

[[maybe_unused]] auto RowCountSupport::compile(
    [[maybe_unused]] const planner::PlanExpression*,
    [[maybe_unused]] const planner::PlanExpression*
) -> base::Result<RowCountSupport> {
    return base::fail(row_count_error(
        base::ErrorCode::kUnsupported,
        "S02.Q08:R01: compile LIMIT/OFFSET expressions",
        "compile"
    ));
}

// LAB-S02.Q08:R01-END
RowCountSupport::RowCountSupport(RowCountSupport&& other) noexcept
    : input_{std::move(other.input_)},
      limit_{std::move(other.limit_)},
      offset_{std::move(other.offset_)} {
    other.limit_.reset();
    other.offset_.reset();
}

auto RowCountSupport::operator=(RowCountSupport&& other) noexcept -> RowCountSupport& {
    if (this != &other) {
        input_ = std::move(other.input_);
        limit_ = std::move(other.limit_);
        offset_ = std::move(other.offset_);
        other.limit_.reset();
        other.offset_.reset();
    }
    return *this;
}

// LAB-S02.Q08:R02-BEGIN
[[maybe_unused]] auto RowCountSupport::evaluate([[maybe_unused]] const ExecContext&) const
    -> base::Result<RowCountValues> {
    return base::fail(row_count_error(
        base::ErrorCode::kUnsupported,
        "S02.Q08:R02: evaluate LIMIT/OFFSET expressions",
        "evaluate"
    ));
}

// LAB-S02.Q08:R02-END
auto RowCountSupport::valid() const noexcept -> bool {
    return input_.descriptor() != nullptr;
}

auto RowCountSupport::has_limit() const noexcept -> bool {
    return valid() && limit_.has_value();
}

}  // namespace db::execution
