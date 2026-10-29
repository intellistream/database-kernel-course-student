#include "db/execution/merge_join.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/value.hpp"

#include "join_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto merge_join_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.merge_join", "construct");
}

// LAB-S07.Q04:R01-BEGIN
[[maybe_unused]] auto partial_to_strong([[maybe_unused]] const std::partial_ordering) noexcept
    -> std::strong_ordering {
    base::lab::missing_algorithm("S07.Q04:R01");
}

// LAB-S07.Q04:R01-END

}  // namespace

MergeJoinState::MergeJoinState(
    TupleSlot output_slot,
    std::unique_ptr<PlanState> outer,
    std::unique_ptr<PlanState> inner,
    std::vector<std::size_t> outer_columns,
    std::vector<std::size_t> inner_columns,
    std::optional<ExpressionProgram> residual_filter
)
    : PlanState{std::move(output_slot)},
      outer_{std::move(outer)},
      inner_{std::move(inner)},
      outer_columns_{std::move(outer_columns)},
      inner_columns_{std::move(inner_columns)},
      residual_filter_{std::move(residual_filter)} {}

auto MergeJoinState::create(
    const planner::MergeJoinPlan& plan,
    std::unique_ptr<PlanState> outer,
    std::unique_ptr<PlanState> inner
) -> base::Result<std::unique_ptr<MergeJoinState>> {
    if (outer == nullptr || inner == nullptr) {
        return base::fail(
            merge_join_error(base::ErrorCode::kPlan, "merge-join child input is absent")
        );
    }
    if (plan.join_columns.empty()) {
        return base::fail(merge_join_error(
            base::ErrorCode::kPlan,
            "merge join requires at least one equality column"
        ));
    }

    auto slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    const TupleDescriptorHandle outer_descriptor = outer->output_slot().descriptor();
    const TupleDescriptorHandle inner_descriptor = inner->output_slot().descriptor();
    auto shape = detail::validate_join_output_shape(
        outer_descriptor,
        inner_descriptor,
        slot->descriptor(),
        "merge join"
    );
    if (!shape) {
        return base::fail(std::move(shape).error());
    }

    const auto outer_columns = outer_descriptor->columns();
    const auto inner_columns = inner_descriptor->columns();
    std::vector<std::size_t> outer_indices;
    std::vector<std::size_t> inner_indices;
    outer_indices.reserve(plan.join_columns.size());
    inner_indices.reserve(plan.join_columns.size());
    for (const planner::PlanJoinColumnPair& pair : plan.join_columns) {
        const auto outer_index = static_cast<std::size_t>(pair.outer_column.zero_based());
        const auto inner_index = static_cast<std::size_t>(pair.inner_column.zero_based());
        if (outer_index >= outer_columns.size() || inner_index >= inner_columns.size()) {
            return base::fail(merge_join_error(
                base::ErrorCode::kPlan,
                "merge-join key column is outside a child schema"
            ));
        }
        if (outer_columns[outer_index].data_type != inner_columns[inner_index].data_type) {
            return base::fail(
                merge_join_error(base::ErrorCode::kPlan, "merge-join key column types differ")
            );
        }
        outer_indices.push_back(outer_index);
        inner_indices.push_back(inner_index);
    }

    std::optional<ExpressionProgram> residual_filter;
    if (plan.residual_filter != nullptr) {
        if (plan.residual_filter->data_type != types::kBoolType) {
            return base::fail(
                merge_join_error(base::ErrorCode::kPlan, "merge-join residual filter is not BOOL")
            );
        }
        auto compiled = ExpressionProgram::compile(*plan.residual_filter);
        if (!compiled) {
            return base::fail(std::move(compiled).error());
        }
        residual_filter = std::move(compiled).value();
    }

    return std::unique_ptr<MergeJoinState>{new MergeJoinState{
        std::move(slot).value(),
        std::move(outer),
        std::move(inner),
        std::move(outer_indices),
        std::move(inner_indices),
        std::move(residual_filter),
    }};
}

