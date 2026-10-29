#include "logical_scan_ssi.hpp"

#include "db/base/lab_missing.hpp"
#include "db/engine/session.hpp"

#include "query_error.hpp"
#include "storage_query_binding.hpp"

namespace db::engine::detail {

namespace {

// The transaction component owns the lifetime and graph semantics of a
// logical SIREAD, while this private adapter owns the execution-only bytecode,
// descriptor, and frozen parameters needed to evaluate it. No execution type
// crosses the transaction public boundary.
class ExecutionSsiPredicateMatcher final : public transaction::SsiPredicateMatcher {
public:
    [[maybe_unused]] ExecutionSsiPredicateMatcher(
        [[maybe_unused]] execution::ExpressionProgram program,
        [[maybe_unused]] execution::ExecContext context,
        [[maybe_unused]] execution::TupleDescriptorHandle descriptor
    ) noexcept
        : program_{std::move(program)},
          context_{std::move(context)},
          descriptor_{std::move(descriptor)} {
        // LAB-S09.Q05:R01-BEGIN
        base::lab::missing_algorithm("S09.Q05:R01");
        // LAB-S09.Q05:R01-END
    }

    [[maybe_unused]] auto matches([[maybe_unused]] const std::span<const types::Value> values) const
        -> base::Result<bool> override {
        // LAB-S09.Q05:R02-BEGIN
        base::lab::missing_algorithm("S09.Q05:R02");
        // LAB-S09.Q05:R02-END
    }

private:
    execution::ExpressionProgram program_;
    execution::ExecContext context_;
    execution::TupleDescriptorHandle descriptor_;
};

// The access layer sees raw heap payloads even when MVCC makes a physical
// version invisible. This adapter decodes such a payload and reuses the exact
// immutable SQL matcher retained by transaction, so an unrelated physical row
// cannot manufacture a reader ->rw writer edge in logical-predicate mode.
class ExecutionHeapScanQualificationPort final : public access::HeapScanQualificationPort {
public:
    [[maybe_unused]] ExecutionHeapScanQualificationPort(
        [[maybe_unused]] access::TupleSchema tuple_schema,
        [[maybe_unused]] std::shared_ptr<const transaction::SsiPredicateMatcher> matcher
    ) noexcept
        : tuple_schema_{std::move(tuple_schema)},
          matcher_{std::move(matcher)} {
        // LAB-S09.Q05:R03-BEGIN
        base::lab::missing_algorithm("S09.Q05:R03");
        // LAB-S09.Q05:R03-END
    }

    [[maybe_unused]] auto matches([[maybe_unused]] const std::span<const std::byte> payload) const
        -> base::Result<bool> override {
        // LAB-S09.Q05:R04-BEGIN
        base::lab::missing_algorithm("S09.Q05:R04");
        // LAB-S09.Q05:R04-END
    }

private:
    access::TupleSchema tuple_schema_;
    std::shared_ptr<const transaction::SsiPredicateMatcher> matcher_;
};

}  // namespace

[[maybe_unused]] auto compile_logical_scan_filter(
    [[maybe_unused]] const planner::PlanExpressionPtr& filter,
    [[maybe_unused]] const QueryExecutionContext& context
) -> base::Result<std::optional<execution::ExpressionProgram>> {
    // LAB-S09.Q05:R05-BEGIN
    base::lab::missing_algorithm("S09.Q05:R05");
    // LAB-S09.Q05:R05-END
}

[[maybe_unused]] auto make_logical_scan_ssi(
    [[maybe_unused]] const std::optional<execution::ExpressionProgram>& logical_filter,
    [[maybe_unused]] const execution::ExecContext& context,
    [[maybe_unused]] const execution::TupleDescriptorHandle& descriptor
) -> base::Result<LogicalScanSsi> {
    // LAB-S09.Q05:R06-BEGIN
    base::lab::missing_algorithm("S09.Q05:R06");
    // LAB-S09.Q05:R06-END
}

[[maybe_unused]] auto make_heap_scan_qualification(
    [[maybe_unused]] access::TupleSchema tuple_schema,
    [[maybe_unused]] std::shared_ptr<const transaction::SsiPredicateMatcher> matcher
) -> std::unique_ptr<access::HeapScanQualificationPort> {
    // LAB-S09.Q05:R07-BEGIN
    base::lab::missing_algorithm("S09.Q05:R07");
    // LAB-S09.Q05:R07-END
}

[[maybe_unused]] auto
reject_doomed_serializable_transaction([[maybe_unused]] QueryExecutionContext& context)
    -> base::Status {
    // LAB-S09.Q05:R08-BEGIN
    base::lab::missing_algorithm("S09.Q05:R08");
    // LAB-S09.Q05:R08-END
}
}  // namespace db::engine::detail
