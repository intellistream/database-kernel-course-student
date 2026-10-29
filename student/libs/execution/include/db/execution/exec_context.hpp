#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "db/base/result.hpp"
#include "db/types/value.hpp"

namespace db::planner {

class ParameterNamespace;
struct PlanParameterReference;
enum class PlanParameterKind : std::uint8_t;

}  // namespace db::planner

namespace db::execution {

struct ParameterSlotSchema final {
    planner::PlanParameterKind kind{};
    types::DataType data_type{types::kUnknownType};

    auto operator==(const ParameterSlotSchema&) const -> bool = default;
};

class ParameterDescriptor final {
private:
    struct ConstructionToken final {};

public:
    ParameterDescriptor(
        ConstructionToken construction_token,
        std::vector<ParameterSlotSchema> slots
    );
    auto slots() const noexcept -> std::span<const ParameterSlotSchema>;

private:
    friend class ExecContext;

    std::vector<ParameterSlotSchema> slots_;
};

using ParameterDescriptorHandle = std::shared_ptr<const ParameterDescriptor>;

// Query-local mutable parameter storage. The planner namespace owns the fixed
// slot layout, and the engine supplies count-checked, type-adapted external
// values. ExecContext copies both without repeating those boundary checks, so
// a PlanState never borrows binder or caller-owned parameter memory. An
// ExecContext is intentionally thread-confined: one plan tree is driven
// sequentially by one session/command at a time.
class ExecContext final {
public:
    static auto create(
        const planner::ParameterNamespace& parameter_namespace,
        std::span<const types::Value> external_parameters
    ) -> base::Result<ExecContext>;

    ExecContext(const ExecContext&) = delete;
    auto operator=(const ExecContext&) -> ExecContext& = delete;
    ExecContext(ExecContext&& other) noexcept;
    auto operator=(ExecContext&& other) noexcept -> ExecContext&;
    ~ExecContext() = default;

    auto parameter_count() const noexcept -> std::size_t;
    auto external_parameter_count() const noexcept -> std::size_t;
    // Stable schema identity shared by snapshots. The documented moved-from
    // empty context has no descriptor.
    auto parameter_descriptor() const noexcept -> const ParameterDescriptorHandle&;

    // Returns a value-owned frozen parameter namespace, including currently
    // bound executor parameters. Retained consumers use this to avoid
    // borrowing this thread-confined execution context.
    auto snapshot() const -> ExecContext;

    // The returned reference remains valid until the referenced executor slot
    // is changed or cleared, this context is moved, or it is destroyed.
    auto parameter(const planner::PlanParameterReference& reference) const
        -> base::Result<std::reference_wrapper<const types::Value>>;

    // Bounds and binding-state checked access for an ExpressionProgram that
    // has already matched its parameter references to parameter_descriptor().
    auto parameter_value(std::size_t index) const
        -> base::Result<std::reference_wrapper<const types::Value>>;

    auto executor_parameter_is_bound(const planner::PlanParameterReference& reference) const
        -> base::Result<bool>;
    auto
    set_executor_parameter(const planner::PlanParameterReference& reference, types::Value value)
        -> base::Status;
    auto clear_executor_parameter(const planner::PlanParameterReference& reference) -> base::Status;
    void clear_executor_parameters() noexcept;

private:
    struct ParameterSlot final {
        std::optional<types::Value> value{};
    };

    ExecContext(
        ParameterDescriptorHandle parameter_descriptor,
        std::vector<ParameterSlot> parameter_slots,
        std::size_t external_parameter_count
    );

    auto resolve_parameter(const planner::PlanParameterReference& reference) const
        -> base::Result<std::size_t>;
    ParameterDescriptorHandle parameter_descriptor_;
    std::vector<ParameterSlot> parameter_slots_;
    std::size_t external_parameter_count_{0U};
};

}  // namespace db::execution
