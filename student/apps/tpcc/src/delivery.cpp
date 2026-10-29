#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "prepared.hpp"
#include "transaction_common.hpp"
#include "transaction_internal.hpp"

namespace db::tpcc {
namespace {

using PendingOrder = std::pair<std::uint32_t, std::uint32_t>;

struct DeliverySample final {
    std::uint32_t district{0U};
    std::uint32_t order{0U};
    std::uint32_t customer{0U};
    std::uint32_t lines{0U};
    float amount{0.0F};
};

struct InspectionIndices final {
    std::uint32_t district{0U};
    std::uint32_t order{0U};
    std::size_t claim{0U};
    std::size_t earlier{0U};
    std::size_t customer{0U};
    std::size_t lines{0U};
    std::size_t sum{0U};
};

struct InspectionBatch final {
    std::vector<protocol::RmdbBatchOperation> operations;
    std::vector<InspectionIndices> indices;
};

auto delivery_line_sum(
    const std::span<const std::vector<types::Value>> rows,
    const std::size_t expected
) -> base::Result<float> {
    if (rows.size() != expected || expected < 5U || expected > 15U) {
        return transaction_error("Delivery order line cardinality disagreed with orders");
    }
    std::uint64_t units = 0U;
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        const auto& row = rows[index];
        if (row.size() != 3U) {
            return transaction_error("Delivery order line shape was malformed");
        }
        auto line_number = value_i32(row[0U]);
        auto amount = value_f32(row[1U]);
        auto delivered = value_text(row[2U]);
        if (!line_number) {
            return base::fail(std::move(line_number).error());
        }
        if (!amount) {
            return base::fail(std::move(amount).error());
        }
        if (!delivered) {
            return base::fail(std::move(delivered).error());
        }
        const double cents = std::round(static_cast<double>(amount.value()) * 100.0);
        if (std::cmp_not_equal(line_number.value(), index + 1U) || !delivered->empty()
            || cents < 1.0 || cents > 999'999.0) {
            return transaction_error("Delivery order line values were malformed");
        }
        const auto amount_units = exact_float32_units(amount.value());
        if (!amount_units.has_value()) {
            return transaction_error("Delivery line amount was not exact finite FLOAT32");
        }
        if (amount_units.value() > std::numeric_limits<std::uint64_t>::max() - units) {
            return transaction_error("Delivery line amount sum overflowed");
        }
        units += amount_units.value();
    }
    const auto sum = exact_float32_sum(units);
    if (!sum.has_value()) {
        return transaction_error("Delivery line amount sum exceeded the exact domain");
    }
    return sum.value();
}

auto claim_orders(SqlSession& session, const DatasetScale scale, const std::uint32_t warehouse)
    -> base::Result<std::optional<std::vector<PendingOrder>>> {
    std::vector<protocol::RmdbBatchOperation> operations{operation(statement::kBegin)};
    operations.reserve(scale.districts_per_warehouse + 1U);
    for (std::uint32_t district = 1U; district <= scale.districts_per_warehouse; ++district) {
        operations.push_back(
            operation(statement::kDeliveryMinOrder, {integer(warehouse), integer(district)})
        );
    }
    auto response = session.batch(operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (!response->has_value()) {
        return std::optional<std::vector<PendingOrder>>{};
    }

    std::vector<PendingOrder> orders;
    for (std::uint32_t district = 1U; district <= scale.districts_per_warehouse; ++district) {
        auto rows = query_rows(**response, district);
        if (!rows) {
            return base::fail(std::move(rows).error());
        }
        if (rows->size() != 1U || rows->front().size() != 1U) {
            return transaction_error("Delivery MIN(no_o_id) was not scalar");
        }
        const types::Value& value = rows->front().front();
        if (value.is_null()) {
            continue;
        }
        auto order = value_i32(value);
        if (!order) {
            return base::fail(std::move(order).error());
        }
        if (order.value() <= 0) {
            return transaction_error("Delivery MIN(no_o_id) was invalid");
        }
        orders.emplace_back(district, static_cast<std::uint32_t>(order.value()));
    }
    return std::optional<std::vector<PendingOrder>>{std::move(orders)};
}

auto inspection_batch(const std::span<const PendingOrder> orders, const std::uint32_t warehouse)
    -> InspectionBatch {
    InspectionBatch batch;
    batch.operations.reserve(orders.size() * 6U);
    batch.indices.reserve(orders.size());
    for (const auto& [district, order_id] : orders) {
        const auto parameters = [warehouse, district, order_id]() {
            return std::vector<types::Value>{
                integer(warehouse),
                integer(district),
                integer(order_id),
            };
        };
        batch.operations.push_back(operation(statement::kDeliveryLockOrder, parameters()));
        const std::size_t claim = batch.operations.size();
        batch.operations.push_back(operation(statement::kDeliveryClaimCount, parameters()));
        const std::size_t earlier = batch.operations.size();
        batch.operations.push_back(operation(statement::kDeliveryEarlierCount, parameters()));
        const std::size_t customer = batch.operations.size();
        batch.operations.push_back(operation(statement::kDeliveryCustomer, parameters()));
        const std::size_t lines = batch.operations.size();
        batch.operations.push_back(operation(statement::kDeliveryLines, parameters()));
        const std::size_t sum = batch.operations.size();
        batch.operations.push_back(operation(statement::kDeliverySumAmount, parameters()));
        batch.indices.push_back(
            InspectionIndices{
                .district = district,
                .order = order_id,
                .claim = claim,
                .earlier = earlier,
                .customer = customer,
                .lines = lines,
                .sum = sum,
            }
        );
    }
    return batch;
}

auto inspect_order(
    SqlSession& session,
    const protocol::RmdbBatchResponse& response,
    const InspectionIndices& index,
    const DatasetScale scale
) -> base::Result<std::optional<DeliverySample>> {
    auto claim = scalar_i32(response, index.claim);
    if (!claim) {
        return base::fail(std::move(claim).error());
    }
    if (claim.value() != 1) {
        auto aborted = session.abort_open_transaction();
        if (!aborted) {
            return base::fail(std::move(aborted).error());
        }
        return std::optional<DeliverySample>{};
    }
    auto earlier = scalar_i32(response, index.earlier);
    if (!earlier) {
        return base::fail(std::move(earlier).error());
    }
    if (earlier.value() != 0) {
        return transaction_error("Delivery MIN disagreed with earlier-order count");
    }
    auto metadata = query_rows(response, index.customer);
    if (!metadata) {
        return base::fail(std::move(metadata).error());
    }
    if (metadata->size() != 1U || metadata->front().size() != 3U) {
        return transaction_error("Delivery order metadata was malformed");
    }
    auto customer = value_i32(metadata->front()[0U]);
    auto declared_lines = value_i32(metadata->front()[1U]);
    auto old_carrier = value_i32(metadata->front()[2U]);
    if (!customer) {
        return base::fail(std::move(customer).error());
    }
    if (!declared_lines) {
        return base::fail(std::move(declared_lines).error());
    }
    if (!old_carrier) {
        return base::fail(std::move(old_carrier).error());
    }
    if (customer.value() < 1 || std::cmp_greater(customer.value(), scale.customers_per_district)
        || declared_lines.value() < 5 || declared_lines.value() > 15 || old_carrier.value() != 0) {
        return transaction_error("Delivery order carrier or line count was invalid");
    }
    auto lines = query_rows(response, index.lines);
    if (!lines) {
        return base::fail(std::move(lines).error());
    }
    auto expected_sum =
        delivery_line_sum(lines.value(), static_cast<std::size_t>(declared_lines.value()));
    auto observed_sum = scalar_f32(response, index.sum);
    if (!expected_sum) {
        return base::fail(std::move(expected_sum).error());
    }
    if (!observed_sum) {
        return base::fail(std::move(observed_sum).error());
    }
    if (canonical_float_bits(expected_sum.value()) != canonical_float_bits(observed_sum.value())) {
        return transaction_error("Delivery SUM(ol_amount) disagreed with stored FLOAT32 lines");
    }
    return std::optional<DeliverySample>{DeliverySample{
        .district = index.district,
        .order = index.order,
        .customer = static_cast<std::uint32_t>(customer.value()),
        .lines = static_cast<std::uint32_t>(declared_lines.value()),
        .amount = observed_sum.value(),
    }};
}

auto inspect_orders(
    SqlSession& session,
    const DatasetScale scale,
    const std::uint32_t warehouse,
    const std::span<const PendingOrder> orders
) -> base::Result<std::optional<std::vector<DeliverySample>>> {
    InspectionBatch batch = inspection_batch(orders, warehouse);
    auto response = session.batch(batch.operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (!response->has_value()) {
        return std::optional<std::vector<DeliverySample>>{};
    }
    std::vector<DeliverySample> samples;
    samples.reserve(batch.indices.size());
    for (const InspectionIndices& index : batch.indices) {
        auto sample = inspect_order(session, **response, index, scale);
        if (!sample) {
            return base::fail(std::move(sample).error());
        }
        if (!sample->has_value()) {
            return std::optional<std::vector<DeliverySample>>{};
        }
        samples.push_back(**sample);
    }
    return std::optional<std::vector<DeliverySample>>{std::move(samples)};
}

auto apply_batch(
    const std::span<const DeliverySample> samples,
    const std::uint32_t warehouse,
    const std::uint32_t carrier,
    std::vector<std::pair<std::size_t, std::size_t>>& balances
) -> std::vector<protocol::RmdbBatchOperation> {
    const std::string delivery_timestamp = timestamp();
    std::vector<protocol::RmdbBatchOperation> operations;
    operations.reserve(samples.size() * 6U + 1U);
    balances.reserve(samples.size());
    for (const DeliverySample& sample : samples) {
        operations.push_back(operation(
            statement::kDeliveryDeleteOrder,
            {integer(warehouse), integer(sample.district), integer(sample.order)}
        ));
        operations.push_back(operation(
            statement::kDeliveryUpdateOrder,
            {integer(carrier), integer(warehouse), integer(sample.district), integer(sample.order)}
        ));
        operations.push_back(operation(
            statement::kDeliveryUpdateLines,
            {
                text(delivery_timestamp),
                integer(warehouse),
                integer(sample.district),
                integer(sample.order),
            }
        ));
        const std::size_t before = operations.size();
        operations.push_back(operation(
            statement::kDeliveryCustomerBalanceBefore,
            {integer(warehouse), integer(sample.district), integer(sample.customer)}
        ));
        operations.push_back(operation(
            statement::kDeliveryUpdateCustomer,
            {money(sample.amount),
             integer(warehouse),
             integer(sample.district),
             integer(sample.customer)}
        ));
        const std::size_t after = operations.size();
        operations.push_back(operation(
            statement::kDeliveryCustomerBalanceAfter,
            {integer(warehouse), integer(sample.district), integer(sample.customer)}
        ));
        balances.emplace_back(before, after);
    }
    operations.push_back(operation(statement::kCommit));
    return operations;
}

auto apply_delivery(
    SqlSession& session,
    const std::span<const DeliverySample> samples,
    const std::uint32_t warehouse,
    const std::uint32_t carrier
) -> base::Result<AttemptResult> {
    std::vector<std::pair<std::size_t, std::size_t>> balances;
    const auto operations = apply_batch(samples, warehouse, carrier, balances);
    auto response = session.batch(operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (!response->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        const auto [before_index, after_index] = balances[index];
        auto before = scalar_f32(**response, before_index);
        auto after = scalar_f32(**response, after_index);
        if (!before) {
            return base::fail(std::move(before).error());
        }
        if (!after) {
            return base::fail(std::move(after).error());
        }
        if (canonical_float_bits(before.value() + samples[index].amount)
            != canonical_float_bits(after.value())) {
            return transaction_error("Delivery customer balance FLOAT32 update was not bit-exact");
        }
    }
    return AttemptResult{
        .disposition = AttemptDisposition::kCommitted,
        .delivery_processed_orders = static_cast<std::uint32_t>(samples.size()),
    };
}

auto commit_empty_delivery(SqlSession& session) -> base::Result<AttemptResult> {
    const std::vector<protocol::RmdbBatchOperation> commit{operation(statement::kCommit)};
    auto response = session.batch(commit);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    return AttemptResult{
        .disposition =
            response->has_value() ? AttemptDisposition::kCommitted : AttemptDisposition::kAborted,
    };
}

auto attempt(
    SqlSession& session,
    const DatasetScale scale,
    const std::uint32_t warehouse,
    const std::uint32_t carrier
) -> base::Result<AttemptResult> {
    auto orders = claim_orders(session, scale, warehouse);
    if (!orders) {
        return base::fail(std::move(orders).error());
    }
    if (!orders->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    if (orders->value().empty()) {
        return commit_empty_delivery(session);
    }
    auto samples = inspect_orders(session, scale, warehouse, orders->value());
    if (!samples) {
        return base::fail(std::move(samples).error());
    }
    if (!samples->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    return apply_delivery(session, samples->value(), warehouse, carrier);
}

}  // namespace

auto run_delivery(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData> {
    const std::uint32_t carrier = execution.generator.value(1U, 10U);
    return retry_transaction(
        execution.session,
        TransactionKind::kDelivery,
        execution.retry_limit,
        counters,
        [&execution, carrier]() {
            return attempt(
                execution.session,
                execution.scale,
                execution.route.home_warehouse,
                carrier
            );
        }
    );
}

}  // namespace db::tpcc
