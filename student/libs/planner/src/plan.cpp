#include "db/planner/plan.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/type_registry.hpp"

namespace db::planner {
namespace {

[[maybe_unused]] auto plan_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

[[maybe_unused]] auto parameter_namespace_unavailable(const std::string_view operation)
    -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, "parameter namespace has been moved from"}
        .with_context("planner.parameter_namespace", std::string{operation});
}

[[maybe_unused]] auto valid_type(const types::DataType data_type) -> bool {
    return types::validate_data_type(data_type).has_value();
}

[[maybe_unused]] auto checked_parameter_count(const std::size_t count)
    -> base::Result<std::uint32_t> {
    constexpr std::size_t k_maximum_parameters =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (count > k_maximum_parameters) {
        return base::fail(plan_error("parameter namespace exceeds 32-bit capacity"));
    }
    return static_cast<std::uint32_t>(count);
}

[[maybe_unused]] auto reference_from(const PlanParameter& parameter) -> PlanParameterReference {
    return PlanParameterReference{
        .id = parameter.id,
        .kind = parameter.kind,
        .data_type = parameter.data_type,
    };
}

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

}  // namespace

ParameterNamespace::ParameterNamespace(ParameterNamespace&& other) noexcept
    : parameters_{std::move(other.parameters_)},
      external_count_{other.external_count_},
      active_{other.active_} {
    other.parameters_.clear();
    other.external_count_ = 0U;
    other.active_ = false;
}

auto ParameterNamespace::operator=(ParameterNamespace&& other) noexcept -> ParameterNamespace& {
    if (this != &other) {
        parameters_ = std::move(other.parameters_);
        external_count_ = other.external_count_;
        active_ = other.active_;
        other.parameters_.clear();
        other.external_count_ = 0U;
        other.active_ = false;
    }
    return *this;
}

auto ParameterNamespace::valid() const noexcept -> bool {
    return active_;
}

auto ParameterNamespace::from_external_slots(
    const std::span<const ExternalParameterSlot> external_slots
) -> base::Result<ParameterNamespace> {
    auto count = checked_parameter_count(external_slots.size());
    if (!count) {
        return base::fail(std::move(count).error());
    }

    std::vector<PlanParameter> parameters;
    parameters.reserve(external_slots.size());
    for (std::size_t index = 0U; index < external_slots.size(); ++index) {
        const ExternalParameterSlot& external = external_slots[index];
        const auto expected_slot = static_cast<std::uint32_t>(index + 1U);
        if (!external.slot.valid() || external.slot.one_based() != expected_slot) {
            return base::fail(plan_error("external parameters must have dense one-based slots"));
        }
        if (!valid_type(external.data_type) || external.data_type == types::kUnknownType) {
            return base::fail(plan_error("external parameter has an invalid or UNKNOWN type"));
        }
        parameters.push_back(
            PlanParameter{
                .id = PlanParameterId{static_cast<std::uint32_t>(index)},
                .kind = PlanParameterKind::kExternal,
                .data_type = external.data_type,
                .external_slot = external.slot,
            }
        );
    }
    return ParameterNamespace{std::move(parameters), count.value()};
}

auto ParameterNamespace::external_reference(const ExternalParameterNumber slot) const
    -> base::Result<PlanParameterReference> {
    if (!valid()) {
        return base::fail(parameter_namespace_unavailable("resolve external parameter"));
    }
    if (!slot.valid() || slot.one_based() > external_count_) {
        return base::fail(plan_error("external parameter slot is absent from the namespace"));
    }
    const auto index = static_cast<std::size_t>(slot.one_based() - 1U);
    return reference_from(parameters_.at(index));
}

