#pragma once

#include <string_view>

#include "db/base/result.hpp"

namespace db::engine {

class Session;

namespace detail {

// Owns exactly one started Session command. Explicit completion reports every
// transaction-service error; destruction is only a no-throw rollback guard for
// an exceptional or otherwise unhandled exit.
class CommandScope final {
public:
    CommandScope(CommandScope&& other) noexcept;
    auto operator=(CommandScope&& other) -> CommandScope& = delete;
    CommandScope(const CommandScope&) = delete;
    auto operator=(const CommandScope&) -> CommandScope& = delete;
    ~CommandScope() noexcept;

    auto finish_success() -> base::Status;
    auto finish_failure(base::Error primary_error, std::string_view operation) -> base::Error;

private:
    friend auto start_command_scope(Session& session) -> base::Result<CommandScope>;

    explicit CommandScope(Session& session) noexcept;

    Session* session_;
};

auto start_command_scope(Session& session) -> base::Result<CommandScope>;

}  // namespace detail
}  // namespace db::engine
