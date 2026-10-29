#include "db/execution/hash_join.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "join_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto hash_join_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.hash_join", "construct");
}

}  // namespace

// LAB-S07.Q03:R01-BEGIN
[[maybe_unused]] auto
HashJoinState::JoinKeyHash::operator()([[maybe_unused]] const JoinKey&) const noexcept
    -> std::size_t {
    base::lab::missing_algorithm("S07.Q03:R01");
}

// LAB-S07.Q03:R01-END

HashJoinState::HashJoinState(
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

auto HashJoinState::create(
    const planner::HashJoinPlan& plan,
    std::unique_ptr<PlanState> outer,
    std::unique_ptr<PlanState> inner
) -> base::Result<std::unique_ptr<HashJoinState>> {
    if (outer == nullptr || inner == nullptr) {
        return base::fail(
            hash_join_error(base::ErrorCode::kPlan, "hash-join child input is absent")
        );
    }
    if (plan.join_columns.empty()) {
        return base::fail(hash_join_error(
            base::ErrorCode::kPlan,
            "hash join requires at least one equality column"
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
        "hash join"
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
            return base::fail(hash_join_error(
                base::ErrorCode::kPlan,
                "hash-join key column is outside a child schema"
            ));
        }
        if (outer_columns[outer_index].data_type != inner_columns[inner_index].data_type) {
            return base::fail(
                hash_join_error(base::ErrorCode::kPlan, "hash-join key column types differ")
            );
        }
        outer_indices.push_back(outer_index);
        inner_indices.push_back(inner_index);
    }

    std::optional<ExpressionProgram> residual_filter;
    if (plan.residual_filter != nullptr) {
        if (plan.residual_filter->data_type != types::kBoolType) {
            return base::fail(
                hash_join_error(base::ErrorCode::kPlan, "hash-join residual filter is not BOOL")
            );
        }
        auto compiled = ExpressionProgram::compile(*plan.residual_filter);
        if (!compiled) {
            return base::fail(std::move(compiled).error());
        }
        residual_filter = std::move(compiled).value();
    }

    return std::unique_ptr<HashJoinState>{new HashJoinState{
        std::move(slot).value(),
        std::move(outer),
        std::move(inner),
        std::move(outer_indices),
        std::move(inner_indices),
        std::move(residual_filter),
    }};
}

// LAB-S07.Q03:R02-BEGIN
[[maybe_unused]] auto HashJoinState::make_key(
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] const std::span<const std::size_t>
) const -> std::optional<HashJoinState::JoinKey> {
    base::lab::missing_algorithm("S07.Q03:R02");
}

// LAB-S07.Q03:R02-END

// LAB-S07.Q03:R03-BEGIN
[[maybe_unused]] auto HashJoinState::next_outer_tuple([[maybe_unused]] ExecContext&)
    -> base::Result<std::optional<Tuple>> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q03:R03: fetch the next hash-join probe row",
        }
    );
}

// LAB-S07.Q03:R03-END

// LAB-S07.Q03:R04-BEGIN
[[maybe_unused]] auto HashJoinState::append_match(
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] ExecContext&
) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q03:R04: materialize and qualify a hash-join match",
        }
    );
}

// LAB-S07.Q03:R04-END

// LAB-S07.Q03:R05-BEGIN
[[maybe_unused]] auto HashJoinState::append_matches(
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] const std::vector<Tuple>&,
    [[maybe_unused]] ExecContext&
) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q03:R05: expand duplicate hash-join matches",
        }
    );
}

// LAB-S07.Q03:R05-END

// LAB-S07.Q03:R06-BEGIN
[[maybe_unused]] auto HashJoinState::build_inner([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q03:R06: build hash-join buckets from the inner input",
        }
    );
}

// LAB-S07.Q03:R06-END

// LAB-S07.Q03:R07-BEGIN
[[maybe_unused]] auto HashJoinState::fill_pending_from_outer([[maybe_unused]] ExecContext&)
    -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q03:R07: probe hash-join buckets from the outer input",
        }
    );
}

// LAB-S07.Q03:R07-END

// LAB-S07.Q03:R08-BEGIN
[[maybe_unused]] auto HashJoinState::do_open([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q03:R08: initialize hash-join build and probe state",
        }
    );
}

// LAB-S07.Q03:R08-END

// LAB-S07.Q03:R09-BEGIN
[[maybe_unused]] auto HashJoinState::do_next([[maybe_unused]] ExecContext&) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q03:R09: emit hash-join results",
        }
    );
}

// LAB-S07.Q03:R09-END

// LAB-S07.Q03:R10-BEGIN
[[maybe_unused]] auto HashJoinState::do_rescan([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q03:R10: reset hash-join build and probe state",
        }
    );
}

// LAB-S07.Q03:R10-END

auto HashJoinState::do_close(ExecContext& context) -> base::Status {
    table_.clear();
    pending_.clear();
    pending_pos_ = 0U;
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
