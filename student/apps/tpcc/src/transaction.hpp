#pragma once

#include <cstdint>
#include <span>

#include "db/base/result.hpp"

#include "config.hpp"
#include "metrics.hpp"
#include "session.hpp"
#include "workload.hpp"

namespace db::tpcc {

enum class TransactionCompletion : std::uint8_t {
    kCommitted,
    kExpectedRollback,
    kAbandoned,
    kDeadlineStopped,
};

struct TransactionRecord final {
    TransactionCompletion completion{TransactionCompletion::kAbandoned};
    TransactionCounters counters{};

    auto completed() const noexcept -> bool;
};

struct TransactionExecution final {
    SqlSession& session;
    TransactionGenerator& generator;
    RouteSelection route{};
    DatasetScale scale{};
    RoutingConfig routing{};
    std::span<const std::uint32_t> hot_items;
    std::uint32_t retry_limit{0U};
};

auto execute_transaction(TransactionKind kind, const TransactionExecution& execution)
    -> base::Result<TransactionRecord>;

}  // namespace db::tpcc
