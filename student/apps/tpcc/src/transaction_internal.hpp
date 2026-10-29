#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include "db/base/result.hpp"

#include "array_access.hpp"
#include "transaction.hpp"

namespace db::tpcc {

enum class AttemptDisposition : std::uint8_t {
    kCommitted,
    kExpectedRollback,
    kAborted,
};

struct AttemptResult final {
    AttemptDisposition disposition{AttemptDisposition::kAborted};
    std::uint32_t delivery_processed_orders{0U};
};

struct CompletionData final {
    TransactionCompletion completion{TransactionCompletion::kAbandoned};
};

template <typename Attempt>
auto retry_transaction(
    SqlSession& session,
    const TransactionKind kind,
    const std::uint32_t retry_limit,
    TransactionCounters& counters,
    Attempt attempt
) -> base::Result<CompletionData> {
    const std::size_t index = transaction_index(kind);
    for (std::uint32_t number = 0U; number < retry_limit; ++number) {
        auto result = attempt();
        if (!result) {
            return base::fail(std::move(result).error());
        }
        if (session.deadline_stopped()) {
            return CompletionData{.completion = TransactionCompletion::kDeadlineStopped};
        }
        if (result->disposition == AttemptDisposition::kCommitted) {
            ++array_element(counters.committed, index);
            counters.delivery_processed_orders += result->delivery_processed_orders;
            return CompletionData{.completion = TransactionCompletion::kCommitted};
        }
        if (result->disposition == AttemptDisposition::kExpectedRollback) {
            ++counters.expected_rollbacks;
            return CompletionData{.completion = TransactionCompletion::kExpectedRollback};
        }
        ++array_element(counters.database_aborts, index);
        if (number + 1U < retry_limit) {
            ++counters.retries;
        }
    }
    ++array_element(counters.abandoned, index);
    return CompletionData{.completion = TransactionCompletion::kAbandoned};
}

auto run_new_order(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData>;

auto run_payment(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData>;

auto run_order_status(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData>;

auto run_delivery(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData>;

auto run_stock_level(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData>;

}  // namespace db::tpcc