// LAB-S07.Q04:R02-BEGIN
[[maybe_unused]] auto MergeJoinState::next_outer([[maybe_unused]] ExecContext&)
    -> base::Result<std::optional<MergeJoinState::Row>> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R02: fetch the next ordered outer row",
        }
    );
}

// LAB-S07.Q04:R02-END

// LAB-S07.Q04:R03-BEGIN
[[maybe_unused]] auto MergeJoinState::next_inner([[maybe_unused]] ExecContext&)
    -> base::Result<std::optional<MergeJoinState::Row>> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R03: fetch the next ordered inner row",
        }
    );
}

// LAB-S07.Q04:R03-END

// LAB-S07.Q04:R04-BEGIN
[[maybe_unused]] auto MergeJoinState::compare_values(
    [[maybe_unused]] const types::Value&,
    [[maybe_unused]] const types::Value&
) const -> base::Result<std::strong_ordering> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R04: compare merge-join key values",
        }
    );
}

// LAB-S07.Q04:R04-END

// LAB-S07.Q04:R05-BEGIN
[[maybe_unused]] auto MergeJoinState::compare_keys(
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] const std::span<const types::Value>
) const -> base::Result<std::strong_ordering> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R05: compare composite merge-join keys",
        }
    );
}

// LAB-S07.Q04:R05-END

// LAB-S07.Q04:R06-BEGIN
[[maybe_unused]] auto MergeJoinState::key_contains_null(
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] const std::span<const std::size_t>
) const noexcept -> bool {
    base::lab::missing_algorithm("S07.Q04:R06");
}

// LAB-S07.Q04:R06-END

// LAB-S07.Q04:R07-BEGIN
[[maybe_unused]] auto MergeJoinState::build_inner_group([[maybe_unused]] ExecContext&)
    -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R07: retain an equal-key inner merge group",
        }
    );
}

// LAB-S07.Q04:R07-END

// LAB-S07.Q04:R08-BEGIN
[[maybe_unused]] auto MergeJoinState::prepare_group([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R08: advance ordered merge inputs to a key group",
        }
    );
}

// LAB-S07.Q04:R08-END

// LAB-S07.Q04:R09-BEGIN
[[maybe_unused]] auto MergeJoinState::emit_group_row(
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] ExecContext&
) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R09: qualify and emit a merge-group row",
        }
    );
}

// LAB-S07.Q04:R09-END

// LAB-S07.Q04:R10-BEGIN
[[maybe_unused]] auto MergeJoinState::process_group([[maybe_unused]] ExecContext&)
    -> base::Result<MergeJoinState::GroupStep> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R10: replay an equal-key inner merge group",
        }
    );
}

// LAB-S07.Q04:R10-END

// LAB-S07.Q04:R11-BEGIN
[[maybe_unused]] auto MergeJoinState::do_open([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R11: initialize ordered merge state",
        }
    );
}

// LAB-S07.Q04:R11-END

// LAB-S07.Q04:R12-BEGIN
[[maybe_unused]] auto MergeJoinState::do_next([[maybe_unused]] ExecContext&) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R12: emit merge-join results",
        }
    );
}

// LAB-S07.Q04:R12-END

// LAB-S07.Q04:R13-BEGIN
[[maybe_unused]] auto MergeJoinState::do_rescan([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q04:R13: reset ordered merge state",
        }
    );
}

// LAB-S07.Q04:R13-END

auto MergeJoinState::do_close(ExecContext& context) -> base::Status {
    outer_row_.reset();
    inner_row_.reset();
    inner_group_.clear();
    inner_group_pos_ = 0U;
    done_ = true;
    auto inner_closed = inner_->close(context);
    auto outer_closed = outer_->close(context);
    if (!inner_closed) {
        return base::fail(std::move(inner_closed).error());
    }
    if (!outer_closed) {
        return base::fail(std::move(outer_closed).error());
    }
    return {};
}

}  // namespace db::execution
