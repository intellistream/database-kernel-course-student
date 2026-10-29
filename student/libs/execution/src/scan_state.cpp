#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/operators.hpp"
#include "db/execution/scan_port.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/types/operator.hpp"
#include "db/types/value.hpp"

#include "projection_program.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto scan_plan_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kPlan, std::move(message)}.with_context(
        "execution.operator",
        "construct"
    );
}

[[maybe_unused]] auto scan_runtime_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kExecution, std::move(message)}.with_context(
        "execution.operator",
        "run"
    );
}

[[maybe_unused]] auto descriptor_for_schema(const planner::PlanSchema& schema)
    -> base::Result<TupleDescriptorHandle> {
    return TupleDescriptor::from_plan_schema(schema);
}

[[maybe_unused]] auto empty_input_tuple() -> base::Result<Tuple> {
    auto descriptor = TupleDescriptor::create({});
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return Tuple::create(std::move(descriptor).value(), {});
}

[[maybe_unused]] auto output_slot_for(const planner::PlanSchema& schema)
    -> base::Result<TupleSlot> {
    auto descriptor = descriptor_for_schema(schema);
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return TupleSlot::create(std::move(descriptor).value());
}

[[maybe_unused]] auto compile_scan_filter(
    [[maybe_unused]] const planner::PlanExpressionPtr& expression,
    [[maybe_unused]] const std::string_view invalid_type_message
) -> base::Result<std::optional<ExpressionProgram>> {
    // LAB-S02.Q07:R17-BEGIN
    base::lab::missing_algorithm("S02.Q07:R17");
    // LAB-S02.Q07:R17-END
}

[[maybe_unused]] auto
valid_index_comparison([[maybe_unused]] const types::BinaryOperator operation) noexcept -> bool {
    // LAB-S02.Q07:R18-BEGIN
    base::lab::missing_algorithm("S02.Q07:R18");
    // LAB-S02.Q07:R18-END
}

[[maybe_unused]] auto validate_shape(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const TupleDescriptorHandle& expected,
    [[maybe_unused]] const std::string_view operation
) -> base::Status {
    // LAB-S02.Q07:R19-BEGIN
    base::lab::missing_algorithm("S02.Q07:R19");
    // LAB-S02.Q07:R19-END
}

[[maybe_unused]] constexpr auto scan_column_index(const planner::PlanColumnIndex index) noexcept
    -> ScanColumnIndex {
    return ScanColumnIndex{index.zero_based()};
}

[[maybe_unused]] constexpr auto scan_key_index(const planner::PlanIndexKeyIndex index) noexcept
    -> ScanKeyIndex {
    return ScanKeyIndex{index.zero_based()};
}

[[maybe_unused]] auto scan_required_columns(
    [[maybe_unused]] const std::optional<std::vector<planner::PlanColumnIndex>>& columns
) -> std::optional<std::vector<ScanColumnIndex>> {
    // LAB-S02.Q07:R20-BEGIN
    base::lab::missing_algorithm("S02.Q07:R20");
    // LAB-S02.Q07:R20-END
}

struct ScanOutput final {
    TupleSlot slot;
    std::unique_ptr<ProjectionProgram> projection;
};

[[maybe_unused]] auto scan_output(
    [[maybe_unused]] const TupleDescriptorHandle& scan_descriptor,
    [[maybe_unused]] const planner::ResultPlan* const result
) -> base::Result<ScanOutput> {
    // LAB-S02.Q07:R21-BEGIN
    base::lab::missing_algorithm("S02.Q07:R21");
    // LAB-S02.Q07:R21-END
}

}  // namespace

ScanState::ScanState(
    TupleSlot output_slot,
    TupleDescriptorHandle scan_descriptor,
    std::optional<ExpressionProgram> filter,
    std::unique_ptr<ProjectionProgram> projection
)
    : PlanState{std::move(output_slot)},
      scan_descriptor_{std::move(scan_descriptor)},
      filter_{std::move(filter)},
      projection_{std::move(projection)} {
    if (projection_ == nullptr) {
        kernel_ = filter_.has_value() ? Kernel::kQual : Kernel::kRaw;
    } else if (projection_->identity()) {
        kernel_ = filter_.has_value() ? Kernel::kQualIdentityProject : Kernel::kIdentityProject;
    } else {
        kernel_ = filter_.has_value() ? Kernel::kQualProject : Kernel::kProject;
    }
}

ScanState::~ScanState() = default;

[[maybe_unused]] auto ScanState::validate_port_tuple([[maybe_unused]] const Tuple& tuple)
    -> base::Status {
    // LAB-S02.Q07:R22-BEGIN
    base::lab::missing_algorithm("S02.Q07:R22");
    // LAB-S02.Q07:R22-END
}

