#pragma once

#include <cstdint>
#include <memory>

#include "db/base/result.hpp"

namespace db::engine {

class Database;

namespace detail {
class SessionFactory;
class SessionAccess;
}  // namespace detail

enum class SessionState : std::uint8_t {
    kIdle,
    kInTransaction,
    kFailedTransaction,
};

enum class SessionIsolation : std::uint8_t {
    kReadCommitted,
    kRepeatableRead,
    kSerializable,
};

// A session is the engine-owned transaction facade used by SQL dispatch. It
// intentionally exposes neither TransactionBackend nor its XID, snapshot,
// ProcSlot, WAL, or catalog dependencies. An idle command is
// implicit/autocommit, while a command inside BEGIN/COMMIT is part of the
// explicit block.
class Session final {
public:
    Session(Session&& other) noexcept;
    auto operator=(Session&& other) noexcept -> Session&;
    Session(const Session&) = delete;
    auto operator=(const Session&) -> Session& = delete;
    ~Session();

    // False after close or on the inert source left by a move. Observers then
    // return their idle defaults and work operations return kUnavailable.
    auto valid() const noexcept -> bool;

    auto state() const noexcept -> SessionState;
    auto isolation() const noexcept -> SessionIsolation;
    auto command_active() const noexcept -> bool;

    // Idle changes the connection default. An explicit block may change its
    // isolation only before its first command snapshot. Failed blocks reject
    // all commands except rollback.
    auto set_isolation(SessionIsolation isolation) -> base::Status;

    auto begin() -> base::Status;
    auto commit() -> base::Status;
    auto rollback() -> base::Status;

    // Start/finish is a narrow dispatch port, not a SQL parser API. A
    // successful implicit command commits; a failed implicit command aborts.
    // A failed explicit command retains the failed-transaction state until
    // rollback().
    auto start_command() -> base::Status;
    auto finish_command(bool succeeded) -> base::Status;

    // Releases the session's ProcArray lease. Active or failed transactions
    // must be rolled back first. The operation is idempotent after success.
    auto close() -> base::Status;

private:
    struct Impl;

    explicit Session(std::unique_ptr<Impl> implementation) noexcept;
    void release() noexcept;

    auto belongs_to(const std::shared_ptr<void>& database_owner) const noexcept -> bool;

    friend class Database;
    friend class detail::SessionFactory;
    friend class detail::SessionAccess;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::engine
