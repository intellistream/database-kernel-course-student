#include "ssi_conflict_graph.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <vector>

#include "db/base/lab_missing.hpp"
#include "db/transaction/visibility.hpp"
#include "db/transaction/xid.hpp"

#include "ssi_lifecycle.hpp"

namespace db::transaction::detail {
namespace {

[[maybe_unused]] auto contains_xid(const std::vector<base::Xid>& values, base::Xid xid) noexcept
    -> bool {
    return std::ranges::find(values, xid) != values.end();
}

[[maybe_unused]] auto has_rw_path(
    [[maybe_unused]] const SsiStateMap& states,
    [[maybe_unused]] base::Xid start,
    [[maybe_unused]] base::Xid goal
) -> bool {
    // LAB-S09.Q06:R28-BEGIN
    base::lab::missing_algorithm("S09.Q06:R28");
    // LAB-S09.Q06:R28-END
}

[[maybe_unused]] auto postgres_failure_victim(
    [[maybe_unused]] const SsiStateMap& states,
    [[maybe_unused]] base::Xid reader_xid,
    [[maybe_unused]] base::Xid writer_xid,
    [[maybe_unused]] base::Xid actor_xid
) -> std::optional<base::Xid> {
    // LAB-S09.Q06:R29-BEGIN
    base::lab::missing_algorithm("S09.Q06:R29");
    // LAB-S09.Q06:R29-END
}
}  // namespace

[[maybe_unused]] auto ssi_reader_can_conflict_with_writer(
    [[maybe_unused]] const base::Xid reader_xid,
    [[maybe_unused]] const SsiTransactionState& reader,
    [[maybe_unused]] const SsiTransactionState& writer
) noexcept -> bool {
    // LAB-S09.Q06:R30-BEGIN
    base::lab::missing_algorithm("S09.Q06:R30");
    // LAB-S09.Q06:R30-END
}

[[maybe_unused]] void flag_ssi_rw_conflict(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] ActiveSnapshotHorizon& active_snapshots,
    [[maybe_unused]] const base::Xid reader_xid,
    [[maybe_unused]] const base::Xid writer_xid,
    [[maybe_unused]] const base::Xid actor_xid
) {
    // LAB-S09.Q06:R31-BEGIN
    base::lab::missing_algorithm("S09.Q06:R31");
    // LAB-S09.Q06:R31-END
}

[[maybe_unused]] void record_ssi_out_conflict_if_concurrent(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] ActiveSnapshotHorizon& active_snapshots,
    [[maybe_unused]] const base::Xid reader_xid,
    [[maybe_unused]] const base::Xid writer_xid
) {
    // LAB-S09.Q06:R32-BEGIN
    base::lab::missing_algorithm("S09.Q06:R32");
    // LAB-S09.Q06:R32-END
}
}  // namespace db::transaction::detail
