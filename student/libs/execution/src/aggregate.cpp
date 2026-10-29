#include "db/execution/aggregate.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "state_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto aggregate_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.aggregate", "construct");
}

[[maybe_unused]] auto aggregate_runtime_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kExecution, std::move(message)}.with_context(
        "execution.aggregate",
        "run"
    );
}

[[maybe_unused]] auto is_numeric(const types::DataType type) noexcept -> bool {
    return type == types::kInt64Type || type == types::kFloat32Type;
}

[[maybe_unused]] auto aggregate_kind_name(const planner::PlanAggregateKind kind) noexcept
    -> std::string_view {
    switch (kind) {
        case planner::PlanAggregateKind::kCount:
            return "COUNT";
        case planner::PlanAggregateKind::kSum:
            return "SUM";
        case planner::PlanAggregateKind::kAverage:
            return "AVG";
        case planner::PlanAggregateKind::kMinimum:
            return "MIN";
        case planner::PlanAggregateKind::kMaximum:
            return "MAX";
    }
    return "aggregate";
}

// A small signed two-limb accumulator avoids implementation-defined compiler
// extensions while retaining the reference executor's i128 intermediate.
struct SignedWide final {
    std::uint64_t high{0U};
    std::uint64_t low{0U};

    // LAB-S05.Q01:R01-BEGIN
    [[maybe_unused]] auto add([[maybe_unused]] const std::int64_t) noexcept -> bool {
        base::lab::missing_algorithm("S05.Q01:R01");
    }

    [[maybe_unused]] auto fits_int64() const noexcept -> bool {
        base::lab::missing_algorithm("S05.Q01:R01");
    }

    [[maybe_unused]] auto as_int64() const noexcept -> std::int64_t {
        base::lab::missing_algorithm("S05.Q01:R01");
    }

    [[maybe_unused]] auto as_long_double() const noexcept -> long double {
        base::lab::missing_algorithm("S05.Q01:R01");
    }

    // LAB-S05.Q01:R01-END
};

struct FloatSum final {
    double value{0.0};

    // LAB-S05.Q01:R02-BEGIN
    [[maybe_unused]] void add([[maybe_unused]] const float) noexcept {
        base::lab::missing_algorithm("S05.Q01:R02");
    }

    // LAB-S05.Q01:R02-END
};

// LAB-S05.Q01:R03-BEGIN
[[maybe_unused]] auto float_total_key([[maybe_unused]] const float) noexcept -> std::uint32_t {
    base::lab::missing_algorithm("S05.Q01:R03");
}

// LAB-S05.Q01:R03-END

// LAB-S05.Q01:R04-BEGIN
[[maybe_unused]] auto
compare_values([[maybe_unused]] const types::Value&, [[maybe_unused]] const types::Value&)
    -> base::Result<std::strong_ordering> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q01:R04: compare aggregate MIN/MAX values",
        }
    );
}

// LAB-S05.Q01:R04-END

[[maybe_unused]] auto validate_output_schema(const planner::AggregatePlan& plan) -> base::Status {
    for (std::size_t index = 0U; index < plan.group_by.size(); ++index) {
        const auto& expression = plan.group_by.at(index);
        const auto& output = plan.metadata.output.columns.at(index);
        if (expression == nullptr || expression->data_type != output.data_type) {
            return base::fail(aggregate_error(
                base::ErrorCode::kPlan,
                "aggregate group output type differs from its expression"
            ));
        }
    }
    for (std::size_t index = 0U; index < plan.aggregates.size(); ++index) {
        const auto& aggregate = plan.aggregates.at(index);
        const auto& output = plan.metadata.output.columns.at(plan.group_by.size() + index);
        if (aggregate.output_name.empty() || aggregate.output_name != output.name
            || aggregate.data_type != output.data_type) {
            return base::fail(aggregate_error(
                base::ErrorCode::kPlan,
                "aggregate output metadata differs from its schema"
            ));
        }
    }
    return {};
}

}  // namespace

struct AggregateState::Accumulator final {
    planner::PlanAggregateKind kind{planner::PlanAggregateKind::kCount};
    bool distinct{false};
    bool star{false};
    types::DataType result_type{types::kUnknownType};
    std::unordered_set<types::Value, ValueHash> seen;
    std::int64_t count{0};
    SignedWide integer_sum;
    FloatSum float_sum;
    float average_float_sum{0.0F};
    std::optional<types::Value> extreme;
    bool has_value{false};

