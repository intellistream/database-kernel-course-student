#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "prepared.hpp"
#include "transaction_common.hpp"
#include "transaction_internal.hpp"

namespace db::tpcc {
namespace {

auto attempt(
    SqlSession& session,
    const RouteSelection route,
    const DatasetScale scale,
    const std::uint32_t threshold
) -> base::Result<AttemptResult> {
    const std::vector<protocol::RmdbBatchOperation> first{
        operation(statement::kBegin),
        operation(
            statement::kStockLevelNextOrder,
            {integer(route.home_warehouse), integer(route.district)}
        ),
    };
    auto first_response = session.batch(first);
    if (!first_response) {
        return base::fail(std::move(first_response).error());
    }
    if (!first_response->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    auto next_order = scalar_i32(**first_response, 1U);
    if (!next_order) {
        return base::fail(std::move(next_order).error());
    }
    if (next_order.value() <= 0) {
        return transaction_error("StockLevel next order id was invalid");
    }

    const std::vector<protocol::RmdbBatchOperation> second{
        operation(
            statement::kStockLevelCount,
            {
                integer(route.home_warehouse),
                integer(route.district),
                signed_integer(next_order.value()),
                signed_integer(std::max<std::int32_t>(1, next_order.value() - 20)),
                integer(threshold),
            }
        ),
        operation(statement::kCommit),
    };
    auto second_response = session.batch(second);
    if (!second_response) {
        return base::fail(std::move(second_response).error());
    }
    if (!second_response->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    auto count = scalar_i32(**second_response, 0U);
    if (!count) {
        return base::fail(std::move(count).error());
    }
    if (count.value() < 0 || std::cmp_greater(count.value(), std::min(scale.items, 300U))) {
        return transaction_error("StockLevel count was outside the distinct-item bound");
    }
    return AttemptResult{.disposition = AttemptDisposition::kCommitted};
}

}  // namespace

auto run_stock_level(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData> {
    const std::uint32_t threshold = execution.generator.value(10U, 20U);
    return retry_transaction(
        execution.session,
        TransactionKind::kStockLevel,
        execution.retry_limit,
        counters,
        [&execution, threshold]() {
            return attempt(execution.session, execution.route, execution.scale, threshold);
        }
    );
}

}  // namespace db::tpcc
