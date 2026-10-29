#include "db/execution/exec_context.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {
namespace {

auto context_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.context", "parameters");
}

}  // namespace

ParameterDescriptor::ParameterDescriptor(
    const ConstructionToken construction_token,
    std::vector<ParameterSlotSchema> slots
)
    : slots_{std::move(slots)} {
    static_cast<void>(construction_token);
}

auto ParameterDescriptor::slots() const noexcept -> std::span<const ParameterSlotSchema> {
    return slots_;
}

ExecContext::ExecContext(
    ParameterDescriptorHandle parameter_descriptor,
    std::vector<ParameterSlot> parameter_slots,
    const std::size_t external_parameter_count
)
    : parameter_descriptor_{std::move(parameter_descriptor)},
      parameter_slots_{std::move(parameter_slots)},
      external_parameter_count_{external_parameter_count} {}

ExecContext::ExecContext(ExecContext&& other) noexcept
    : parameter_descriptor_{std::move(other.parameter_descriptor_)},
      parameter_slots_{std::move(other.parameter_slots_)},
      external_parameter_count_{std::exchange(other.external_parameter_count_, 0U)} {
    other.parameter_slots_.clear();
}

auto ExecContext::operator=(ExecContext&& other) noexcept -> ExecContext& {
    if (this != &other) {
        parameter_descriptor_ = std::move(other.parameter_descriptor_);
        parameter_slots_ = std::move(other.parameter_slots_);
        external_parameter_count_ = std::exchange(other.external_parameter_count_, 0U);
        other.parameter_slots_.clear();
    }
    return *this;
}

auto ExecContext::create(
    const planner::ParameterNamespace& parameter_namespace,
    const std::span<const types::Value> external_parameters
) -> base::Result<ExecContext> {
    if (!parameter_namespace.valid()) {
        return base::fail(
            context_error(base::ErrorCode::kUnavailable, "parameter namespace has been moved from")
        );
    }
    const std::span<const planner::PlanParameter> parameters = parameter_namespace.parameters();
    const std::size_t external_count = parameter_namespace.external_parameter_count();
    if (external_parameters.size() != external_count) {
        return base::fail(context_error(
            base::ErrorCode::kInvalidArgument,
            "external parameter value count does not match the planned namespace"
        ));
    }

    std::vector<ParameterSlot> parameter_slots;
    std::vector<ParameterSlotSchema> parameter_schema;
    parameter_slots.reserve(parameters.size());
    parameter_schema.reserve(parameters.size());
    for (std::size_t index = 0U; index < parameters.size(); ++index) {
        const planner::PlanParameter& parameter = parameters[index];
        parameter_schema.push_back(
            ParameterSlotSchema{
                .kind = parameter.kind,
                .data_type = parameter.data_type,
            }
        );
        if (index < external_count) {
            parameter_slots.push_back(
                ParameterSlot{
                    .value = external_parameters[index],
                }
            );
            continue;
        }
        parameter_slots.push_back(
            ParameterSlot{
                .value = std::nullopt,
            }
        );
    }
    auto descriptor = std::make_shared<const ParameterDescriptor>(
        ParameterDescriptor::ConstructionToken{},
        std::move(parameter_schema)
    );
    return ExecContext{std::move(descriptor), std::move(parameter_slots), external_count};
}

auto ExecContext::parameter_count() const noexcept -> std::size_t {
    return parameter_slots_.size();
}

auto ExecContext::external_parameter_count() const noexcept -> std::size_t {
    return external_parameter_count_;
}

auto ExecContext::parameter_descriptor() const noexcept -> const ParameterDescriptorHandle& {
    return parameter_descriptor_;
}

auto ExecContext::snapshot() const -> ExecContext {
    return ExecContext{parameter_descriptor_, parameter_slots_, external_parameter_count_};
}

