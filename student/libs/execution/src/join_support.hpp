#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "state_support.hpp"

namespace db::execution::detail {

inline auto validate_join_output_shape(
    const TupleDescriptorHandle& outer,
    const TupleDescriptorHandle& inner,
    const TupleDescriptorHandle& output,
    const std::string_view operation
) -> base::Status {
    const std::string prefix{operation};
    if (outer == nullptr || inner == nullptr || output == nullptr
        || output->size() != outer->size() + inner->size()) {
        return base::fail(
            state_plan_error(operation, prefix + " output shape differs from joined child schemas")
        );
    }
    const auto output_columns = output->columns();
    const auto outer_columns = outer->columns();
    const auto inner_columns = inner->columns();
    for (std::size_t index = 0U; index < outer_columns.size(); ++index) {
        if (output_columns[index].data_type != outer_columns[index].data_type) {
            return base::fail(
                state_plan_error(operation, prefix + " output type differs from outer child")
            );
        }
    }
    for (std::size_t index = 0U; index < inner_columns.size(); ++index) {
        if (output_columns[outer_columns.size() + index].data_type
            != inner_columns[index].data_type) {
            return base::fail(
                state_plan_error(operation, prefix + " output type differs from inner child")
            );
        }
    }
    return {};
}

}  // namespace db::execution::detail
