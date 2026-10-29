#include "metrics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "array_access.hpp"
#include "prepared.hpp"

namespace db::tpcc {
namespace {

enum class BatchKind : std::size_t {
    kNewOrderRead,
    kNewOrderWrite,
    kPaymentRead,
    kPaymentWrite,
    kOrderStatusCustomer,
    kOrderStatusLatest,
    kOrderStatusDetails,
    kDeliveryQueue,
    kDeliveryInspect,
    kDeliveryApply,
    kDeliveryEmptyCommit,
    kStockLevelDistrict,
    kStockLevelCount,
    kAbortOrOther,
};

auto classify_batch(const std::span<const protocol::RmdbBatchOperation> operations) noexcept
    -> BatchKind {
    const std::uint16_t first = operations.empty() ? 0U : operations[0U].statement_id;
    const std::uint16_t second = operations.size() < 2U ? 0U : operations[1U].statement_id;
    if (first == statement::kBegin && second == statement::kNewOrderCustomer) {
        return BatchKind::kNewOrderRead;
    }
    if (first == statement::kNewOrderInsertOrder) {
        return BatchKind::kNewOrderWrite;
    }
    if (first == statement::kBegin && second == statement::kPaymentWarehouseSelect) {
        return BatchKind::kPaymentRead;
    }
    if (first == statement::kPaymentCustomerUpdateGood
        || first == statement::kPaymentCustomerUpdateBad) {
        return BatchKind::kPaymentWrite;
    }
    if (first == statement::kBegin
        && (second == statement::kOrderStatusCustomerById
            || second == statement::kOrderStatusCustomerByLast)) {
        return BatchKind::kOrderStatusCustomer;
    }
    if (first == statement::kOrderStatusMaxOrder) {
        return BatchKind::kOrderStatusLatest;
    }
    if (first == statement::kOrderStatusOrderDetails) {
        return BatchKind::kOrderStatusDetails;
    }
    if (first == statement::kBegin && second == statement::kDeliveryMinOrder) {
        return BatchKind::kDeliveryQueue;
    }
    if (first == statement::kDeliveryLockOrder) {
        return BatchKind::kDeliveryInspect;
    }
    if (first == statement::kDeliveryDeleteOrder) {
        return BatchKind::kDeliveryApply;
    }
    if (first == statement::kCommit && operations.size() == 1U) {
        return BatchKind::kDeliveryEmptyCommit;
    }
    if (first == statement::kBegin && second == statement::kStockLevelNextOrder) {
        return BatchKind::kStockLevelDistrict;
    }
    if (first == statement::kStockLevelCount) {
        return BatchKind::kStockLevelCount;
    }
    return BatchKind::kAbortOrOther;
}

auto batch_name(const BatchKind kind) noexcept -> const char* {
    switch (kind) {
        case BatchKind::kNewOrderRead:
            return "new_order_read";
        case BatchKind::kNewOrderWrite:
            return "new_order_write";
        case BatchKind::kPaymentRead:
            return "payment_read";
        case BatchKind::kPaymentWrite:
            return "payment_write";
        case BatchKind::kOrderStatusCustomer:
            return "order_status_customer";
        case BatchKind::kOrderStatusLatest:
            return "order_status_latest";
        case BatchKind::kOrderStatusDetails:
            return "order_status_details";
        case BatchKind::kDeliveryQueue:
            return "delivery_queue";
        case BatchKind::kDeliveryInspect:
            return "delivery_inspect";
        case BatchKind::kDeliveryApply:
            return "delivery_apply";
        case BatchKind::kDeliveryEmptyCommit:
            return "delivery_empty_commit";
        case BatchKind::kStockLevelDistrict:
            return "stock_level_district";
        case BatchKind::kStockLevelCount:
            return "stock_level_count";
        case BatchKind::kAbortOrOther:
            return "abort_or_other";
    }
    return "unknown";
}

auto percentile(const std::vector<std::uint64_t>& sorted, const std::size_t percent) noexcept
    -> std::uint64_t {
    if (sorted.empty()) {
        return 0U;
    }
    return sorted[(sorted.size() - 1U) * percent / 100U];
}

}  // namespace

void TransactionCounters::merge(const TransactionCounters& other) noexcept {
    for (std::size_t index = 0U; index < attempted.size(); ++index) {
        array_element(attempted, index) += array_element(other.attempted, index);
        array_element(committed, index) += array_element(other.committed, index);
        array_element(database_aborts, index) += array_element(other.database_aborts, index);
        array_element(abandoned, index) += array_element(other.abandoned, index);
    }
    retries += other.retries;
    expected_rollbacks += other.expected_rollbacks;
    delivery_processed_orders += other.delivery_processed_orders;
}

auto TransactionCounters::total_committed() const noexcept -> std::uint64_t {
    std::uint64_t total = 0U;
    for (const std::uint64_t value : committed) {
        total += value;
    }
    return total;
}

SqlTelemetry::SqlTelemetry() = default;

void SqlTelemetry::record(
    const std::span<const protocol::RmdbBatchOperation> operations,
    const protocol::RmdbBatchResponse& response,
    const std::chrono::nanoseconds elapsed
) {
    const auto elapsed_ns = static_cast<std::uint64_t>(elapsed.count());
    const bool success = response.terminal.kind == protocol::RmdbBatchTerminalKind::kOk;
    const std::size_t attempted =
        success ? operations.size()
                : std::min(
                      operations.size(),
                      static_cast<std::size_t>(response.terminal.failed_operation) + 1U
                  );
    const std::size_t completed =
        std::min(operations.size(), static_cast<std::size_t>(response.executed_operation_count));

    BatchAccumulator& batch =
        array_element(batches_, static_cast<std::size_t>(classify_batch(operations)));
    ++batch.count;
    batch.total_ns += elapsed_ns;
    batch.latency_ns.push_back(elapsed_ns);
    if (attempted == 0U) {
        return;
    }

    const std::uint64_t base = elapsed_ns / attempted;
    const std::uint64_t remainder = elapsed_ns % attempted;
    for (std::size_t index = 0U; index < attempted; ++index) {
        StatementAccumulator& statement =
            array_element(statements_, operations[index].statement_id);
        ++statement.attempted;
        statement.completed += static_cast<std::uint64_t>(index < completed);
        statement.batch_inclusive_total_ns += elapsed_ns;
        statement.attributed_total_ns += base + static_cast<std::uint64_t>(index < remainder);
    }
}

void SqlTelemetry::merge(SqlTelemetry other) {
    for (std::size_t index = 0U; index < statements_.size(); ++index) {
        StatementAccumulator& target = array_element(statements_, index);
        const StatementAccumulator& source = array_element(other.statements_, index);
        target.attempted += source.attempted;
        target.completed += source.completed;
        target.batch_inclusive_total_ns += source.batch_inclusive_total_ns;
        target.attributed_total_ns += source.attributed_total_ns;
    }
    for (std::size_t index = 0U; index < batches_.size(); ++index) {
        BatchAccumulator& target = array_element(batches_, index);
        BatchAccumulator& source = array_element(other.batches_, index);
        target.count += source.count;
        target.total_ns += source.total_ns;
        target.latency_ns.insert(
            target.latency_ns.end(),
            std::make_move_iterator(source.latency_ns.begin()),
            std::make_move_iterator(source.latency_ns.end())
        );
    }
}

auto SqlTelemetry::into_profile() && -> SqlProfile {
    SqlProfile profile;
    for (std::size_t index = 0U; index < batches_.size(); ++index) {
        BatchAccumulator& accumulator = array_element(batches_, index);
        if (accumulator.count == 0U) {
            continue;
        }
        std::sort(accumulator.latency_ns.begin(), accumulator.latency_ns.end());
        profile.total_batch_count += accumulator.count;
        profile.total_batch_wait_ns += accumulator.total_ns;
        profile.batches.push_back(
            BatchMetric{
                .name = batch_name(static_cast<BatchKind>(index)),
                .count = accumulator.count,
                .total_ns = accumulator.total_ns,
                .p50_ns = percentile(accumulator.latency_ns, 50U),
                .p95_ns = percentile(accumulator.latency_ns, 95U),
            }
        );
    }
    std::sort(
        profile.batches.begin(),
        profile.batches.end(),
        [](const auto& left, const auto& right) {
            return left.total_ns > right.total_ns;
        }
    );

    for (std::size_t id = 0U; id < statements_.size(); ++id) {
        const StatementAccumulator& accumulator = array_element(statements_, id);
        if (accumulator.attempted == 0U) {
            continue;
        }
        profile.statements.push_back(
            StatementMetric{
                .id = static_cast<std::uint16_t>(id),
                .name = statement::name(static_cast<std::uint16_t>(id)),
                .attempted = accumulator.attempted,
                .completed = accumulator.completed,
                .batch_inclusive_total_ns = accumulator.batch_inclusive_total_ns,
                .attributed_total_ns = accumulator.attributed_total_ns,
            }
        );
    }
    std::sort(
        profile.statements.begin(),
        profile.statements.end(),
        [](const auto& left, const auto& right) {
            return left.attributed_total_ns > right.attributed_total_ns;
        }
    );
    return profile;
}

}  // namespace db::tpcc
