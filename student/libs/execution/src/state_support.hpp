#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"

namespace db::execution::detail {

inline auto state_plan_error(const std::string_view operation, std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kPlan, std::move(message)}.with_context(
        "execution.plan_state",
        std::string{operation}
    );
}

inline auto output_slot_for_schema(const planner::PlanSchema& schema) -> base::Result<TupleSlot> {
    auto descriptor = TupleDescriptor::from_plan_schema(schema);
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return TupleSlot::create(std::move(descriptor).value());
}

inline auto validate_passthrough_shape(
    const TupleDescriptorHandle& input,
    const TupleDescriptorHandle& output,
    const std::string_view operation
) -> base::Status {
    if (input == nullptr || output == nullptr || input->size() != output->size()) {
        return base::fail(state_plan_error(
            operation,
            std::string{operation} + " output shape differs from its child"
        ));
    }
    const auto input_columns = input->columns();
    const auto output_columns = output->columns();
    for (std::size_t index = 0U; index < input_columns.size(); ++index) {
        if (input_columns[index].data_type != output_columns[index].data_type) {
            return base::fail(state_plan_error(
                operation,
                std::string{operation} + " output type differs from its child"
            ));
        }
    }
    return {};
}

}  // namespace db::execution::detail
