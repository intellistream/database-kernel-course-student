#include "db/transaction/state.hpp"

#include <limits>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::transaction {
namespace {

[[maybe_unused]] auto state_error(std::string message, std::string operation) -> base::Error {
    return base::Error{base::ErrorCode::kTransactionState, std::move(message)}.with_context(
        "transaction",
        std::move(operation)
    );
}

[[maybe_unused]] auto range_error(std::string message, std::string operation) -> base::Error {
    return base::Error{base::ErrorCode::kOutOfRange, std::move(message)}.with_context(
        "transaction",
        std::move(operation)
    );
}

[[maybe_unused]] auto uses_transaction_snapshot([[maybe_unused]] IsolationLevel isolation) noexcept
    -> bool {
    // LAB-S09.Q02:R04-BEGIN
    base::lab::missing_algorithm("S09.Q02:R04");
    // LAB-S09.Q02:R04-END
}

}  // namespace

auto isolation_level_name(IsolationLevel isolation) noexcept -> std::string_view {
    switch (isolation) {
        case IsolationLevel::kReadCommitted:
            return "read committed";
        case IsolationLevel::kRepeatableRead:
            return "repeatable read";
        case IsolationLevel::kSerializable:
            return "serializable";
    }
    return "unknown";
}

auto transaction_block_state_name(TransactionBlockState state) noexcept -> std::string_view {
    switch (state) {
        case TransactionBlockState::kIdle:
            return "idle";
        case TransactionBlockState::kInTransaction:
            return "in transaction";
        case TransactionBlockState::kFailedTransaction:
            return "failed transaction";
    }
    return "unknown";
}

auto TransactionStateMachine::state() const noexcept -> TransactionBlockState {
    return state_;
}

auto TransactionStateMachine::scope() const noexcept -> TransactionScope {
    return scope_;
}

auto TransactionStateMachine::isolation() const noexcept -> IsolationLevel {
    return state_ == TransactionBlockState::kIdle ? default_isolation_ : active_isolation_;
}

auto TransactionStateMachine::default_isolation() const noexcept -> IsolationLevel {
    return default_isolation_;
}

auto TransactionStateMachine::transaction_epoch() const noexcept -> std::uint64_t {
    return transaction_epoch_;
}

auto TransactionStateMachine::command_id() const noexcept -> std::uint32_t {
    return command_id_;
}

auto TransactionStateMachine::command_active() const noexcept -> bool {
    return command_active_;
}

auto TransactionStateMachine::snapshot_fixed() const noexcept -> bool {
    return snapshot_fixed_;
}

[[maybe_unused]] auto
TransactionStateMachine::set_isolation([[maybe_unused]] IsolationLevel isolation) -> base::Status {
    // LAB-S09.Q02:R05-BEGIN
    base::lab::missing_algorithm("S09.Q02:R05");
    // LAB-S09.Q02:R05-END
}

[[maybe_unused]] auto TransactionStateMachine::begin_explicit() -> base::Result<TransactionStart> {
    // LAB-S08.Q01:R01-BEGIN
    base::lab::missing_algorithm("S08.Q01:R01");
    // LAB-S08.Q01:R01-END
}

[[maybe_unused]] auto TransactionStateMachine::start_command() -> base::Result<CommandStart> {
    // LAB-S08.Q01:R02-BEGIN
    base::lab::missing_algorithm("S08.Q01:R02");
    // LAB-S08.Q01:R02-END
}

[[maybe_unused]] auto
TransactionStateMachine::finish_command([[maybe_unused]] CommandOutcome outcome)
    -> base::Result<CommandFinish> {
    // LAB-S08.Q01:R03-BEGIN
    base::lab::missing_algorithm("S08.Q01:R03");
    // LAB-S08.Q01:R03-END
}

[[maybe_unused]] auto
TransactionStateMachine::preview_finish_command([[maybe_unused]] const CommandOutcome outcome) const
    -> base::Result<CommandFinish> {
    // LAB-S08.Q01:R04-BEGIN
    base::lab::missing_algorithm("S08.Q01:R04");
    // LAB-S08.Q01:R04-END
}

[[maybe_unused]] auto TransactionStateMachine::commit_explicit()
    -> base::Result<TransactionDisposition> {
    // LAB-S08.Q01:R05-BEGIN
    base::lab::missing_algorithm("S08.Q01:R05");
    // LAB-S08.Q01:R05-END
}

[[maybe_unused]] auto TransactionStateMachine::validate_commit_explicit() const -> base::Status {
    // LAB-S08.Q01:R06-BEGIN
    base::lab::missing_algorithm("S08.Q01:R06");
    // LAB-S08.Q01:R06-END
}

[[maybe_unused]] auto TransactionStateMachine::rollback_explicit()
    -> base::Result<TransactionDisposition> {
    // LAB-S08.Q01:R07-BEGIN
    base::lab::missing_algorithm("S08.Q01:R07");
    // LAB-S08.Q01:R07-END
}

[[maybe_unused]] auto TransactionStateMachine::validate_rollback_explicit() const -> base::Status {
    // LAB-S08.Q01:R08-BEGIN
    base::lab::missing_algorithm("S08.Q01:R08");
    // LAB-S08.Q01:R08-END
}

[[maybe_unused]] void TransactionStateMachine::apply_finish_command(
    [[maybe_unused]] const CommandOutcome outcome
) noexcept {
    // LAB-S08.Q01:R09-BEGIN
    base::lab::missing_algorithm("S08.Q01:R09");
    // LAB-S08.Q01:R09-END
}

[[maybe_unused]] void TransactionStateMachine::apply_explicit_completion() noexcept {
    // LAB-S08.Q01:R10-BEGIN
    base::lab::missing_algorithm("S08.Q01:R10");
    // LAB-S08.Q01:R10-END
}

[[maybe_unused]] void TransactionStateMachine::mark_explicit_failed() noexcept {
    // LAB-S08.Q01:R11-BEGIN
    base::lab::missing_algorithm("S08.Q01:R11");
    // LAB-S08.Q01:R11-END
}

[[maybe_unused]] auto TransactionStateMachine::advance_epoch() -> base::Status {
    // LAB-S08.Q01:R12-BEGIN
    base::lab::missing_algorithm("S08.Q01:R12");
    // LAB-S08.Q01:R12-END
}

[[maybe_unused]] void TransactionStateMachine::reset_to_idle() noexcept {
    // LAB-S08.Q01:R13-BEGIN
    base::lab::missing_algorithm("S08.Q01:R13");
    // LAB-S08.Q01:R13-END
}

}  // namespace db::transaction
