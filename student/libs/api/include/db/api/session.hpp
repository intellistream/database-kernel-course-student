#pragma once

#include <cstdint>
#include <memory>

#include "db/base/result.hpp"

namespace db::api {

class Database;

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

// Stable embedded transaction facade. Its implementation owns the engine
// session, so no transaction backend, snapshot, XID, or engine type appears in
// this header or in the public object layout.
class Session final {
public:
    Session(Session&& other) noexcept;
    auto operator=(Session&& other) noexcept -> Session&;
    Session(const Session&) = delete;
    auto operator=(const Session&) -> Session& = delete;
    ~Session();

    // False after close or on the inert source left by a move.
    auto valid() const noexcept -> bool;

    auto state() const noexcept -> SessionState;
    auto isolation() const noexcept -> SessionIsolation;
    auto command_active() const noexcept -> bool;

    auto set_isolation(SessionIsolation isolation) -> base::Status;
    auto begin() -> base::Status;
    auto commit() -> base::Status;
    auto rollback() -> base::Status;
    auto start_command() -> base::Status;
    auto finish_command(bool succeeded) -> base::Status;
    auto close() -> base::Status;

private:
    struct Impl;

    explicit Session(std::unique_ptr<Impl> implementation) noexcept;

    friend class Database;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::api
