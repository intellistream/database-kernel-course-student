#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "db/base/result.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/plan_state.hpp"
#include "db/execution/scan_port.hpp"
#include "db/planner/plan.hpp"
#include "db/types/operator.hpp"

namespace db::execution {

class ProjectionProgram;

class ValuesState final : public PlanState {
public:
    ValuesState(const ValuesState&) = delete;
    auto operator=(const ValuesState&) -> ValuesState& = delete;
    ValuesState(ValuesState&&) = delete;
    auto operator=(ValuesState&&) -> ValuesState& = delete;
    static auto create(const planner::ValuesPlan& plan)
        -> base::Result<std::unique_ptr<ValuesState>>;

    ~ValuesState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    ValuesState(
        TupleSlot output_slot,
        Tuple input,
        std::vector<std::vector<ExpressionProgram>> rows
    );

    Tuple input_;
    std::vector<std::vector<ExpressionProgram>> rows_;
    std::size_t next_row_{0U};
};

class ResultState final : public PlanState {
public:
    ResultState(const ResultState&) = delete;
    auto operator=(const ResultState&) -> ResultState& = delete;
    ResultState(ResultState&&) = delete;
    auto operator=(ResultState&&) -> ResultState& = delete;
    static auto create(const planner::ResultPlan& plan, std::unique_ptr<PlanState> input)
        -> base::Result<std::unique_ptr<ResultState>>;

    ~ResultState() override;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    ResultState(
        TupleSlot output_slot,
        std::unique_ptr<PlanState> input,
        std::unique_ptr<ProjectionProgram> projection
    );

    std::unique_ptr<PlanState> input_;
    std::unique_ptr<ProjectionProgram> projection_;
};

// Shared PG-style scan loop. Access-specific states provide one physical tuple
// at a time; this base owns descriptor caching, qualification, optional target
// projection, and independent scan/output value recyclers.
class ScanState : public PlanState {
public:
    ScanState(const ScanState&) = delete;
    auto operator=(const ScanState&) -> ScanState& = delete;
    ScanState(ScanState&&) = delete;
    auto operator=(ScanState&&) -> ScanState& = delete;
    ~ScanState() override;

protected:
    ScanState(
        TupleSlot output_slot,
        TupleDescriptorHandle scan_descriptor,
        std::optional<ExpressionProgram> filter,
        std::unique_ptr<ProjectionProgram> projection
    );

    auto do_next(ExecContext& context) -> base::Result<bool> final;
    auto do_close(ExecContext& context) -> base::Status final;

    virtual auto next_scan_tuple(ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<Tuple>> = 0;
    virtual auto close_scan(ExecContext& context) -> base::Status = 0;

private:
    enum class Kernel : std::uint8_t {
        kRaw,
        kQual,
        kIdentityProject,
        kQualIdentityProject,
        kProject,
        kQualProject,
    };

    template <bool KQual, bool KProject, bool KIdentity>
    auto next_impl(ExecContext& context) -> base::Result<bool>;
    auto validate_port_tuple(const Tuple& tuple) -> base::Status;

    TupleDescriptorHandle scan_descriptor_;
    std::optional<ExpressionProgram> filter_;
    std::unique_ptr<ProjectionProgram> projection_;
    TupleDescriptorHandle validated_port_descriptor_;
    std::vector<types::Value> recycled_scan_values_;
    Kernel kernel_{Kernel::kRaw};
};

class SeqScanState final : public ScanState {
public:
    SeqScanState(const SeqScanState&) = delete;
    auto operator=(const SeqScanState&) -> SeqScanState& = delete;
    SeqScanState(SeqScanState&&) = delete;
    auto operator=(SeqScanState&&) -> SeqScanState& = delete;
    static auto create(const planner::SeqScanPlan& plan, std::unique_ptr<SeqScanPort> port)
        -> base::Result<std::unique_ptr<SeqScanState>>;
    static auto create_projected(
        const planner::SeqScanPlan& scan,
        const planner::ResultPlan& result,
        std::unique_ptr<SeqScanPort> port
    ) -> base::Result<std::unique_ptr<SeqScanState>>;

    ~SeqScanState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto next_scan_tuple(ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<Tuple>> override;
    auto close_scan(ExecContext& context) -> base::Status override;

private:
    static auto create_impl(
        const planner::SeqScanPlan& scan,
        const planner::ResultPlan* result,
        std::unique_ptr<SeqScanPort> port
    ) -> base::Result<std::unique_ptr<SeqScanState>>;

    SeqScanState(
        TupleSlot output_slot,
        TupleDescriptorHandle scan_descriptor,
        std::unique_ptr<SeqScanPort> port,
        SeqScanRequest request,
        std::optional<ExpressionProgram> filter,
        std::unique_ptr<ProjectionProgram> projection
    );

    std::unique_ptr<SeqScanPort> port_;
    SeqScanRequest request_;
};

class IndexScanState final : public ScanState {
public:
    IndexScanState(const IndexScanState&) = delete;
    auto operator=(const IndexScanState&) -> IndexScanState& = delete;
    IndexScanState(IndexScanState&&) = delete;
    auto operator=(IndexScanState&&) -> IndexScanState& = delete;
    static auto create(const planner::IndexScanPlan& plan, std::unique_ptr<IndexScanPort> port)
        -> base::Result<std::unique_ptr<IndexScanState>>;
    static auto create_projected(
        const planner::IndexScanPlan& scan,
        const planner::ResultPlan& result,
        std::unique_ptr<IndexScanPort> port
    ) -> base::Result<std::unique_ptr<IndexScanState>>;

    ~IndexScanState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto next_scan_tuple(ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<Tuple>> override;
    auto close_scan(ExecContext& context) -> base::Status override;

private:
    struct BoundDescriptor final {
        ScanKeyIndex key_index{};
        types::BinaryOperator comparison{types::BinaryOperator::kEqual};
    };

    struct RuntimeState final {
        Tuple input;
        std::unique_ptr<IndexScanPort> port;
        IndexScanRequest request;
        std::vector<BoundDescriptor> bounds;
        std::vector<ExpressionProgram> bound_programs;
    };

    static auto create_impl(
        const planner::IndexScanPlan& scan,
        const planner::ResultPlan* result,
        std::unique_ptr<IndexScanPort> port
    ) -> base::Result<std::unique_ptr<IndexScanState>>;

    IndexScanState(
        TupleSlot output_slot,
        TupleDescriptorHandle scan_descriptor,
        std::optional<ExpressionProgram> filter,
        std::unique_ptr<ProjectionProgram> projection,
        RuntimeState runtime
    );

    auto resolve_bounds(const Tuple& input, const ExecContext& context) const
        -> base::Result<std::vector<IndexScanBound>>;

    Tuple input_;
    std::unique_ptr<IndexScanPort> port_;
    IndexScanRequest request_;
    std::vector<BoundDescriptor> bounds_;
    std::vector<ExpressionProgram> bound_programs_;
};

}  // namespace db::execution
