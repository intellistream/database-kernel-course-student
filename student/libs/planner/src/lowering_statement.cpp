#include <cstdint>
#include <utility>
#include <variant>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/planner/lowering.hpp"

#include "lowering_internal.hpp"

namespace db::planner {

[[maybe_unused]] auto assemble_planned_select([[maybe_unused]] LoweredSelectPlan lowered)
    -> base::Result<PlannedStatement> {
    // LAB-S02.Q05:R51-BEGIN
    base::lab::missing_algorithm("S02.Q05:R51");
    // LAB-S02.Q05:R51-END
}

[[maybe_unused]] auto lower_preprocessed_statement(
    [[maybe_unused]] PreprocessedStatement statement,
    [[maybe_unused]] const MetadataPort& metadata_port,
    [[maybe_unused]] ParameterNamespace parameters
) -> base::Result<PlannedStatement> {
    // LAB-S02.Q05:R52-BEGIN
    base::lab::missing_algorithm("S02.Q05:R52");
    // LAB-S02.Q05:R52-END
}
}  // namespace db::planner
