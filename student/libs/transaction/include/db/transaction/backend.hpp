#pragma once

#include <memory>
#include <optional>
#include <unordered_map>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/state.hpp"
#include "db/transaction/visibility.hpp"

namespace db::transaction {

class ProcSlot;
class TransactionManager;

// Session-local access-method hints. These values are deliberately neither
// shared nor transactional: a stale page hint is always rechecked by the heap
// access method, and conditional clearing avoids erasing a newer hint.
class BackendLocalState final {
public:
    auto heap_target(base::RelationId relation_id) const -> std::optional<base::PageId>;
    auto set_heap_target(base::RelationId relation_id, base::PageId page_id) -> base::Status;
    auto clear_heap_target(base::RelationId relation_id, base::PageId page_id) -> base::Status;
    auto heap_target_count() const noexcept -> std::size_t;

private:
    std::unordered_map<base::RelationId, base::PageId> heap_targets_;
};

struct TransactionBackendOptions final {
    // False is conservative for low-level callers that can mutate storage
    // without reporting it. A frontend that routes every durable mutation
    // through mark_persistent_write() may opt into precise tracking.
    bool track_persistent_writes{false};

    auto operator==(const TransactionBackendOptions&) const -> bool = default;
};

struct BackendTransactionStart final {
    TransactionStart transition{};
    base::Xid xid{base::kInvalidXid};
    std::optional<Snapshot> snapshot;
};

struct BackendCommandStart final {
    CommandStart transition{};
    base::Xid xid{base::kInvalidXid};
    Snapshot snapshot;
};

// Per-session composition boundary for the pure state machine and shared
// transaction services. TransactionManager must outlive this non-owning
// facade. One stable ProcSlotLease is retained across transaction boundaries.
class TransactionBackend final {
public:
    ~TransactionBackend();
    TransactionBackend(const TransactionBackend&) = delete;
    auto operator=(const TransactionBackend&) -> TransactionBackend& = delete;
    TransactionBackend(TransactionBackend&&) = delete;
    auto operator=(TransactionBackend&&) -> TransactionBackend& = delete;

    static auto create(TransactionManager& manager, TransactionBackendOptions options = {})
        -> base::Result<std::unique_ptr<TransactionBackend>>;

    auto state() const noexcept -> TransactionBlockState;
    auto scope() const noexcept -> TransactionScope;
    auto isolation() const noexcept -> IsolationLevel;
    auto command_active() const noexcept -> bool;
    auto xid() const noexcept -> base::Xid;
    auto has_persistent_writes() const noexcept -> bool;
    // Null only after close(); the observer remains owned by the retained
    // ProcSlotLease and must not outlive this backend.
    auto proc_slot() const noexcept -> const ProcSlot*;
    auto local_state() noexcept -> BackendLocalState&;
    auto local_state() const noexcept -> const BackendLocalState&;

    auto set_isolation(IsolationLevel isolation) -> base::Status;
    auto begin_explicit() -> base::Result<BackendTransactionStart>;
    auto start_command() -> base::Result<BackendCommandStart>;
    auto finish_command(CommandOutcome outcome) -> base::Result<CommandFinish>;
    auto commit_explicit() -> base::Status;
    auto rollback_explicit() -> base::Status;
    auto mark_persistent_write() -> base::Status;
    // Marks the active transaction as a caller-owned directly durable write.
    // It is mutually exclusive with ordinary WAL-backed writes. The caller
    // must persist data and establish its recovery boundary before commit.
    auto mark_unlogged_write() -> base::Status;

    // Only an idle backend can unregister. Destroying a live backend leaves an
    // abandoned visible slot for TransactionManager recovery.
    auto close() -> base::Status;

private:
    struct Inner;

    explicit TransactionBackend(std::unique_ptr<Inner> inner) noexcept;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::transaction
