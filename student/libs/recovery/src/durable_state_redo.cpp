#include "db/recovery/durable_state_redo.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/recovery/checkpoint.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/xid.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"

#include "startup_port_boundary.hpp"

namespace db::recovery {
namespace {

[[maybe_unused]] auto durable_state_error(
    const base::ErrorCode code,
    const std::string_view operation,
    std::string message
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "recovery.durable_state",
        std::string{operation}
    );
}

[[maybe_unused]] auto validate_seed(const DurableStateReplaySeed& seed) -> base::Status {
    if (seed.next_xid == base::kInvalidXid
        || (!transaction::is_normal_xid(seed.next_xid)
            && seed.next_xid != transaction::kXidWrapBoundary)) {
        return base::fail(durable_state_error(
            base::ErrorCode::kInvalidArgument,
            "create replayer",
            "durable replay seed has an invalid next XID"
        ));
    }
    if (seed.latest_completed_xid != base::kInvalidXid
        && (!transaction::is_normal_xid(seed.latest_completed_xid)
            || seed.latest_completed_xid.value() >= seed.next_xid.value())) {
        return base::fail(durable_state_error(
            base::ErrorCode::kInvalidArgument,
            "create replayer",
            "durable replay seed has an invalid completed-XID horizon"
        ));
    }
    if (seed.next_object_id == 0U) {
        return base::fail(durable_state_error(
            base::ErrorCode::kInvalidArgument,
            "create replayer",
            "durable replay seed has an invalid next object identifier"
        ));
    }
    return {};
}

[[maybe_unused]] auto require_final_status(
    [[maybe_unused]] const transaction::CommitLog& commit_log,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::string_view operation
) -> base::Status {
    // LAB-S10.Q10:R03-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q10:R03"});
    // LAB-S10.Q10:R03-END
}

[[maybe_unused]] auto next_after([[maybe_unused]] const base::Xid xid) noexcept -> base::Xid {
    // LAB-S10.Q10:R04-BEGIN
    base::lab::missing_algorithm("S10.Q10:R04");
    // LAB-S10.Q10:R04-END
}

}  // namespace

DurableStateReplayer::DurableStateReplayer(
    const DurableStateReplaySeed seed,
    transaction::CommitLog& commit_log,
    CatalogRecoveryPort& catalog
) noexcept
    : commit_log_{&commit_log},
      catalog_{&catalog},
      recovered_next_xid_{
          base::Xid{std::max(seed.next_xid.value(), commit_log.next_xid().value())}
      },
      recovered_next_object_id_{seed.next_object_id} {}

DurableStateReplayer::DurableStateReplayer(DurableStateReplayer&& other) noexcept
    : commit_log_{std::exchange(other.commit_log_, nullptr)},
      catalog_{std::exchange(other.catalog_, nullptr)},
      recovered_next_xid_{std::exchange(other.recovered_next_xid_, base::kInvalidXid)},
      recovered_next_object_id_{std::exchange(other.recovered_next_object_id_, 0U)},
      last_lsn_{std::exchange(other.last_lsn_, base::kInvalidLsn)},
      transaction_records_replayed_{std::exchange(other.transaction_records_replayed_, 0U)},
      finalized_result_{std::exchange(other.finalized_result_, std::nullopt)} {}

auto DurableStateReplayer::operator=(DurableStateReplayer&& other) noexcept
    -> DurableStateReplayer& {
    if (this == &other) {
        return *this;
    }
    commit_log_ = std::exchange(other.commit_log_, nullptr);
    catalog_ = std::exchange(other.catalog_, nullptr);
    recovered_next_xid_ = std::exchange(other.recovered_next_xid_, base::kInvalidXid);
    recovered_next_object_id_ = std::exchange(other.recovered_next_object_id_, 0U);
    last_lsn_ = std::exchange(other.last_lsn_, base::kInvalidLsn);
    transaction_records_replayed_ = std::exchange(other.transaction_records_replayed_, 0U);
    finalized_result_ = std::exchange(other.finalized_result_, std::nullopt);
    return *this;
}

auto DurableStateReplayer::valid() const noexcept -> bool {
    return commit_log_ != nullptr && catalog_ != nullptr;
}

[[maybe_unused]] auto DurableStateReplayer::create(
    [[maybe_unused]] const DurableStateReplaySeed seed,
    [[maybe_unused]] transaction::CommitLog& commit_log,
    [[maybe_unused]] CatalogRecoveryPort& catalog
) -> base::Result<DurableStateReplayer> {
    // LAB-S10.Q10:R05-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q10:R05"});
    // LAB-S10.Q10:R05-END
}

[[maybe_unused]] auto
DurableStateReplayer::replay([[maybe_unused]] const wal::DecodedWalRecord& decoded)
    -> base::Result<DurableStateReplayDisposition> {
    // LAB-S10.Q10:R06-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q10:R06"});
    // LAB-S10.Q10:R06-END
}

[[maybe_unused]] auto DurableStateReplayer::finalize() -> base::Result<DurableStateReplayResult> {
    // LAB-S10.Q10:R07-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q10:R07"});
    // LAB-S10.Q10:R07-END
}

}  // namespace db::recovery
