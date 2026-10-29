#pragma once

#include "db/engine/dispatch.hpp"

#include "physical_services.hpp"

namespace db::engine::detail {

// The storage-backed adapter is owned by the engine composition root. It
// consumes only a planned statement and a live command snapshot; parser,
// binder and planner state never crosses into the physical ports.
struct QueryExecutionContext final {
    PhysicalServices services;
    Session& session;
};

class StorageExecutionPort final : public DispatchExecutionPort {
public:
    explicit StorageExecutionPort(QueryExecutionContext context) noexcept
        : context_{context} {}

    auto execute(const DispatchPlan& plan, std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> override;

private:
    QueryExecutionContext context_;
};

}  // namespace db::engine::detail
