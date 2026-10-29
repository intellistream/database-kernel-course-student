#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

#include "db/base/result.hpp"
#include "db/engine/dispatch.hpp"
#include "db/engine/session.hpp"
#include "db/sql/bound_ast.hpp"

#include "physical_services.hpp"

namespace db::engine::detail {

// The database composition root owns these services and controls their
// lifetime. Utility execution borrows the common physical bundle plus its
// serialization/checkpoint policy without exposing relation types publicly.
struct UtilityExecutionContext final {
    PhysicalServices services;
    std::mutex& utility_mutex;
    std::function<base::Status()> checkpoint;
};

auto execute_utility(
    UtilityExecutionContext context,
    Session& session,
    sql::BoundStatement statement,
    std::uint64_t expected_generation
) -> base::Result<DispatchExecutionResult>;

}  // namespace db::engine::detail
