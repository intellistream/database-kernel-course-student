#include "db/transaction/ssi.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/xid.hpp"

#include "ssi_commit_order.hpp"
#include "ssi_conflict_graph.hpp"
#include "ssi_lifecycle.hpp"
#include "ssi_predicate_locks.hpp"
#include "ssi_read_locks.hpp"
#include "ssi_state.hpp"

namespace db::transaction {
namespace {

using detail::ActiveSnapshotHorizon;
using detail::CommittedStateIndex;
using detail::PredicateCandidate;
using detail::SsiPredicateIndex;
using detail::SsiPredicateLock;
using detail::SsiStateMap;
using detail::SsiTargetIndex;
using detail::SsiTransactionState;

[[maybe_unused]] auto ssi_error(base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "transaction/ssi",
        std::move(operation)
    );
}

[[maybe_unused]] auto valid_optional_xid(base::Xid xid) noexcept -> bool {
    return xid == base::kInvalidXid || is_normal_xid(xid);
}

[[maybe_unused]] auto valid_snapshot(const Snapshot& snapshot) noexcept -> bool {
    if (!is_normal_xid(snapshot.xmin)
        || (!is_normal_xid(snapshot.xmax) && snapshot.xmax != kXidWrapBoundary)
        || snapshot.xmin.value() > snapshot.xmax.value()) {
        return false;
    }
    std::uint32_t previous = 0;
    for (const auto xid : snapshot.in_progress) {
        if (!is_normal_xid(xid) || xid.value() < snapshot.xmin.value()
            || xid.value() >= snapshot.xmax.value() || xid.value() <= previous) {
            return false;
        }
        previous = xid.value();
    }
    return true;
}

[[maybe_unused]] auto transaction_set([[maybe_unused]] const Snapshot& snapshot) -> Snapshot {
    // LAB-S09.Q06:R01-BEGIN
    base::lab::missing_algorithm("S09.Q06:R01");
    // LAB-S09.Q06:R01-END
}

[[maybe_unused]] auto contains_xid(const std::vector<base::Xid>& values, base::Xid xid) noexcept
    -> bool {
    return std::ranges::find(values, xid) != values.end();
}

}  // namespace

struct SerializableConflictManager::Inner final {
    mutable std::mutex mutex;
    SsiStateMap states;
    SsiTargetIndex target_index;
    SsiPredicateIndex predicate_index;
    ActiveSnapshotHorizon active_snapshots;
    CommittedStateIndex committed_states;
    std::uint64_t last_commit_sequence{1};
    std::uint64_t next_predicate_lock_id{1};
};

[[maybe_unused]] auto SsiReadTarget::for_tuple(
    [[maybe_unused]] base::RelationId relation_id,
    [[maybe_unused]] base::Rid rid
) noexcept -> SsiReadTarget {
    // LAB-S09.Q06:R02-BEGIN
    base::lab::missing_algorithm("S09.Q06:R02");
    // LAB-S09.Q06:R02-END
}

[[maybe_unused]] auto SsiReadTarget::for_page(
    [[maybe_unused]] base::RelationId relation_id,
    [[maybe_unused]] base::PageId page_id
) noexcept -> SsiReadTarget {
    // LAB-S09.Q06:R03-BEGIN
    base::lab::missing_algorithm("S09.Q06:R03");
    // LAB-S09.Q06:R03-END
}

[[maybe_unused]] auto
SsiReadTarget::for_relation([[maybe_unused]] base::RelationId relation_id) noexcept
    -> SsiReadTarget {
    // LAB-S09.Q06:R04-BEGIN
    base::lab::missing_algorithm("S09.Q06:R04");
    // LAB-S09.Q06:R04-END
}

auto SsiReadTarget::valid() const noexcept -> bool {
    if (relation_id == base::kInvalidRelationId) {
        return false;
    }
    switch (granularity) {
        case SsiLockGranularity::kTuple:
            return page_id != base::kInvalidPageId && slot_id != base::kInvalidSlotId;
        case SsiLockGranularity::kPage:
            return page_id != base::kInvalidPageId && slot_id == base::kInvalidSlotId;
        case SsiLockGranularity::kRelation:
            return page_id == base::kInvalidPageId && slot_id == base::kInvalidSlotId;
    }
    return false;
}

[[maybe_unused]] auto SsiPredicate::all() noexcept -> SsiPredicate {
    // LAB-S09.Q06:R05-BEGIN
    base::lab::missing_algorithm("S09.Q06:R05");
    // LAB-S09.Q06:R05-END
}