auto ParameterNamespace::allocate_executor_parameter(const types::DataType data_type)
    -> base::Result<PlanParameterReference> {
    if (!valid()) {
        return base::fail(parameter_namespace_unavailable("allocate executor parameter"));
    }
    if (!valid_type(data_type) || data_type == types::kUnknownType) {
        return base::fail(plan_error("executor parameter has an invalid or UNKNOWN type"));
    }
    auto count = checked_parameter_count(parameters_.size());
    if (!count) {
        return base::fail(std::move(count).error());
    }
    if (count.value() == std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(plan_error("parameter namespace exceeds 32-bit capacity"));
    }
    parameters_.push_back(
        PlanParameter{
            .id = PlanParameterId{count.value()},
            .kind = PlanParameterKind::kExecutor,
            .data_type = data_type,
            .external_slot = std::nullopt,
        }
    );
    return reference_from(parameters_.back());
}

auto ParameterNamespace::reference(const PlanParameterId id) const
    -> base::Result<PlanParameterReference> {
    if (!valid()) {
        return base::fail(parameter_namespace_unavailable("resolve parameter"));
    }
    if (!id.valid() || static_cast<std::size_t>(id.zero_based()) >= parameters_.size()) {
        return base::fail(plan_error("parameter ID is absent from the namespace"));
    }
    return reference_from(parameters_.at(static_cast<std::size_t>(id.zero_based())));
}

auto ParameterNamespace::parameters() const noexcept -> std::span<const PlanParameter> {
    return valid() ? std::span<const PlanParameter>{parameters_} : std::span<const PlanParameter>{};
}

auto ParameterNamespace::external_parameter_count() const noexcept -> std::uint32_t {
    return valid() ? external_count_ : 0U;
}

[[maybe_unused]] auto PlannedStatement::create(
    [[maybe_unused]] const std::uint64_t catalog_generation,
    [[maybe_unused]] Plan planned_root,
    [[maybe_unused]] ParameterNamespace parameters
) -> base::Result<PlannedStatement> {
    // LAB-S04.Q08:R15-BEGIN
    base::lab::missing_algorithm("S04.Q08:R15");
    // LAB-S04.Q08:R15-END
}

auto plan_operator_kind(const Plan& plan) -> PlanOperatorKind {
    return std::visit(
        Overloaded{
            [](const ValuesPlan&) {
                return PlanOperatorKind::kValues;
            },
            [](const SeqScanPlan&) {
                return PlanOperatorKind::kSeqScan;
            },
            [](const IndexScanPlan&) {
                return PlanOperatorKind::kIndexScan;
            },
            [](const ResultPlan&) {
                return PlanOperatorKind::kResult;
            },
            [](const ModifyTablePlan&) {
                return PlanOperatorKind::kModifyTable;
            },
            [](const SortPlan&) {
                return PlanOperatorKind::kSort;
            },
            [](const LimitPlan&) {
                return PlanOperatorKind::kLimit;
            },
            [](const TopNPlan&) {
                return PlanOperatorKind::kTopN;
            },
            [](const AppendPlan&) {
                return PlanOperatorKind::kAppend;
            },
            [](const UniquePlan&) {
                return PlanOperatorKind::kUnique;
            },
            [](const AggregatePlan&) {
                return PlanOperatorKind::kAggregate;
            },
            [](const NestedLoopPlan&) {
                return PlanOperatorKind::kNestedLoop;
            },
            [](const MergeJoinPlan&) {
                return PlanOperatorKind::kMergeJoin;
            },
            [](const HashJoinPlan&) {
                return PlanOperatorKind::kHashJoin;
            },
            [](const SubqueryScanPlan&) {
                return PlanOperatorKind::kSubqueryScan;
            },
            [](const SubPlanEvalPlan&) {
                return PlanOperatorKind::kSubPlanEval;
            },
        },
        plan.node
    );
}

auto plan_metadata(const Plan& plan) -> const PlanOperatorMetadata& {
    return std::visit(
        [](const auto& node) -> const PlanOperatorMetadata& {
            return node.metadata;
        },
        plan.node
    );
}

}  // namespace db::planner
