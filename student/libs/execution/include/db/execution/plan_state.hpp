#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "db/base/result.hpp"
#include "db/execution/exec_context.hpp"
#include "db/execution/tuple.hpp"

namespace db::execution {

enum class PlanStateLifecycle : std::uint8_t {
    kCreated,
    kOpen,
    kExhausted,
    kFailed,
    kClosed,
};

// Common checked lifecycle for every Volcano operator. Derived states own
// their resources through RAII and implement do_* hooks; this base owns the
// output slot and guarantees that no stale output remains after lifecycle
// calls. Any failed lifecycle hook leaves the state close-only so a derived
// operator never needs to promise recovery from partially acquired or advanced
// I/O.
class PlanState {
public:
    PlanState(const PlanState&) = delete;
    auto operator=(const PlanState&) -> PlanState& = delete;
    PlanState(PlanState&&) = delete;
    auto operator=(PlanState&&) -> PlanState& = delete;
    virtual ~PlanState() = default;

    auto open(ExecContext& context) -> base::Status;

    // Returns true only when output_slot() contains one newly materialized
    // tuple. False is stable end-of-stream until a successful rescan().
    auto next(ExecContext& context) -> base::Result<bool>;
    auto rescan(ExecContext& context) -> base::Status;

    // close() is idempotent and is the sole operation allowed after open,
    // next, or rescan fails. If cleanup reports an error, the state is
    // nevertheless terminal and will not dispatch do_close() again.
    auto close(ExecContext& context) -> base::Status;

    auto lifecycle() const noexcept -> PlanStateLifecycle;
    auto output_slot() const noexcept -> const TupleSlot&;

    // Transfers the current logical row to a terminal consumer and empties
    // the slot without changing the lifecycle. It returns nullopt when no row
    // is materialized. The physical RID and descriptor remain executor-local.
    auto take_output_values() noexcept -> std::optional<std::vector<types::Value>>;

protected:
    // Derived factories first create a checked TupleSlot, then move it into
    // this base. This prevents a state from existing without a descriptor.
    explicit PlanState(TupleSlot output_slot);

    // These are trusted plan-state materialization operations. Operator
    // factories compile output arity and types once; scan ports validate their
    // independently owned tuples before calling them. store_output() retags
    // the owned tuple with this state's immutable descriptor. Public
    // Tuple/TupleSlot construction remains checked for all other boundaries.
    void store_output(Tuple tuple);
    void store_output_values(
        std::vector<types::Value> values,
        std::optional<base::Rid> rid = std::nullopt
    );

    // Transfers the child's current materialization into this state without
    // copying its values. Factories must already have established compatible
    // input/output shapes. The tuple is retagged with this state's descriptor
    // and the child slot becomes empty, matching C++ ownership rather than
    // cloning rows at passthrough nodes.
    void transfer_output_from(PlanState& child);

    // Transfers only the logical values and discards the child's physical RID.
    // Projection nodes use this when an identity target list can reuse the
    // child's owned value vector but must keep Result's RID-free semantics.
    void transfer_output_values_from(PlanState& child);

    // Consumes a successfully produced child tuple for materializing states
    // that retain rows beyond the next child call. The caller must have just
    // observed child.next() == true; the child slot becomes empty.
    auto take_output_from(PlanState& child) -> Tuple;

    // Returns storage reclaimed from this state's previous output row. The
    // vector is empty when the row was consumed by ownership transfer or no
    // previous row exists.
    auto take_recycled_output_values() noexcept -> std::vector<types::Value>;

    virtual auto do_open(ExecContext& context) -> base::Status = 0;

    // On success, true requires a tuple stored through store_output*(); false
    // requires no stored tuple. do_open() and do_rescan() must not store a
    // tuple. The base validates all three conditions.
    virtual auto do_next(ExecContext& context) -> base::Result<bool> = 0;
    virtual auto do_rescan(ExecContext& context) -> base::Status = 0;
    // Must tolerate every partial-acquisition point in do_open(). It performs
    // one best-effort cleanup pass and leaves ordinary RAII members safe to
    // destroy even when it returns an error.
    virtual auto do_close(ExecContext& context) -> base::Status = 0;

private:
    void recycle_output_values() noexcept;

    TupleSlot output_slot_;
    std::vector<types::Value> recycled_output_values_;
    PlanStateLifecycle lifecycle_{PlanStateLifecycle::kCreated};
};

}  // namespace db::execution