[[maybe_unused]] SsiPredicate::SsiPredicate(
    [[maybe_unused]] std::shared_ptr<const SsiPredicateMatcher> implementation
) noexcept
    : implementation_{std::move(implementation)} {
    // LAB-S09.Q06:R06-BEGIN
    base::lab::missing_algorithm("S09.Q06:R06");
    // LAB-S09.Q06:R06-END
}

[[maybe_unused]] auto
SsiPredicate::matcher([[maybe_unused]] std::shared_ptr<const SsiPredicateMatcher> implementation)
    -> base::Result<SsiPredicate> {
    // LAB-S09.Q06:R07-BEGIN
    base::lab::missing_algorithm("S09.Q06:R07");
    // LAB-S09.Q06:R07-END
}

[[maybe_unused]] auto SsiPredicate::is_all() const noexcept -> bool {
    // LAB-S09.Q06:R08-BEGIN
    base::lab::missing_algorithm("S09.Q06:R08");
    // LAB-S09.Q06:R08-END
}

[[maybe_unused]] auto
SsiPredicate::matches([[maybe_unused]] const std::span<const types::Value> values) const
    -> base::Result<bool> {
    // LAB-S09.Q06:R09-BEGIN
    base::lab::missing_algorithm("S09.Q06:R09");
    // LAB-S09.Q06:R09-END
}

[[maybe_unused]] SerializableConflictManager::SerializableConflictManager()
    : inner_{std::make_unique<Inner>()} {
    // LAB-S09.Q06:R10-BEGIN
    base::lab::missing_algorithm("S09.Q06:R10");
    // LAB-S09.Q06:R10-END
}

SerializableConflictManager::~SerializableConflictManager() = default;

[[maybe_unused]] auto
SerializableConflictManager::register_transaction([[maybe_unused]] base::Xid xid)
    -> base::Result<std::uint64_t> {
    // LAB-S09.Q06:R11-BEGIN
    base::lab::missing_algorithm("S09.Q06:R11");
    // LAB-S09.Q06:R11-END
}

[[maybe_unused]] auto SerializableConflictManager::set_snapshot(
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] const Snapshot& snapshot
) -> base::Status {
    // LAB-S09.Q06:R12-BEGIN
    base::lab::missing_algorithm("S09.Q06:R12");
    // LAB-S09.Q06:R12-END
}

[[maybe_unused]] auto SerializableConflictManager::record_read(
    [[maybe_unused]] base::Xid reader_xid,
    [[maybe_unused]] SsiReadRecord record
) -> base::Status {
    // LAB-S09.Q06:R13-BEGIN
    base::lab::missing_algorithm("S09.Q06:R13");
    // LAB-S09.Q06:R13-END
}

[[maybe_unused]] auto SerializableConflictManager::record_invisible_conflict(
    [[maybe_unused]] base::Xid reader_xid,
    [[maybe_unused]] base::Xid writer_xid
) -> base::Status {
    // LAB-S09.Q06:R14-BEGIN
    base::lab::missing_algorithm("S09.Q06:R14");
    // LAB-S09.Q06:R14-END
}

[[maybe_unused]] auto SerializableConflictManager::record_predicate_read(
    [[maybe_unused]] const base::Xid reader_xid,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] SsiPredicate predicate
) -> base::Status {
    // LAB-S09.Q06:R15-BEGIN
    base::lab::missing_algorithm("S09.Q06:R15");
    // LAB-S09.Q06:R15-END
}

[[maybe_unused]] auto SerializableConflictManager::copy_page_lock_after_split(
    [[maybe_unused]] base::RelationId relation_id,
    [[maybe_unused]] base::PageId old_page_id,
    [[maybe_unused]] base::PageId new_page_id
) -> base::Status {
    // LAB-S09.Q06:R16-BEGIN
    base::lab::missing_algorithm("S09.Q06:R16");
    // LAB-S09.Q06:R16-END
}

[[maybe_unused]] auto SerializableConflictManager::check_write_conflict(
    [[maybe_unused]] base::Xid writer_xid,
    [[maybe_unused]] SsiReadTarget target
) -> base::Status {
    // LAB-S09.Q06:R17-BEGIN
    base::lab::missing_algorithm("S09.Q06:R17");
    // LAB-S09.Q06:R17-END
}