template <bool KQual, bool KProject, bool KIdentity>
[[maybe_unused]] auto ScanState::next_impl([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S02.Q07:R23-BEGIN
    base::lab::missing_algorithm("S02.Q07:R23");
    // LAB-S02.Q07:R23-END
}

[[maybe_unused]] auto ScanState::do_next([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S02.Q07:R24-BEGIN
    base::lab::missing_algorithm("S02.Q07:R24");
    // LAB-S02.Q07:R24-END
}

auto ScanState::do_close(ExecContext& context) -> base::Status {
    auto closed = close_scan(context);
    validated_port_descriptor_.reset();
    recycled_scan_values_.clear();
    return closed;
}

SeqScanState::SeqScanState(
    TupleSlot output_slot,
    TupleDescriptorHandle scan_descriptor,
    std::unique_ptr<SeqScanPort> port,
    SeqScanRequest request,
    std::optional<ExpressionProgram> filter,
    std::unique_ptr<ProjectionProgram> projection
)
    : ScanState{
          std::move(output_slot),
          std::move(scan_descriptor),
          std::move(filter),
          std::move(projection),
      },
      port_{std::move(port)},
      request_{std::move(request)} {}

auto SeqScanState::create(const planner::SeqScanPlan& plan, std::unique_ptr<SeqScanPort> port)
    -> base::Result<std::unique_ptr<SeqScanState>> {
    return create_impl(plan, nullptr, std::move(port));
}

auto SeqScanState::create_projected(
    const planner::SeqScanPlan& scan,
    const planner::ResultPlan& result,
    std::unique_ptr<SeqScanPort> port
) -> base::Result<std::unique_ptr<SeqScanState>> {
    return create_impl(scan, &result, std::move(port));
}

[[maybe_unused]] auto SeqScanState::create_impl(
    [[maybe_unused]] const planner::SeqScanPlan& scan,
    [[maybe_unused]] const planner::ResultPlan* const result,
    [[maybe_unused]] std::unique_ptr<SeqScanPort> port
) -> base::Result<std::unique_ptr<SeqScanState>> {
    // LAB-S02.Q07:R25-BEGIN
    base::lab::missing_algorithm("S02.Q07:R25");
    // LAB-S02.Q07:R25-END
}

[[maybe_unused]] auto SeqScanState::do_open([[maybe_unused]] ExecContext& context) -> base::Status {
    // LAB-S02.Q07:R26-BEGIN
    base::lab::missing_algorithm("S02.Q07:R26");
    // LAB-S02.Q07:R26-END
}

[[maybe_unused]] auto SeqScanState::next_scan_tuple(
    [[maybe_unused]] ExecContext& context,
    [[maybe_unused]] std::vector<types::Value> recycled_values
) -> base::Result<std::optional<Tuple>> {
    // LAB-S02.Q07:R27-BEGIN
    base::lab::missing_algorithm("S02.Q07:R27");
    // LAB-S02.Q07:R27-END
}

[[maybe_unused]] auto SeqScanState::do_rescan([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S02.Q07:R28-BEGIN
    base::lab::missing_algorithm("S02.Q07:R28");
    // LAB-S02.Q07:R28-END
}

auto SeqScanState::close_scan(ExecContext& context) -> base::Status {
    return port_->close(context);
}

IndexScanState::IndexScanState(
    TupleSlot output_slot,
    TupleDescriptorHandle scan_descriptor,
    std::optional<ExpressionProgram> filter,
    std::unique_ptr<ProjectionProgram> projection,
    RuntimeState runtime
)
    : ScanState{
          std::move(output_slot),
          std::move(scan_descriptor),
          std::move(filter),
          std::move(projection),
      },
      input_{std::move(runtime.input)},
      port_{std::move(runtime.port)},
      request_{std::move(runtime.request)},
      bounds_{std::move(runtime.bounds)},
      bound_programs_{std::move(runtime.bound_programs)} {}

auto IndexScanState::create(const planner::IndexScanPlan& plan, std::unique_ptr<IndexScanPort> port)
    -> base::Result<std::unique_ptr<IndexScanState>> {
    return create_impl(plan, nullptr, std::move(port));
}

auto IndexScanState::create_projected(
    const planner::IndexScanPlan& scan,
    const planner::ResultPlan& result,
    std::unique_ptr<IndexScanPort> port
) -> base::Result<std::unique_ptr<IndexScanState>> {
    return create_impl(scan, &result, std::move(port));
}

[[maybe_unused]] auto IndexScanState::create_impl(
    [[maybe_unused]] const planner::IndexScanPlan& scan,
    [[maybe_unused]] const planner::ResultPlan* const result,
    [[maybe_unused]] std::unique_ptr<IndexScanPort> port
) -> base::Result<std::unique_ptr<IndexScanState>> {
    // LAB-S07.Q05:R05-BEGIN
    base::lab::missing_algorithm("S07.Q05:R05");
    // LAB-S07.Q05:R05-END
}

[[maybe_unused]] auto IndexScanState::resolve_bounds(
    [[maybe_unused]] const Tuple& input,
    [[maybe_unused]] const ExecContext& context
) const -> base::Result<std::vector<IndexScanBound>> {
    // LAB-S07.Q05:R06-BEGIN
    base::lab::missing_algorithm("S07.Q05:R06");
    // LAB-S07.Q05:R06-END
}

[[maybe_unused]] auto IndexScanState::do_open([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S07.Q05:R07-BEGIN
    base::lab::missing_algorithm("S07.Q05:R07");
    // LAB-S07.Q05:R07-END
}

[[maybe_unused]] auto IndexScanState::next_scan_tuple(
    [[maybe_unused]] ExecContext& context,
    [[maybe_unused]] std::vector<types::Value> recycled_values
) -> base::Result<std::optional<Tuple>> {
    // LAB-S02.Q07:R29-BEGIN
    base::lab::missing_algorithm("S02.Q07:R29");
    // LAB-S02.Q07:R29-END
}

[[maybe_unused]] auto IndexScanState::do_rescan([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S07.Q05:R08-BEGIN
    base::lab::missing_algorithm("S07.Q05:R08");
    // LAB-S07.Q05:R08-END
}

auto IndexScanState::close_scan(ExecContext& context) -> base::Status {
    return port_->close(context);
}

}  // namespace db::execution
