#include "command_scope.hpp"

#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/engine/session.hpp"

namespace db::engine::detail {
namespace {

[[maybe_unused]] auto scope_error(std::string message, std::string operation) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)}.with_context(
        "engine.command",
        std::move(operation)
    );
}

}  // namespace

[[maybe_unused]] CommandScope::CommandScope([[maybe_unused]] Session& session) noexcept
    : session_{&session} {
    // LAB-S08.Q07:R01-BEGIN
    base::lab::missing_algorithm("S08.Q07:R01");
    // LAB-S08.Q07:R01-END
}

[[maybe_unused]] CommandScope::CommandScope([[maybe_unused]] CommandScope&& other) noexcept
    : session_{std::exchange(other.session_, nullptr)} {
    // LAB-S08.Q07:R02-BEGIN
    base::lab::missing_algorithm("S08.Q07:R02");
    // LAB-S08.Q07:R02-END
}

CommandScope::~CommandScope() noexcept {
    auto* const session = std::exchange(session_, nullptr);
    if (session == nullptr || !session->command_active()) {
        return;
    }
    try {
        static_cast<void>(session->finish_command(false));
    } catch (...) {  // NOLINT(bugprone-empty-catch) -- destructor containment boundary.
        // A destructor cannot report cleanup failure or allow an exception to
        // cross the API/thread boundary. Normal exits use the explicit methods.
    }
}

[[maybe_unused]] auto CommandScope::finish_success() -> base::Status {
    // LAB-S08.Q07:R03-BEGIN
    base::lab::missing_algorithm("S08.Q07:R03");
    // LAB-S08.Q07:R03-END
}

[[maybe_unused]] auto CommandScope::finish_failure(
    [[maybe_unused]] base::Error primary_error,
    [[maybe_unused]] const std::string_view operation
) -> base::Error {
    // LAB-S08.Q07:R04-BEGIN
    base::lab::missing_algorithm("S08.Q07:R04");
    // LAB-S08.Q07:R04-END
}

[[maybe_unused]] auto start_command_scope([[maybe_unused]] Session& session)
    -> base::Result<CommandScope> {
    // LAB-S08.Q07:R05-BEGIN
    base::lab::missing_algorithm("S08.Q07:R05");
    // LAB-S08.Q07:R05-END
}
}  // namespace db::engine::detail