    explicit Accumulator(const AggregateSpec& spec)
        : kind{spec.kind},
          distinct{spec.distinct},
          star{spec.star},
          result_type{spec.result_type} {}

    // LAB-S05.Q03:R02-BEGIN
    [[maybe_unused]] auto accept_distinct([[maybe_unused]] const types::Value&) -> bool {
        base::lab::missing_algorithm("S05.Q03:R02");
    }

    // LAB-S05.Q03:R02-END

    // LAB-S05.Q01:R05-BEGIN
    [[maybe_unused]] auto add_count() -> base::Status {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "S05.Q01:R05: advance a COUNT accumulator",
            }
        );
    }

    // LAB-S05.Q01:R05-END

    // LAB-S05.Q01:R06-BEGIN
    [[maybe_unused]] auto add_numeric([[maybe_unused]] const types::Value&) -> base::Status {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "S05.Q01:R06: advance SUM/AVG accumulator state",
            }
        );
    }

    // LAB-S05.Q01:R06-END

    // LAB-S05.Q01:R07-BEGIN
    [[maybe_unused]] auto add_extreme([[maybe_unused]] const types::Value&) -> base::Status {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "S05.Q01:R07: advance MIN/MAX accumulator state",
            }
        );
    }

    // LAB-S05.Q01:R07-END

    // LAB-S05.Q01:R08-BEGIN
    [[maybe_unused]] auto add([[maybe_unused]] const types::Value&) -> base::Status {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "S05.Q01:R08: apply a scalar aggregate input value",
            }
        );
    }

    // LAB-S05.Q01:R08-END

    // LAB-S05.Q01:R09-BEGIN
    [[maybe_unused]] auto finish() const -> base::Result<types::Value> {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "S05.Q01:R09: finish scalar aggregate state",
            }
        );
    }

    // LAB-S05.Q01:R09-END
};

AggregateState::~AggregateState() = default;

// LAB-S05.Q03:R01-BEGIN
[[maybe_unused]] auto
AggregateState::ValueHash::operator()([[maybe_unused]] const types::Value&) const noexcept
    -> std::size_t {
    base::lab::missing_algorithm("S05.Q03:R01");
}

// LAB-S05.Q03:R01-END

// LAB-S05.Q02:R01-BEGIN
[[maybe_unused]] auto AggregateState::GroupKeyHash::operator()(
    [[maybe_unused]] const std::vector<types::Value>&
) const noexcept -> std::size_t {
    base::lab::missing_algorithm("S05.Q02:R01");
}

// LAB-S05.Q02:R01-END

AggregateState::AggregateState(
    TupleSlot output_slot,
    std::unique_ptr<PlanState> input,
    std::vector<ExpressionProgram> group_by,
    std::vector<AggregateSpec> aggregates,
    std::optional<ExpressionProgram> having,
    const bool ordered_min
)
    : PlanState{std::move(output_slot)},
      input_{std::move(input)},
      group_by_{std::move(group_by)},
      aggregates_{std::move(aggregates)},
      having_{std::move(having)},
      ordered_min_{ordered_min} {}

auto AggregateState::create(const planner::AggregatePlan& plan, std::unique_ptr<PlanState> input)
    -> base::Result<std::unique_ptr<AggregateState>> {
    if (input == nullptr) {
        return base::fail(aggregate_error(base::ErrorCode::kPlan, "aggregate input is absent"));
    }
    if (plan.metadata.output.columns.size() != plan.group_by.size() + plan.aggregates.size()) {
        return base::fail(aggregate_error(
            base::ErrorCode::kPlan,
            "aggregate output width differs from groups and aggregates"
        ));
    }
    auto output_schema = validate_output_schema(plan);
    if (!output_schema) {
        return base::fail(std::move(output_schema).error());
    }
    auto slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    auto group_by = compile_group_by(plan);
    if (!group_by) {
        return base::fail(std::move(group_by).error());
    }
    std::vector<AggregateSpec> aggregates;
    aggregates.reserve(plan.aggregates.size());
    for (const planner::PlanAggregate& aggregate : plan.aggregates) {
        auto compiled = compile_aggregate(aggregate);
        if (!compiled) {
            return base::fail(std::move(compiled).error());
        }
        aggregates.push_back(std::move(compiled).value());
    }
    auto having = compile_having(plan);
    if (!having) {
        return base::fail(std::move(having).error());
    }
    return std::unique_ptr<AggregateState>{new AggregateState{
        std::move(slot).value(),
        std::move(input),
        std::move(group_by).value(),
        std::move(aggregates),
        std::move(having).value(),
        plan.ordered_min,
    }};
}

