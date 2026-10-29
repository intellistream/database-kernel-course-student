#pragma once

#include <span>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/execution/tuple.hpp"
#include "db/types/value.hpp"

namespace db::execution {

// Execution-owned mutation target. Plan adaptation resolves the planner schema
// into this immutable descriptor before any port call, so a storage adapter
// never receives or retains planner IR.
struct ModifyTarget final {
    base::Oid relation_oid{base::kInvalidOid};
    TupleDescriptorHandle descriptor{};
};

// Result returned by the access adapter after one UPDATE. The adapter owns
// heap/MVCC/WAL work and reports whether a new index entry is required. A HOT
// update returns false; a cold update returns true after maintaining indexes.
struct ModifyUpdateResult final {
    base::Rid rid{base::kInvalidRid};
    bool index_maintenance_required{false};
};

// Consumer-owned mutation port. Execution supplies borrowed, materialized
// values and an execution-owned target for one synchronous call; the adapter
// must not retain the target reference or value spans. The concrete
// implementation owns catalog-aware indexed-column comparison together with
// access, transaction, WAL, and index services at the composition root. The
// port object must outlive its ModifyTableState.
class ModifyTablePort {
public:
    ModifyTablePort(const ModifyTablePort&) = delete;
    auto operator=(const ModifyTablePort&) -> ModifyTablePort& = delete;
    ModifyTablePort(ModifyTablePort&&) = delete;
    auto operator=(ModifyTablePort&&) -> ModifyTablePort& = delete;
    virtual ~ModifyTablePort() = default;

    virtual auto insert(const ModifyTarget& target, std::span<const types::Value> values)
        -> base::Result<base::Rid> = 0;

    virtual auto update(
        const ModifyTarget& target,
        base::Rid rid,
        std::span<const types::Value> old_values,
        std::span<const types::Value> new_values
    ) -> base::Result<ModifyUpdateResult> = 0;

    virtual auto
    delete_row(const ModifyTarget& target, base::Rid rid, std::span<const types::Value> old_values)
        -> base::Status = 0;

protected:
    ModifyTablePort() = default;
};

}  // namespace db::execution