auto ExecContext::parameter(const planner::PlanParameterReference& reference) const
    -> base::Result<std::reference_wrapper<const types::Value>> {
    auto index = resolve_parameter(reference);
    if (!index) {
        return base::fail(
            std::move(index).error().with_context("execution.context", "read parameter")
        );
    }
    return parameter_value(index.value());
}

auto ExecContext::parameter_value(const std::size_t index) const
    -> base::Result<std::reference_wrapper<const types::Value>> {
    if (index >= parameter_slots_.size()) {
        return base::fail(context_error(
            base::ErrorCode::kExecution,
            "parameter index is outside this execution namespace"
        ));
    }
    const ParameterSlot& slot = parameter_slots_[index];
    if (!slot.value.has_value()) {
        return base::fail(context_error(
            base::ErrorCode::kExecution,
            "executor parameter is not bound for this execution"
        ));
    }
    return std::cref(*slot.value);
}

auto ExecContext::executor_parameter_is_bound(
    const planner::PlanParameterReference& reference
) const -> base::Result<bool> {
    auto index = resolve_parameter(reference);
    if (!index) {
        return base::fail(
            std::move(index).error().with_context("execution.context", "inspect executor parameter")
        );
    }
    if (index.value() < external_parameter_count_) {
        return base::fail(context_error(
            base::ErrorCode::kExecution,
            "parameter reference does not name an executor parameter"
        ));
    }
    const ParameterSlot& slot = parameter_slots_[index.value()];
    return slot.value.has_value();
}

auto ExecContext::set_executor_parameter(
    const planner::PlanParameterReference& reference,
    types::Value value
) -> base::Status {
    auto index = resolve_parameter(reference);
    if (!index) {
        return base::fail(
            std::move(index).error().with_context("execution.context", "set executor parameter")
        );
    }
    if (index.value() < external_parameter_count_) {
        return base::fail(context_error(
            base::ErrorCode::kExecution,
            "parameter reference does not name an executor parameter"
        ));
    }
    ParameterSlot& slot = parameter_slots_[index.value()];
    const auto slot_schema = parameter_descriptor_->slots()[index.value()];
    if (value.data_type() != slot_schema.data_type) {
        return base::fail(context_error(
            base::ErrorCode::kInvalidArgument,
            "executor parameter value type does not match its planned type"
        ));
    }
    slot.value = std::move(value);
    return {};
}

auto ExecContext::clear_executor_parameter(const planner::PlanParameterReference& reference)
    -> base::Status {
    auto index = resolve_parameter(reference);
    if (!index) {
        return base::fail(
            std::move(index).error().with_context("execution.context", "clear executor parameter")
        );
    }
    if (index.value() < external_parameter_count_) {
        return base::fail(context_error(
            base::ErrorCode::kExecution,
            "parameter reference does not name an executor parameter"
        ));
    }
    ParameterSlot& slot = parameter_slots_[index.value()];
    slot.value.reset();
    return {};
}

void ExecContext::clear_executor_parameters() noexcept {
    for (std::size_t index = external_parameter_count_; index < parameter_slots_.size(); ++index) {
        parameter_slots_[index].value.reset();
    }
}

auto ExecContext::resolve_parameter(const planner::PlanParameterReference& reference) const
    -> base::Result<std::size_t> {
    if (!reference.id.valid()) {
        return base::fail(
            context_error(base::ErrorCode::kExecution, "parameter reference has an invalid ID")
        );
    }
    const std::size_t index = reference.id.zero_based();
    if (index >= parameter_slots_.size()) {
        return base::fail(context_error(
            base::ErrorCode::kExecution,
            "parameter reference is outside this execution namespace"
        ));
    }
    const ParameterSlotSchema slot = parameter_descriptor_->slots()[index];
    if (slot.kind != reference.kind || slot.data_type != reference.data_type) {
        return base::fail(context_error(
            base::ErrorCode::kExecution,
            "parameter reference does not match this execution namespace"
        ));
    }
    return index;
}

}  // namespace db::execution