auto AggregateState::compile_group_by(const planner::AggregatePlan& plan)
    -> base::Result<std::vector<ExpressionProgram>> {
    std::vector<ExpressionProgram> result;
    result.reserve(plan.group_by.size());
    for (const auto& expression : plan.group_by) {
        if (expression == nullptr) {
            return base::fail(aggregate_error(base::ErrorCode::kPlan, "group key is absent"));
        }
        auto compiled = ExpressionProgram::compile(*expression);
        if (!compiled) {
            return base::fail(
                std::move(compiled).error().with_context("execution.aggregate", "compile group key")
            );
        }
        result.push_back(std::move(compiled).value());
    }
    return result;
}

auto AggregateState::compile_aggregate(const planner::PlanAggregate& aggregate)
    -> base::Result<AggregateState::AggregateSpec> {
    if (aggregate.star
        && (aggregate.kind != planner::PlanAggregateKind::kCount || aggregate.distinct
            || !aggregate.arguments.empty())) {
        return base::fail(
            aggregate_error(base::ErrorCode::kPlan, "only COUNT(*) may be a star aggregate")
        );
    }
    if (!aggregate.star
        && (aggregate.arguments.size() != 1U || aggregate.arguments.front() == nullptr)) {
        return base::fail(aggregate_error(
            base::ErrorCode::kPlan,
            std::string{aggregate_kind_name(aggregate.kind)} + " requires one argument"
        ));
    }
    const types::DataType input_type =
        aggregate.star ? types::kUnknownType : aggregate.arguments.front()->data_type;
    if (aggregate.kind == planner::PlanAggregateKind::kCount
        && aggregate.data_type != types::kInt64Type) {
        return base::fail(aggregate_error(base::ErrorCode::kPlan, "COUNT output type must be INT"));
    }
    if ((aggregate.kind == planner::PlanAggregateKind::kSum
         || aggregate.kind == planner::PlanAggregateKind::kAverage)
        && !is_numeric(input_type)) {
        return base::fail(aggregate_error(
            base::ErrorCode::kPlan,
            std::string{aggregate_kind_name(aggregate.kind)} + " requires a numeric argument"
        ));
    }
    if (aggregate.kind == planner::PlanAggregateKind::kAverage
        && aggregate.data_type != types::kFloat32Type) {
        return base::fail(aggregate_error(base::ErrorCode::kPlan, "AVG output type must be FLOAT"));
    }
    if ((aggregate.kind == planner::PlanAggregateKind::kSum
         || aggregate.kind == planner::PlanAggregateKind::kMinimum
         || aggregate.kind == planner::PlanAggregateKind::kMaximum)
        && aggregate.data_type != input_type) {
        return base::fail(aggregate_error(
            base::ErrorCode::kPlan,
            std::string{aggregate_kind_name(aggregate.kind)}
                + " output type differs from its argument"
        ));
    }
    std::optional<ExpressionProgram> argument;
    if (!aggregate.star) {
        auto compiled = ExpressionProgram::compile(*aggregate.arguments.front());
        if (!compiled) {
            return base::fail(
                std::move(compiled).error().with_context(
                    "execution.aggregate",
                    "compile aggregate argument"
                )
            );
        }
        argument = std::move(compiled).value();
    }
    return AggregateSpec{
        .kind = aggregate.kind,
        .distinct = aggregate.distinct,
        .star = aggregate.star,
        .input_type = input_type,
        .result_type = aggregate.data_type,
        .argument = std::move(argument),
    };
}

