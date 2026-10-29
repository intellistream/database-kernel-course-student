#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include "db/engine/session.hpp"

namespace db::transaction {
class TransactionManager;
}  // namespace db::transaction

namespace db::engine::detail {

class CheckpointAdmission;

class SessionFactory final {
public:
    static auto create(
        transaction::TransactionManager& transaction_manager,
        std::shared_ptr<void> database_owner,
        std::shared_ptr<std::atomic_size_t> active_sessions,
        std::shared_ptr<CheckpointAdmission> admission
    ) -> base::Result<Session>;
};

}  // namespace db::engine::detail
