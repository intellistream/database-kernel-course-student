#include "transaction.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "array_access.hpp"
#include "transaction_internal.hpp"

namespace db::tpcc {

auto TransactionRecord::completed() const noexcept -> bool {
    return completion == TransactionCompletion::kCommitted
           || completion == TransactionCompletion::kExpectedRollback;
}

auto execute_transaction(const TransactionKind kind, const TransactionExecution& execution)
    -> base::Result<TransactionRecord> {
    TransactionRecord record;
    ++array_element(record.counters.attempted, transaction_index(kind));

    base::Result<CompletionData> completion = [&]() -> base::Result<CompletionData> {
        switch (kind) {
            case TransactionKind::kNewOrder:
                return run_new_order(execution, record.counters);
            case TransactionKind::kPayment:
                return run_payment(execution, record.counters);
            case TransactionKind::kOrderStatus:
                return run_order_status(execution, record.counters);
            case TransactionKind::kDelivery:
                return run_delivery(execution, record.counters);
            case TransactionKind::kStockLevel:
                return run_stock_level(execution, record.counters);
        }
        std::unreachable();
    }();
    if (!completion) {
        return base::fail(std::move(completion).error());
    }
    record.completion = completion->completion;
    return record;
}

}  // namespace db::tpcc
