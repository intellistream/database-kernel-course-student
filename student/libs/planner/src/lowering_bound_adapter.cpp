#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/planner/lowering.hpp"
#include "db/sql/bound_ast.hpp"

namespace db::planner {
namespace {

[[maybe_unused]] auto
parameter_namespace_from_bound_slots(const std::span<const sql::BoundParameterSlot> bound_slots)
    -> base::Result<ParameterNamespace> {
    std::vector<ExternalParameterSlot> external_slots;
    external_slots.reserve(bound_slots.size());
    for (const auto& bound_slot : bound_slots) {
        external_slots.push_back(
            ExternalParameterSlot{
                .slot = ExternalParameterNumber{bound_slot.slot.one_based()},
                .data_type = bound_slot.data_type,
            }
        );
    }
    return ParameterNamespace::from_external_slots(external_slots);
}

}  // namespace

[[maybe_unused]] auto lower_bound_select(
    [[maybe_unused]] sql::BoundQuery query,
    [[maybe_unused]] const std::span<const sql::BoundParameterSlot> parameter_slots,
    [[maybe_unused]] const MetadataPort& metadata_port
) -> base::Result<LoweredSelectPlan> {
    // LAB-S02.Q05:R01-BEGIN
    base::lab::missing_algorithm("S02.Q05:R01");
    // LAB-S02.Q05:R01-END
}

[[maybe_unused]] auto lower_bound_statement(
    [[maybe_unused]] sql::BoundStatement statement,
    [[maybe_unused]] const std::span<const sql::BoundParameterSlot> parameter_slots,
    [[maybe_unused]] const MetadataPort& metadata_port
) -> base::Result<PlannedStatement> {
    // LAB-S02.Q05:R02-BEGIN
    base::lab::missing_algorithm("S02.Q05:R02");
    // LAB-S02.Q05:R02-END
}
}  // namespace db::planner