[[maybe_unused]] auto SerializableConflictManager::check_predicate_write_conflict(
    [[maybe_unused]] const base::Xid writer_xid,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const std::optional<std::span<const types::Value>> old_values,
    [[maybe_unused]] const std::optional<std::span<const types::Value>> new_values
) -> base::Status {
    // LAB-S09.Q06:R18-BEGIN
    base::lab::missing_algorithm("S09.Q06:R18");
    // LAB-S09.Q06:R18-END
}

[[maybe_unused]] auto SerializableConflictManager::precommit([[maybe_unused]] base::Xid xid)
    -> base::Status {
    // LAB-S09.Q06:R19-BEGIN
    base::lab::missing_algorithm("S09.Q06:R19");
    // LAB-S09.Q06:R19-END
}

[[maybe_unused]] auto SerializableConflictManager::mark_committed([[maybe_unused]] base::Xid xid)
    -> base::Result<std::uint64_t> {
    // LAB-S09.Q06:R20-BEGIN
    base::lab::missing_algorithm("S09.Q06:R20");
    // LAB-S09.Q06:R20-END
}

[[maybe_unused]] auto SerializableConflictManager::abort([[maybe_unused]] base::Xid xid)
    -> base::Status {
    // LAB-S09.Q06:R21-BEGIN
    base::lab::missing_algorithm("S09.Q06:R21");
    // LAB-S09.Q06:R21-END
}

[[maybe_unused]] void SerializableConflictManager::prune_committed() {
    // LAB-S09.Q06:R22-BEGIN
    base::lab::missing_algorithm("S09.Q06:R22");
    // LAB-S09.Q06:R22-END
}

[[maybe_unused]] auto SerializableConflictManager::mark_doomed([[maybe_unused]] base::Xid xid)
    -> base::Status {
    // LAB-S09.Q06:R23-BEGIN
    base::lab::missing_algorithm("S09.Q06:R23");
    // LAB-S09.Q06:R23-END
}

[[maybe_unused]] auto
SerializableConflictManager::outgoing_conflicts([[maybe_unused]] base::Xid xid) const
    -> std::vector<base::Xid> {
    // LAB-S09.Q06:R24-BEGIN
    base::lab::missing_algorithm("S09.Q06:R24");
    // LAB-S09.Q06:R24-END
}

[[maybe_unused]] auto
SerializableConflictManager::incoming_conflicts([[maybe_unused]] base::Xid xid) const
    -> std::vector<base::Xid> {
    // LAB-S09.Q06:R25-BEGIN
    base::lab::missing_algorithm("S09.Q06:R25");
    // LAB-S09.Q06:R25-END
}

auto SerializableConflictManager::read_lock_count(base::Xid xid) const -> std::size_t {
    const std::lock_guard lock{inner_->mutex};
    const auto found = inner_->states.find(xid);
    return found == inner_->states.end() ? 0 : found->second.read_locks.size();
}

auto SerializableConflictManager::predicate_lock_count(const base::Xid xid) const -> std::size_t {
    const std::lock_guard lock{inner_->mutex};
    const auto found = inner_->states.find(xid);
    return found == inner_->states.end() ? 0U : found->second.predicate_locks.size();
}

auto SerializableConflictManager::target_count() const -> std::size_t {
    const std::lock_guard lock{inner_->mutex};
    return inner_->target_index.size();
}

auto SerializableConflictManager::transaction_count() const -> std::size_t {
    const std::lock_guard lock{inner_->mutex};
    return inner_->states.size();
}

auto SerializableConflictManager::snapshot_sequence(base::Xid xid) const -> std::uint64_t {
    const std::lock_guard lock{inner_->mutex};
    const auto found = inner_->states.find(xid);
    return found == inner_->states.end() ? 0 : found->second.last_commit_before_snapshot;
}

auto SerializableConflictManager::commit_sequence(base::Xid xid) const -> std::uint64_t {
    const std::lock_guard lock{inner_->mutex};
    const auto found = inner_->states.find(xid);
    return found == inner_->states.end() ? 0 : found->second.commit_sequence;
}

auto SerializableConflictManager::is_doomed(base::Xid xid) const -> bool {
    const std::lock_guard lock{inner_->mutex};
    const auto found = inner_->states.find(xid);
    return found != inner_->states.end() && found->second.doomed;
}

auto SerializableConflictManager::did_write(base::Xid xid) const -> bool {
    const std::lock_guard lock{inner_->mutex};
    const auto found = inner_->states.find(xid);
    return found != inner_->states.end() && found->second.did_write;
}

}  // namespace db::transaction
