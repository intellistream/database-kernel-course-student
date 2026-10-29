#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/ssi.hpp"

namespace db::transaction::detail {

struct SsiPredicateLock final {
    std::uint64_t id{0U};
    base::RelationId relation_id{base::kInvalidRelationId};
    SsiPredicate predicate{SsiPredicate::all()};
};

struct PredicateCandidate final {
    base::Xid reader_xid{base::kInvalidXid};
    std::uint64_t lock_id{0U};
    base::RelationId relation_id{base::kInvalidRelationId};
    SsiPredicate predicate{SsiPredicate::all()};
};

struct SsiTransactionState final {
    [[maybe_unused]] explicit SsiTransactionState(
        [[maybe_unused]] std::uint64_t snapshot_sequence
    ) noexcept
        : last_commit_before_snapshot{snapshot_sequence} {
        // LAB-S09.Q06:R40-BEGIN
        base::lab::missing_algorithm("S09.Q06:R40");
        // LAB-S09.Q06:R40-END
    }

    std::vector<SsiReadRecord> read_locks;
    std::vector<SsiPredicateLock> predicate_locks;
    std::vector<base::Xid> outgoing;
    std::vector<base::Xid> incoming;
    std::uint64_t prepare_sequence{0};
    std::uint64_t commit_sequence{0};
    std::uint64_t last_commit_before_snapshot{0};
    std::uint64_t earliest_outgoing_commit{0};
    Snapshot snapshot;
    std::unordered_map<SsiReadTarget, std::size_t> tuple_counts;
    bool snapshot_set{false};
    bool did_write{false};
    bool doomed{false};

    auto prepared() const noexcept -> bool {
        return prepare_sequence != 0;
    }

    auto committed() const noexcept -> bool {
        return commit_sequence != 0;
    }

    auto committed_read_only() const noexcept -> bool {
        return committed() && !did_write;
    }
};

using SsiStateMap = std::unordered_map<base::Xid, SsiTransactionState>;
using SsiTargetIndex = std::unordered_map<SsiReadTarget, std::vector<base::Xid>>;
using SsiPredicateIndex = std::unordered_map<base::RelationId, std::vector<base::Xid>>;

struct ActiveSnapshotHorizon final {
    std::optional<std::uint64_t> oldest;
    std::size_t oldest_count{0U};
    bool dirty{false};
};

using CommittedStateIndex = std::map<std::uint64_t, base::Xid>;

}  // namespace db::transaction::detail
