#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {
namespace {

struct LoweredModificationTarget final {
    PlanRelation relation;
    LoweredPlanInput scan;
};

[[maybe_unused]] auto verify_path_generation(
    [[maybe_unused]] const BaseRelationPathList& paths,
    [[maybe_unused]] const LoweringContext& context
) -> base::Status {
    // LAB-S02.Q05:R03-BEGIN
    base::lab::missing_algorithm("S02.Q05:R03");
    // LAB-S02.Q05:R03-END
}

[[maybe_unused]] auto lower_insert_target(
    [[maybe_unused]] const PreprocessedBaseTable& target,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<PlanRelation> {
    // LAB-S02.Q05:R04-BEGIN
    base::lab::missing_algorithm("S02.Q05:R04");
    // LAB-S02.Q05:R04-END
}

[[maybe_unused]] auto lower_modification_target(
    [[maybe_unused]] const PreprocessedBaseTable& target,
    [[maybe_unused]] const PreprocessedExpressionPtr& filter,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredModificationTarget> {
    // LAB-S02.Q05:R05-BEGIN
    base::lab::missing_algorithm("S02.Q05:R05");
    // LAB-S02.Q05:R05-END
}

[[maybe_unused]] auto lower_insert_columns(
    [[maybe_unused]] const std::vector<QueryColumnIndex>& columns,
    [[maybe_unused]] const PlanRelation& target
) -> base::Result<std::vector<PlanColumnIndex>> {
    // LAB-S02.Q05:R06-BEGIN
    base::lab::missing_algorithm("S02.Q05:R06");
    // LAB-S02.Q05:R06-END
}

[[maybe_unused]] auto lower_insert_values(
    [[maybe_unused]] const PreprocessedInsertValues& values,
    [[maybe_unused]] const PlanSchema& target_schema,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<PlanValuesModificationSource> {
    // LAB-S02.Q05:R07-BEGIN
    base::lab::missing_algorithm("S02.Q05:R07");
    // LAB-S02.Q05:R07-END
}

[[maybe_unused]] auto lower_insert_source(
    [[maybe_unused]] PreprocessedInsertSource& source,
    [[maybe_unused]] const PlanSchema& target_schema,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<PlanModificationSource> {
    // LAB-S02.Q05:R08-BEGIN
    base::lab::missing_algorithm("S02.Q05:R08");
    // LAB-S02.Q05:R08-END
}

[[maybe_unused]] auto lower_update_assignments(
    [[maybe_unused]] const std::vector<PreprocessedAssignment>& assignments,
    [[maybe_unused]] const PlanRelation& target,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<std::vector<PlanAssignment>> {
    // LAB-S02.Q05:R09-BEGIN
    base::lab::missing_algorithm("S02.Q05:R09");
    // LAB-S02.Q05:R09-END
}

[[maybe_unused]] auto modification_metadata() -> PlanOperatorMetadata {
    // LAB-S02.Q05:R10-BEGIN
    base::lab::missing_algorithm("S02.Q05:R10");
    // LAB-S02.Q05:R10-END
}
}  // namespace

[[maybe_unused]] auto lower_insert_plan(
    [[maybe_unused]] PreprocessedInsertStatement& statement,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<Plan> {
    // LAB-S02.Q05:R11-BEGIN
    base::lab::missing_algorithm("S02.Q05:R11");
    // LAB-S02.Q05:R11-END
}

[[maybe_unused]] auto lower_update_plan(
    [[maybe_unused]] PreprocessedUpdateStatement& statement,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<Plan> {
    // LAB-S02.Q05:R12-BEGIN
    base::lab::missing_algorithm("S02.Q05:R12");
    // LAB-S02.Q05:R12-END
}

[[maybe_unused]] auto lower_delete_plan(
    [[maybe_unused]] PreprocessedDeleteStatement& statement,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<Plan> {
    // LAB-S02.Q05:R13-BEGIN
    base::lab::missing_algorithm("S02.Q05:R13");
    // LAB-S02.Q05:R13-END
}
}  // namespace db::planner::detail
