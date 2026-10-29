#pragma once

#include "db/engine/session.hpp"

namespace db::transaction {
class TransactionBackend;
struct Snapshot;
}  // namespace db::transaction

namespace db::engine::detail {

// Private composition-root access to the transaction state captured by the
// current command. The public Session facade remains independent of storage,
// WAL, and transaction implementation types.
class SessionAccess final {
public:
    static auto backend(Session& session) noexcept -> transaction::TransactionBackend*;
    static auto command_snapshot(const Session& session) noexcept -> const transaction::Snapshot*;
};

}  // namespace db::engine::detail
