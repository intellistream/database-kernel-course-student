#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/recovery/dispatcher.hpp"

namespace db::transaction {
class CommitLog;
}  // namespace db::transaction

namespace db::wal {
struct DecodedWalRecord;
}  // namespace db::wal

namespace db::recovery {

// Recovery deliberately owns this small horizon value instead of leaking the
// steady-state transaction manager's construction types into the startup
// replay boundary. It is the durable input needed to rebuild that manager
// after CLOG replay has completed.
struct DurableStateReplaySeed final {
    base::Xid next_xid{base::Xid{1U}};
    base::Xid latest_completed_xid{base::kInvalidXid};
    std::uint32_t next_object_id{1U};

    auto operator==(const DurableStateReplaySeed&) const noexcept -> bool = default;
};

struct DurableStateReplayResult final {
    base::Xid next_xid{base::Xid{1U}};
    base::Xid latest_completed_xid{base::kInvalidXid};
    std::uint32_t next_object_id{1U};
    base::Lsn last_lsn{base::kInvalidLsn};
    std::size_t transaction_records_replayed{0U};

    auto operator==(const DurableStateReplayResult&) const noexcept -> bool = default;
};

enum class DurableStateReplayDisposition : std::uint8_t {
    kApplied,
    kIgnored,
};

// Recovery requires only the durable object-ID frontier. Catalog relation and
// index discovery remain a separate immutable-snapshot concern, so this port
// cannot accidentally become a mutable catalog service locator.
class CatalogRecoveryPort {
public:
    CatalogRecoveryPort() = default;
    CatalogRecoveryPort(const CatalogRecoveryPort&) = delete;
    auto operator=(const CatalogRecoveryPort&) -> CatalogRecoveryPort& = delete;
    CatalogRecoveryPort(CatalogRecoveryPort&&) = delete;
    auto operator=(CatalogRecoveryPort&&) -> CatalogRecoveryPort& = delete;
    virtual ~CatalogRecoveryPort() = default;

    virtual auto ensure_next_object_id(std::uint32_t next_object_id) -> base::Status = 0;
};

// Single-threaded startup replay of non-page durable state. The caller owns
// the CLOG and catalog port for the complete replayer lifetime. Page routes
// are semantically dispatched but deliberately left untouched: the startup
// runner supplies their bounded cache/writeback attachment.
class DurableStateReplayer final {
public:
    static auto create(
        DurableStateReplaySeed seed,
        transaction::CommitLog& commit_log,
        CatalogRecoveryPort& catalog
    ) -> base::Result<DurableStateReplayer>;

    DurableStateReplayer(const DurableStateReplayer&) = delete;
    auto operator=(const DurableStateReplayer&) -> DurableStateReplayer& = delete;
    DurableStateReplayer(DurableStateReplayer&& other) noexcept;
    auto operator=(DurableStateReplayer&& other) noexcept -> DurableStateReplayer&;
    ~DurableStateReplayer() = default;

    // Moving transfers the exclusive replay cursor and both borrowed service
    // bindings. The source becomes inert and rejects replay/finalize with a
    // structured kUnavailable error.
    auto valid() const noexcept -> bool;

    // Records must be supplied in nondecreasing physical WAL order. A final
    // result seals the replayer; callers create a new instance for another
    // recovery pass rather than mixing streams.
    auto replay(const wal::DecodedWalRecord& decoded)
        -> base::Result<DurableStateReplayDisposition>;

    // Completes all gaps below the recovered horizon as aborted, durably
    // flushes CLOG, and reconciles the FCAT OID frontier. It is retry-safe:
    // after a failure a caller may invoke it again, and after success it
    // returns the same cached result without republishing either service.
    auto finalize() -> base::Result<DurableStateReplayResult>;

private:
    DurableStateReplayer(
        DurableStateReplaySeed seed,
        transaction::CommitLog& commit_log,
        CatalogRecoveryPort& catalog
    ) noexcept;

    transaction::CommitLog* commit_log_;
    CatalogRecoveryPort* catalog_;
    RecoveryDispatcher dispatcher_;
    base::Xid recovered_next_xid_;
    std::uint32_t recovered_next_object_id_;
    base::Lsn last_lsn_{base::kInvalidLsn};
    std::size_t transaction_records_replayed_{0U};
    std::optional<DurableStateReplayResult> finalized_result_;
};

}  // namespace db::recovery
