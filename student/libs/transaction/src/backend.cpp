#include "db/transaction/backend.hpp"

#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/xid.hpp"

#include "backend_transaction_services.hpp"

namespace db::transaction {

[[maybe_unused]] auto
BackendLocalState::heap_target([[maybe_unused]] const base::RelationId relation_id) const
    -> std::optional<base::PageId> {
    // LAB-S08.Q07:R18-BEGIN
    base::lab::missing_algorithm("S08.Q07:R18");
    // LAB-S08.Q07:R18-END
}

[[maybe_unused]] auto BackendLocalState::set_heap_target(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const base::PageId page_id
) -> base::Status {
    // LAB-S08.Q07:R19-BEGIN
    base::lab::missing_algorithm("S08.Q07:R19");
    // LAB-S08.Q07:R19-END
}

auto BackendLocalState::clear_heap_target(
    const base::RelationId relation_id,
    const base::PageId page_id
) -> base::Status {
    if (relation_id == base::kInvalidRelationId || page_id == base::kInvalidPageId) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "heap target requires valid relation and page identifiers"
            }
        );
    }
    const auto target = heap_targets_.find(relation_id);
    if (target != heap_targets_.end() && target->second == page_id) {
        heap_targets_.erase(target);
    }
    return {};
}

auto BackendLocalState::heap_target_count() const noexcept -> std::size_t {
    return heap_targets_.size();
}

struct TransactionBackend::Inner final {
    [[maybe_unused]] Inner(
        [[maybe_unused]] TransactionManager& manager,
        [[maybe_unused]] ProcSlotLease lease,
        [[maybe_unused]] const TransactionBackendOptions options
    ) noexcept
        : services{manager, std::move(lease), options} {
        // LAB-S08.Q07:R20-BEGIN
        base::lab::missing_algorithm("S08.Q07:R20");
        // LAB-S08.Q07:R20-END
    }

    detail::BackendTransactionServices services;
    TransactionStateMachine state_machine;
    BackendLocalState local_state;
};

[[maybe_unused]] TransactionBackend::TransactionBackend(
    [[maybe_unused]] std::unique_ptr<Inner> inner
) noexcept
    : inner_{std::move(inner)} {
    // LAB-S08.Q07:R21-BEGIN
    base::lab::missing_algorithm("S08.Q07:R21");
    // LAB-S08.Q07:R21-END
}

TransactionBackend::~TransactionBackend() = default;

[[maybe_unused]] auto TransactionBackend::create(
    [[maybe_unused]] TransactionManager& manager,
    [[maybe_unused]] TransactionBackendOptions options
) -> base::Result<std::unique_ptr<TransactionBackend>> {
    // LAB-S08.Q07:R22-BEGIN
    base::lab::missing_algorithm("S08.Q07:R22");
    // LAB-S08.Q07:R22-END
}

auto TransactionBackend::state() const noexcept -> TransactionBlockState {
    return inner_->state_machine.state();
}

auto TransactionBackend::scope() const noexcept -> TransactionScope {
    return inner_->state_machine.scope();
}

auto TransactionBackend::isolation() const noexcept -> IsolationLevel {
    return inner_->state_machine.isolation();
}

auto TransactionBackend::command_active() const noexcept -> bool {
    return inner_->state_machine.command_active();
}

auto TransactionBackend::xid() const noexcept -> base::Xid {
    return inner_->services.xid();
}

auto TransactionBackend::has_persistent_writes() const noexcept -> bool {
    return inner_->services.has_persistent_writes();
}

auto TransactionBackend::proc_slot() const noexcept -> const ProcSlot* {
    return inner_->services.proc_slot();
}

auto TransactionBackend::local_state() noexcept -> BackendLocalState& {
    return inner_->local_state;
}

auto TransactionBackend::local_state() const noexcept -> const BackendLocalState& {
    return inner_->local_state;
}

[[maybe_unused]] auto TransactionBackend::set_isolation([[maybe_unused]] IsolationLevel isolation)
    -> base::Status {
    // LAB-S08.Q07:R23-BEGIN
    base::lab::missing_algorithm("S08.Q07:R23");
    // LAB-S08.Q07:R23-END
}

[[maybe_unused]] auto TransactionBackend::begin_explicit()
    -> base::Result<BackendTransactionStart> {
    // LAB-S08.Q07:R24-BEGIN
    base::lab::missing_algorithm("S08.Q07:R24");
    // LAB-S08.Q07:R24-END
}

[[maybe_unused]] auto TransactionBackend::start_command() -> base::Result<BackendCommandStart> {
    // LAB-S08.Q07:R25-BEGIN
    base::lab::missing_algorithm("S08.Q07:R25");
    // LAB-S08.Q07:R25-END
}

[[maybe_unused]] auto TransactionBackend::finish_command([[maybe_unused]] CommandOutcome outcome)
    -> base::Result<CommandFinish> {
    // LAB-S08.Q07:R26-BEGIN
    base::lab::missing_algorithm("S08.Q07:R26");
    // LAB-S08.Q07:R26-END
}

[[maybe_unused]] auto TransactionBackend::commit_explicit() -> base::Status {
    // LAB-S08.Q07:R27-BEGIN
    base::lab::missing_algorithm("S08.Q07:R27");
    // LAB-S08.Q07:R27-END
}

[[maybe_unused]] auto TransactionBackend::rollback_explicit() -> base::Status {
    // LAB-S08.Q07:R28-BEGIN
    base::lab::missing_algorithm("S08.Q07:R28");
    // LAB-S08.Q07:R28-END
}

[[maybe_unused]] auto TransactionBackend::mark_persistent_write() -> base::Status {
    // LAB-S08.Q07:R29-BEGIN
    base::lab::missing_algorithm("S08.Q07:R29");
    // LAB-S08.Q07:R29-END
}

[[maybe_unused]] auto TransactionBackend::mark_unlogged_write() -> base::Status {
    // LAB-S08.Q07:R30-BEGIN
    base::lab::missing_algorithm("S08.Q07:R30");
    // LAB-S08.Q07:R30-END
}

auto TransactionBackend::close() -> base::Status {
    return inner_->services.close();
}

}  // namespace db::transaction
