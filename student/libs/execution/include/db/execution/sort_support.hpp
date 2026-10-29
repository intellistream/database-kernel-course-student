#pragma once

#include <compare>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/result.hpp"
#include "db/execution/expression.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::execution {

using SortKeyValues = std::vector<types::Value>;

// Compiled ORDER BY expressions and their direction/NULL placement rules.
// The planner objects are read only during compile and are never retained.
class SortSupport final {
public:
    SortSupport(const SortSupport&) = delete;
    auto operator=(const SortSupport&) -> SortSupport& = delete;
    SortSupport(SortSupport&& other) noexcept;
    auto operator=(SortSupport&& other) noexcept -> SortSupport&;
    ~SortSupport() = default;

    static auto compile(std::span<const planner::PlanSortItem> order_by)
        -> base::Result<SortSupport>;

    auto evaluate(const Tuple& tuple, const ExecContext& context) const
        -> base::Result<SortKeyValues>;

    // Clears and refills caller-owned storage, retaining its capacity between
    // transient comparisons. On error the output is empty.
    auto evaluate_into(const Tuple& tuple, const ExecContext& context, SortKeyValues& output) const
        -> base::Status;

    // All key values have already passed evaluate()'s typed checks, so this
    // comparator is non-throwing and suitable for std::stable_sort.
    auto
    compare(std::span<const types::Value> left, std::span<const types::Value> right) const noexcept
        -> std::strong_ordering;

    // A valid compiled support always owns at least one key. Moving transfers
    // every key and leaves the source inert.
    auto valid() const noexcept -> bool;
    auto key_count() const noexcept -> std::size_t;

private:
    struct KeySpec final {
        using Evaluator = std::variant<std::size_t, ExpressionProgram>;

        Evaluator evaluator;
        types::DataType data_type{types::kUnknownType};
        bool descending{false};
        bool nulls_first{false};

        KeySpec(
            Evaluator key_evaluator,
            types::DataType key_data_type,
            bool is_descending,
            bool place_nulls_first
        )
            : evaluator{std::move(key_evaluator)},
              data_type{key_data_type},
              descending{is_descending},
              nulls_first{place_nulls_first} {}
    };

    explicit SortSupport(std::vector<KeySpec> keys) noexcept
        : keys_{std::move(keys)} {}

    std::vector<KeySpec> keys_;
};

}  // namespace db::execution
