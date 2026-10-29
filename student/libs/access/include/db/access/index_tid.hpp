#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::transaction {
class SerializableConflictManager;
}  // namespace db::transaction

namespace db::access {

enum class IndexTidStateKind : std::uint8_t {
    kLive,
    kRetained,
    kDead,
    kInProgress,
};

// Heap lifecycle state for one RID referenced by an index entry. Retained
// versions do not conflict with the current writer but remain index-visible
// to an older MVCC snapshot; only Dead is removable.
class IndexTidState final {
public:
    static constexpr auto live() noexcept -> IndexTidState {
        return IndexTidState{IndexTidStateKind::kLive, base::kInvalidXid};
    }

    static constexpr auto retained() noexcept -> IndexTidState {
        return IndexTidState{IndexTidStateKind::kRetained, base::kInvalidXid};
    }

    static constexpr auto dead() noexcept -> IndexTidState {
        return IndexTidState{IndexTidStateKind::kDead, base::kInvalidXid};
    }

    static constexpr auto in_progress(base::Xid blocking_xid) noexcept -> IndexTidState {
        return IndexTidState{IndexTidStateKind::kInProgress, blocking_xid};
    }

    constexpr auto kind() const noexcept -> IndexTidStateKind {
        return kind_;
    }

    constexpr auto blocking_xid() const noexcept -> base::Xid {
        return blocking_xid_;
    }

    constexpr auto operator==(const IndexTidState&) const noexcept -> bool = default;

private:
    constexpr IndexTidState(IndexTidStateKind kind, base::Xid blocking_xid) noexcept
        : kind_{kind},
          blocking_xid_{blocking_xid} {}

    IndexTidStateKind kind_;
    base::Xid blocking_xid_;
};

// Consumer-owned port: B+Tree asks heap visibility about RIDs without
// depending on heap page layout or CLOG policy. Escaped implementation
// exceptions are converted at B+Tree and default-batch call boundaries.
class IndexTidClassifier {
public:
    IndexTidClassifier() = default;
    virtual ~IndexTidClassifier() = default;

    virtual auto classify_tid(base::Rid rid) const -> base::Result<IndexTidState> = 0;
    virtual auto
    classify_tids(std::span<const base::Rid> rids, std::vector<IndexTidState>& states) const
        -> base::Status;

protected:
    IndexTidClassifier(const IndexTidClassifier&) = default;
    auto operator=(const IndexTidClassifier&) -> IndexTidClassifier& = default;
    IndexTidClassifier(IndexTidClassifier&&) = default;
    auto operator=(IndexTidClassifier&&) -> IndexTidClassifier& = default;
};

// Transaction wait port used only after B+Tree guards have been released.
// Escaped implementation exceptions become structured access errors.
class UniqueConflictWaiter {
public:
    UniqueConflictWaiter() = default;
    virtual ~UniqueConflictWaiter() = default;

    virtual auto wait_for(base::Xid blocking_xid) const -> base::Status = 0;

protected:
    UniqueConflictWaiter(const UniqueConflictWaiter&) = default;
    auto operator=(const UniqueConflictWaiter&) -> UniqueConflictWaiter& = default;
    UniqueConflictWaiter(UniqueConflictWaiter&&) = default;
    auto operator=(UniqueConflictWaiter&&) -> UniqueConflictWaiter& = default;
};

struct BtreeInsertContext final {
    base::Xid writer_xid{base::kInvalidXid};
    const IndexTidClassifier* tid_classifier{nullptr};
    const UniqueConflictWaiter* unique_waiter{nullptr};
    // Split inheritance affects every active Serializable reader, even when
    // this writer itself runs at a weaker isolation level.
    transaction::SerializableConflictManager* serializable_conflicts{nullptr};
    bool serializable_writer{false};
};

}  // namespace db::access
