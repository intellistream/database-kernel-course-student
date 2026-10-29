#pragma once

#include <cstdint>
#include <string_view>

#include "db/base/result.hpp"

namespace db::transaction {

class TransactionBackend;

enum class IsolationLevel : std::uint8_t {
    kReadCommitted,
    kRepeatableRead,
    kSerializable,
};

enum class TransactionBlockState : std::uint8_t {
    kIdle,
    kInTransaction,
    kFailedTransaction,
};

enum class TransactionScope : std::uint8_t {
    kNone,
    kImplicit,
    kExplicit,
};

enum class SnapshotAction : std::uint8_t {
    kNone,
    kCaptureCommand,
    kCaptureTransaction,
    kReuseTransaction,
};

enum class CommandOutcome : std::uint8_t {
    kSucceeded,
    kFailed,
};

enum class TransactionDisposition : std::uint8_t {
    kNone,
    kCommit,
    kAbort,
};

auto isolation_level_name(IsolationLevel isolation) noexcept -> std::string_view;
auto transaction_block_state_name(TransactionBlockState state) noexcept -> std::string_view;

struct TransactionStart final {
    std::uint64_t epoch;
    IsolationLevel isolation;
    SnapshotAction snapshot_action;

    auto operator==(const TransactionStart&) const -> bool = default;
};

struct CommandStart final {
    std::uint64_t transaction_epoch;
    std::uint32_t command_id;
    IsolationLevel isolation;
    TransactionScope scope;
    SnapshotAction snapshot_action;
    bool starts_transaction;

    auto operator==(const CommandStart&) const -> bool = default;
};

struct CommandFinish final {
    TransactionDisposition disposition;
    TransactionBlockState state;

    auto operator==(const CommandFinish&) const -> bool = default;
};

// Pure per-session transaction-block state. XID allocation, snapshots, CLOG,
// locks, SSI, and durable completion are supplied by collaborating services.
// The owner must perform any returned commit/abort disposition before exposing
// a successful frontend result.
class TransactionStateMachine final {
public:
    TransactionStateMachine() = default;
    ~TransactionStateMachine() = default;
    TransactionStateMachine(const TransactionStateMachine&) = delete;
    auto operator=(const TransactionStateMachine&) -> TransactionStateMachine& = delete;
    TransactionStateMachine(TransactionStateMachine&&) = delete;
    auto operator=(TransactionStateMachine&&) -> TransactionStateMachine& = delete;

    auto state() const noexcept -> TransactionBlockState;
    auto scope() const noexcept -> TransactionScope;
    auto isolation() const noexcept -> IsolationLevel;
    auto default_isolation() const noexcept -> IsolationLevel;
    auto transaction_epoch() const noexcept -> std::uint64_t;
    auto command_id() const noexcept -> std::uint32_t;
    auto command_active() const noexcept -> bool;
    auto snapshot_fixed() const noexcept -> bool;

    // While idle, changes the connection default. Inside an explicit block,
    // changes only that block and only before its first data snapshot.
    auto set_isolation(IsolationLevel isolation) -> base::Status;

    auto begin_explicit() -> base::Result<TransactionStart>;
    auto start_command() -> base::Result<CommandStart>;
    auto finish_command(CommandOutcome outcome) -> base::Result<CommandFinish>;
    auto commit_explicit() -> base::Result<TransactionDisposition>;
    auto rollback_explicit() -> base::Result<TransactionDisposition>;

private:
    friend class TransactionBackend;

    auto preview_finish_command(CommandOutcome outcome) const -> base::Result<CommandFinish>;
    auto validate_commit_explicit() const -> base::Status;
    auto validate_rollback_explicit() const -> base::Status;
    void apply_finish_command(CommandOutcome outcome) noexcept;
    void apply_explicit_completion() noexcept;
    void mark_explicit_failed() noexcept;
    auto advance_epoch() -> base::Status;
    void reset_to_idle() noexcept;

    TransactionBlockState state_{TransactionBlockState::kIdle};
    TransactionScope scope_{TransactionScope::kNone};
    IsolationLevel default_isolation_{IsolationLevel::kReadCommitted};
    IsolationLevel active_isolation_{IsolationLevel::kReadCommitted};
    std::uint64_t transaction_epoch_{0};
    std::uint32_t command_id_{0};
    bool command_active_{false};
    bool isolation_locked_{false};
    bool snapshot_fixed_{false};
};

}  // namespace db::transaction