auto AggregateState::compile_having(const planner::AggregatePlan& plan)
    -> base::Result<std::optional<ExpressionProgram>> {
    if (plan.having == nullptr) {
        return std::optional<ExpressionProgram>{};
    }
    if (plan.having->data_type != types::kBoolType) {
        return base::fail(aggregate_error(base::ErrorCode::kPlan, "aggregate HAVING is not BOOL"));
    }
    auto compiled = ExpressionProgram::compile(*plan.having);
    if (!compiled) {
        return base::fail(
            std::move(compiled).error().with_context("execution.aggregate", "compile HAVING")
        );
    }
    return std::optional<ExpressionProgram>{std::move(compiled).value()};
}

// LAB-S05.Q05:R01-BEGIN
[[maybe_unused]] auto AggregateState::do_open([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q05:R01: initialize aggregate execution state",
        }
    );
}

// LAB-S05.Q05:R01-END

// LAB-S05.Q02:R02-BEGIN
[[maybe_unused]] auto AggregateState::find_or_create_group(
    [[maybe_unused]] std::vector<Group>&,
    [[maybe_unused]] GroupIndex&,
    [[maybe_unused]] const std::vector<types::Value>&
) const -> base::Result<AggregateState::Group*> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q02:R02: find or create a hash aggregate group",
        }
    );
}

// LAB-S05.Q02:R02-END

// LAB-S05.Q02:R03-BEGIN
[[maybe_unused]] auto
AggregateState::make_group([[maybe_unused]] const std::vector<types::Value>&) const
    -> base::Result<AggregateState::Group> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q02:R03: create hash aggregate group state",
        }
    );
}

// LAB-S05.Q02:R03-END

// LAB-S05.Q02:R04-BEGIN
[[maybe_unused]] auto AggregateState::evaluate_group_key(
    [[maybe_unused]] const Tuple&,
    [[maybe_unused]] const ExecContext&,
    [[maybe_unused]] std::vector<types::Value>& key
) const -> base::Status {
    key.clear();
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q02:R04: evaluate a composite hash group key",
        }
    );
}

// LAB-S05.Q02:R04-END

// LAB-S05.Q05:R02-BEGIN
[[maybe_unused]] auto AggregateState::advance_group(
    [[maybe_unused]] Group&,
    [[maybe_unused]] const Tuple&,
    [[maybe_unused]] ExecContext&
) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q05:R02: coordinate aggregate arguments and accumulators",
        }
    );
}

// LAB-S05.Q05:R02-END

// LAB-S05.Q05:R03-BEGIN
[[maybe_unused]] auto
AggregateState::ordered_min_complete([[maybe_unused]] const Group&) const noexcept -> bool {
    base::lab::missing_algorithm("S05.Q05:R03");
}

// LAB-S05.Q05:R03-END

// LAB-S05.Q05:R04-BEGIN
[[maybe_unused]] auto AggregateState::finish_groups(
    [[maybe_unused]] const std::vector<Group>&,
    [[maybe_unused]] ExecContext&
) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q05:R04: finish aggregate groups and materialize output",
        }
    );
}

// LAB-S05.Q05:R04-END

// LAB-S05.Q05:R05-BEGIN
[[maybe_unused]] auto AggregateState::passes_having(
    [[maybe_unused]] const std::vector<types::Value>&,
    [[maybe_unused]] const ExecContext&
) const -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q05:R05: evaluate aggregate HAVING qualification",
        }
    );
}

// LAB-S05.Q05:R05-END

// LAB-S05.Q05:R06-BEGIN
[[maybe_unused]] auto AggregateState::compute([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q05:R06: consume input rows and coordinate aggregate groups",
        }
    );
}

// LAB-S05.Q05:R06-END

// LAB-S05.Q05:R07-BEGIN
[[maybe_unused]] auto AggregateState::do_next([[maybe_unused]] ExecContext&) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q05:R07: publish aggregate output rows",
        }
    );
}

// LAB-S05.Q05:R07-END

// LAB-S05.Q05:R08-BEGIN
[[maybe_unused]] auto AggregateState::do_rescan([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S05.Q05:R08: reset aggregate execution state",
        }
    );
}

// LAB-S05.Q05:R08-END

auto AggregateState::do_close(ExecContext& context) -> base::Status {
    output_rows_.clear();
    next_output_ = 0U;
    computed_ = false;
    return input_->close(context);
}

}  // namespace db::execution
