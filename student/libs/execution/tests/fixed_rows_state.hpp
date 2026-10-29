#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/execution/plan_state.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

namespace db::execution::test {

enum class RowsCall : std::uint8_t {
    kOpen,
    kNext,
    kRescan,
    kClose
};

struct RowsEvent final {
    RowsCall call;
    const ExecContext* context;
    std::size_t invocation;
};

struct RowsTrace final {
    std::vector<RowsEvent> events;
    std::array<std::size_t, 4U> calls{};

    auto count(const RowsCall call) const noexcept -> std::size_t {
        return calls[static_cast<std::size_t>(call)];
    }
};

struct RowsFault final {
    RowsCall call;
    std::size_t invocation;
    base::Error error;
};

// Test-only F-ROWS port. Each successful next copies the next prescribed tuple;
// rescan rewinds that finite script. Faults depend only on numbered hook calls.
// There is no expression, predicate, sorting, duplicate, or storage policy here.
class FixedRowsState final : public PlanState {
public:
    static auto create(
        const planner::PlanSchema& schema,
        std::vector<std::vector<types::Value>> rows,
        std::shared_ptr<RowsTrace> trace,
        std::vector<RowsFault> faults = {}
    ) -> base::Result<std::unique_ptr<FixedRowsState>> {
        if (trace == nullptr) {
            return base::fail(base::Error{base::ErrorCode::kInvalidArgument, "rows trace absent"});
        }
        auto descriptor = TupleDescriptor::from_plan_schema(schema);
        if (!descriptor) {
            return base::fail(std::move(descriptor).error());
        }
        auto slot = TupleSlot::create(*descriptor);
        if (!slot) {
            return base::fail(std::move(slot).error());
        }
        std::vector<Tuple> tuples;
        for (auto& row : rows) {
            auto tuple = Tuple::create(*descriptor, std::move(row));
            if (!tuple) {
                return base::fail(std::move(tuple).error());
            }
            tuples.push_back(std::move(*tuple));
        }
        return std::unique_ptr<FixedRowsState>{new FixedRowsState{
            std::move(*slot),
            std::move(tuples),
            std::move(trace),
            std::move(faults)
        }};
    }

protected:
    auto do_open(ExecContext& context) -> base::Status override {
        return record(RowsCall::kOpen, context);
    }

    auto do_next(ExecContext& context) -> base::Result<bool> override {
        auto status = record(RowsCall::kNext, context);
        if (!status) {
            return base::fail(std::move(status).error());
        }
        if (cursor_ == rows_.size()) {
            return false;
        }
        store_output(rows_[cursor_]);
        ++cursor_;
        return true;
    }

    auto do_rescan(ExecContext& context) -> base::Status override {
        auto status = record(RowsCall::kRescan, context);
        if (!status) {
            return status;
        }
        cursor_ = 0U;
        return {};
    }

    auto do_close(ExecContext& context) -> base::Status override {
        return record(RowsCall::kClose, context);
    }

private:
    FixedRowsState(
        TupleSlot slot,
        std::vector<Tuple> rows,
        std::shared_ptr<RowsTrace> trace,
        std::vector<RowsFault> faults
    )
        : PlanState{std::move(slot)},
          rows_{std::move(rows)},
          trace_{std::move(trace)},
          faults_{std::move(faults)} {}

    auto record(const RowsCall call, ExecContext& context) -> base::Status {
        const std::size_t invocation = ++trace_->calls[static_cast<std::size_t>(call)];
        trace_->events.push_back(RowsEvent{call, &context, invocation});
        for (const RowsFault& fault : faults_) {
            if (fault.call == call && fault.invocation == invocation) {
                return base::fail(fault.error);
            }
        }
        return {};
    }

    std::vector<Tuple> rows_;
    std::shared_ptr<RowsTrace> trace_;
    std::vector<RowsFault> faults_;
    std::size_t cursor_{0U};
};

}  // namespace db::execution::test
