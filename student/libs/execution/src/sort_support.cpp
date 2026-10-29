#include "db/execution/sort_support.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/execution/tuple.hpp"
#include "db/types/type_registry.hpp"

namespace db::execution {

// LAB-S02.Q08:R03-BEGIN
namespace {

[[maybe_unused]] auto sort_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.sort", "compile");
}

[[maybe_unused]] auto evaluate_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kExecution, std::move(message)}.with_context(
        "execution.sort",
        "evaluate"
    );
}

[[maybe_unused]] auto inactive_error() -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, "sort support has been moved from"}
        .with_context("execution.sort", "evaluate");
}

[[maybe_unused]] auto valid_direction([[maybe_unused]] const planner::PlanSortDirection) noexcept
    -> bool {
    base::lab::missing_algorithm("S02.Q08:R03");
}

[[maybe_unused]] auto valid_null_order([[maybe_unused]] const planner::PlanNullOrder) noexcept
    -> bool {
    base::lab::missing_algorithm("S02.Q08:R03");
}

[[maybe_unused]] auto
compare_float_total([[maybe_unused]] const float, [[maybe_unused]] const float) noexcept
    -> std::strong_ordering {
    base::lab::missing_algorithm("S02.Q08:R03");
}

[[maybe_unused]] auto compare_non_null(
    [[maybe_unused]] const types::Value&,
    [[maybe_unused]] const types::Value&
) noexcept -> std::strong_ordering {
    base::lab::missing_algorithm("S02.Q08:R03");
}

[[maybe_unused]] auto reverse([[maybe_unused]] const std::strong_ordering) noexcept
    -> std::strong_ordering {
    base::lab::missing_algorithm("S02.Q08:R03");
}

[[maybe_unused]] auto compare_sort_values(
    [[maybe_unused]] const types::Value&,
    [[maybe_unused]] const types::Value&,
    [[maybe_unused]] const bool,
    [[maybe_unused]] const bool
) noexcept -> std::strong_ordering {
    base::lab::missing_algorithm("S02.Q08:R03");
}

}  // namespace

[[maybe_unused]] auto
SortSupport::compile([[maybe_unused]] const std::span<const planner::PlanSortItem>)
    -> base::Result<SortSupport> {
    return base::fail(
        sort_error(base::ErrorCode::kUnsupported, "S02.Q08:R03: compile ORDER BY key expressions")
    );
}

// LAB-S02.Q08:R03-END
SortSupport::SortSupport(SortSupport&& other) noexcept
    : keys_{std::move(other.keys_)} {
    other.keys_.clear();
}

auto SortSupport::operator=(SortSupport&& other) noexcept -> SortSupport& {
    if (this != &other) {
        keys_ = std::move(other.keys_);
        other.keys_.clear();
    }
    return *this;
}

// LAB-S02.Q08:R04-BEGIN
[[maybe_unused]] auto
SortSupport::evaluate([[maybe_unused]] const Tuple&, [[maybe_unused]] const ExecContext&) const
    -> base::Result<SortKeyValues> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R04: evaluate ORDER BY key expressions",
        }
    );
}

// LAB-S02.Q08:R04-END
// LAB-S02.Q08:R05-BEGIN
[[maybe_unused]] auto SortSupport::evaluate_into(
    [[maybe_unused]] const Tuple&,
    [[maybe_unused]] const ExecContext&,
    [[maybe_unused]] SortKeyValues& output
) const -> base::Status {
    output.clear();
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R05: evaluate ORDER BY keys into reusable storage",
        }
    );
}

// LAB-S02.Q08:R05-END
// LAB-S02.Q08:R06-BEGIN
[[maybe_unused]] auto SortSupport::compare(
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] const std::span<const types::Value>
) const noexcept -> std::strong_ordering {
    base::lab::missing_algorithm("S02.Q08:R06");
}

// LAB-S02.Q08:R06-END
auto SortSupport::valid() const noexcept -> bool {
    return !keys_.empty();
}

auto SortSupport::key_count() const noexcept -> std::size_t {
    return keys_.size();
}

}  // namespace db::execution
