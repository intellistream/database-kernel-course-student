#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "db/base/hash.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/types/value.hpp"

namespace db::transaction {

inline constexpr std::size_t kSsiTupleLocksPerPage = 10;
inline constexpr std::size_t kSsiPageLocksPerRelation = 100;
inline constexpr std::size_t kSsiPredicatesPerRelation = 64;

enum class SsiLockGranularity : std::uint8_t {
    kTuple,
    kPage,
    kRelation,
};

// A type-tagged SIREAD target. Coarser targets use the explicit invalid page
// and slot sentinels; public mutations validate that representation.
struct SsiReadTarget final {
    SsiLockGranularity granularity{SsiLockGranularity::kRelation};
    base::RelationId relation_id{base::kInvalidRelationId};
    base::PageId page_id{base::kInvalidPageId};
    base::SlotId slot_id{base::kInvalidSlotId};

    static auto for_tuple(base::RelationId relation_id, base::Rid rid) noexcept -> SsiReadTarget;
    static auto for_page(base::RelationId relation_id, base::PageId page_id) noexcept
        -> SsiReadTarget;
    static auto for_relation(base::RelationId relation_id) noexcept -> SsiReadTarget;
    auto valid() const noexcept -> bool;

    auto operator==(const SsiReadTarget&) const -> bool = default;
};

struct SsiReadRecord final {
    SsiReadTarget target;
    // Informational tuple-version creator. It never owns this SIREAD lock.
    base::Xid creator_xid{base::kInvalidXid};
    // Concurrent creator/updater observed while reading, if any.
    base::Xid conflict_xid{base::kInvalidXid};

    auto operator==(const SsiReadRecord&) const -> bool = default;
};

// The transaction component retains an immutable SQL-level qualification for
// a scan, but does not know how that qualification is compiled or evaluated.
// An engine/execution adapter owns the concrete evaluator and must be
// immutable/thread-safe because distinct writers can call matches()
// concurrently after the manager releases its graph mutex. It returns a
// structured error rather than throwing across this boundary.
class SsiPredicateMatcher {
public:
    SsiPredicateMatcher(const SsiPredicateMatcher&) = delete;
    auto operator=(const SsiPredicateMatcher&) -> SsiPredicateMatcher& = delete;
    SsiPredicateMatcher(SsiPredicateMatcher&&) = delete;
    auto operator=(SsiPredicateMatcher&&) -> SsiPredicateMatcher& = delete;
    virtual ~SsiPredicateMatcher() = default;

    virtual auto matches(std::span<const types::Value> values) const -> base::Result<bool> = 0;

protected:
    SsiPredicateMatcher() = default;
};

// A retained SQL-scan qualification. `all()` protects a whole relation;
// `matcher()` protects precisely the rows whose old or new values satisfy the
// supplied immutable evaluator. The manager evaluates matchers after releasing
// its graph mutex and revalidates their lock identity before recording an edge.
class SsiPredicate final {
public:
    static auto all() noexcept -> SsiPredicate;
    static auto matcher(std::shared_ptr<const SsiPredicateMatcher> implementation)
        -> base::Result<SsiPredicate>;

    auto is_all() const noexcept -> bool;

private:
    explicit SsiPredicate(std::shared_ptr<const SsiPredicateMatcher> implementation) noexcept;

    auto matches(std::span<const types::Value> values) const -> base::Result<bool>;

    std::shared_ptr<const SsiPredicateMatcher> implementation_;

    friend class SerializableConflictManager;
};

// Process-wide Serializable Snapshot Isolation graph. Callers register an SSI
// participant before ProcArray capture, then attach the captured Snapshot.
// All graph, target-index, promotion, lifecycle, and diagnostic inspection
// serialize under one internal mutex. Core SSI traffic is mutation-heavy, so a
// reader/writer latch would add overhead without exposing useful concurrency.
class SerializableConflictManager final {
public:
    SerializableConflictManager();
    ~SerializableConflictManager();
    SerializableConflictManager(const SerializableConflictManager&) = delete;
    auto operator=(const SerializableConflictManager&) -> SerializableConflictManager& = delete;
    SerializableConflictManager(SerializableConflictManager&&) = delete;
    auto operator=(SerializableConflictManager&&) -> SerializableConflictManager& = delete;

    // Returns PostgreSQL's lastCommitBeforeSnapshot equivalent. Registration
    // is idempotent for the same live XID.
    auto register_transaction(base::Xid xid) -> base::Result<std::uint64_t>;
    auto set_snapshot(base::Xid xid, const Snapshot& snapshot) -> base::Status;

    auto record_read(base::Xid reader_xid, SsiReadRecord record) -> base::Status;
    auto record_invisible_conflict(base::Xid reader_xid, base::Xid writer_xid) -> base::Status;
    // Registers a logical SQL-scan predicate. Physical access methods retain
    // tuple/page/relation SIREADs; this form avoids treating every physically
    // inspected, filter-rejected tuple as a SQL read while still detecting
    // phantoms and predicate-membership updates.
    auto record_predicate_read(
        base::Xid reader_xid,
        base::RelationId relation_id,
        SsiPredicate predicate
    ) -> base::Status;
    auto copy_page_lock_after_split(
        base::RelationId relation_id,
        base::PageId old_page_id,
        base::PageId new_page_id
    ) -> base::Status;

    // A tuple write probes tuple, page, and relation holders; a page write
    // probes page and relation; a relation target probes only relation.
    auto check_write_conflict(base::Xid writer_xid, SsiReadTarget target) -> base::Status;
    // Matches a mutation's old and/or new logical row values against retained
    // SQL-scan predicates. An UPDATE matches either image, a DELETE supplies
    // only old_values, and an INSERT supplies only new_values.
    auto check_predicate_write_conflict(
        base::Xid writer_xid,
        base::RelationId relation_id,
        std::optional<std::span<const types::Value>> old_values,
        std::optional<std::span<const types::Value>> new_values
    ) -> base::Status;

    auto precommit(base::Xid xid) -> base::Status;
    auto mark_committed(base::Xid xid) -> base::Result<std::uint64_t>;
    // Aborted state and every incident edge are removed immediately.
    auto abort(base::Xid xid) -> base::Status;
    // Releases committed state no older active Serializable snapshot can see.
    void prune_committed();
    auto mark_doomed(base::Xid xid) -> base::Status;

    auto outgoing_conflicts(base::Xid xid) const -> std::vector<base::Xid>;
    auto incoming_conflicts(base::Xid xid) const -> std::vector<base::Xid>;
    auto read_lock_count(base::Xid xid) const -> std::size_t;
    auto predicate_lock_count(base::Xid xid) const -> std::size_t;
    auto target_count() const -> std::size_t;
    auto transaction_count() const -> std::size_t;
    auto snapshot_sequence(base::Xid xid) const -> std::uint64_t;
    auto commit_sequence(base::Xid xid) const -> std::uint64_t;
    auto is_doomed(base::Xid xid) const -> bool;
    auto did_write(base::Xid xid) const -> bool;

private:
    struct Inner;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::transaction

template <>
struct std::hash<db::transaction::SsiReadTarget> {
    auto operator()(const db::transaction::SsiReadTarget& target) const noexcept -> std::size_t {
        auto value = std::hash<db::base::RelationId>{}(target.relation_id);
        value = db::base::combine_hash(value, std::hash<db::base::PageId>{}(target.page_id));
        value = db::base::combine_hash(value, std::hash<db::base::SlotId>{}(target.slot_id));
        return db::base::combine_hash(
            value,
            std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(target.granularity))
        );
    }
};
