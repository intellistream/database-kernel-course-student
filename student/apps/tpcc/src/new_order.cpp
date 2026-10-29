#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "prepared.hpp"
#include "transaction_common.hpp"
#include "transaction_internal.hpp"

namespace db::tpcc {
namespace {

struct NewOrderLine final {
    std::uint32_t item{0U};
    std::uint32_t supply_warehouse{0U};
    std::uint32_t quantity{0U};
};

struct NewOrderInput final {
    std::uint32_t warehouse{0U};
    std::uint32_t district{0U};
    std::uint32_t customer{0U};
    std::vector<NewOrderLine> lines;
    bool all_local{true};
    bool expected_invalid{false};
};

struct ResolvedLine final {
    std::int32_t old_quantity{0};
    std::string district_info;
    float amount{0.0F};
};

struct ReadBatch final {
    std::vector<protocol::RmdbBatchOperation> operations;
    std::vector<std::pair<std::size_t, std::size_t>> response_indices;
};

auto generate_input(
    TransactionGenerator& generator,
    const RouteSelection route,
    const DatasetScale scale,
    const RoutingConfig routing,
    const std::span<const std::uint32_t> hot_items
) -> NewOrderInput {
    const std::uint32_t line_count = generator.value(5U, 15U);
    NewOrderInput input{
        .warehouse = route.home_warehouse,
        .district = route.district,
        .customer = generator.nurand(1'023U, 1U, scale.customers_per_district),
        .lines = {},
        .all_local = true,
        .expected_invalid = generator.value(1U, 100U) == 1U,
    };
    input.lines.reserve(line_count);
    for (std::uint32_t line_number = 1U; line_number <= line_count; ++line_number) {
        std::uint32_t item =
            generator.value(1U, 100U) <= routing.hot_item_line_percent
                ? hot_items[generator.value(0U, static_cast<std::uint32_t>(hot_items.size() - 1U))]
                : generator.nurand(8'191U, 1U, scale.items);
        if (input.expected_invalid && line_number == line_count) {
            item = scale.items + 1U;
        }
        const std::uint32_t supply =
            scale.warehouses > 1U
                    && generator.value(1U, 100U) <= routing.new_order_remote_line_percent
                ? other_warehouse(generator, scale.warehouses, route.home_warehouse)
                : route.home_warehouse;
        input.all_local = input.all_local && supply == route.home_warehouse;
        input.lines.push_back(
            NewOrderLine{
                .item = item,
                .supply_warehouse = supply,
                .quantity = generator.value(1U, 10U),
            }
        );
    }
    return input;
}

auto read_batch(const NewOrderInput& input, const DatasetScale scale) -> ReadBatch {
    std::set<std::pair<std::uint32_t, std::uint32_t>> stock_keys;
    for (const NewOrderLine& line : input.lines) {
        if (line.item <= scale.items) {
            stock_keys.emplace(line.supply_warehouse, line.item);
        }
    }
    ReadBatch batch;
    batch.operations = {
        operation(statement::kBegin),
        operation(
            statement::kNewOrderCustomer,
            {integer(input.warehouse), integer(input.district), integer(input.customer)}
        ),
        operation(
            statement::kNewOrderDistrictUpdate,
            {integer(input.warehouse), integer(input.district)}
        ),
        operation(
            statement::kNewOrderDistrictSelect,
            {integer(input.warehouse), integer(input.district)}
        ),
    };
    batch.operations.reserve(4U + stock_keys.size() + input.lines.size() * 2U);
    for (const auto& [warehouse, item] : stock_keys) {
        batch.operations.push_back(
            operation(statement::kNewOrderStockLock, {integer(warehouse), integer(item)})
        );
    }
    batch.response_indices.reserve(input.lines.size());
    for (const NewOrderLine& line : input.lines) {
        const std::size_t item_index = batch.operations.size();
        batch.operations.push_back(operation(statement::kNewOrderItem, {integer(line.item)}));
        const std::size_t stock_index = batch.operations.size();
        batch.operations.push_back(operation(
            statement::stock_district(input.district),
            {integer(line.supply_warehouse), integer(line.item)}
        ));
        batch.response_indices.emplace_back(item_index, stock_index);
    }
    return batch;
}

auto order_id(const protocol::RmdbBatchResponse& response) -> base::Result<std::int32_t> {
    auto customer = require_row_count(response, 1U, 1U, "NewOrder customer");
    if (!customer) {
        return base::fail(std::move(customer).error());
    }
    auto district_rows = query_rows(response, 3U);
    if (!district_rows) {
        return base::fail(std::move(district_rows).error());
    }
    if (district_rows->size() != 1U || district_rows->front().size() != 2U) {
        return transaction_error("NewOrder district lookup was not one two-column row");
    }
    auto next_order = value_i32(district_rows->front()[0U]);
    if (!next_order) {
        return base::fail(std::move(next_order).error());
    }
    if (next_order.value() <= 1) {
        return transaction_error("NewOrder returned invalid d_next_o_id");
    }
    return next_order.value() - 1;
}

auto resolve_line(
    const protocol::RmdbBatchResponse& response,
    const NewOrderInput& input,
    const std::size_t index,
    const std::pair<std::size_t, std::size_t> response_indices,
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::int32_t>& pending_quantity
) -> base::Result<std::optional<ResolvedLine>> {
    const NewOrderLine& line = input.lines[index];
    const auto [item_index, stock_index] = response_indices;
    auto item_rows = query_rows(response, item_index);
    if (!item_rows) {
        return base::fail(std::move(item_rows).error());
    }
    if (item_rows->empty() && input.expected_invalid && index + 1U == input.lines.size()) {
        return std::optional<ResolvedLine>{};
    }
    if (item_rows->size() != 1U || item_rows->front().size() != 4U) {
        return transaction_error("NewOrder item lookup was malformed");
    }
    auto observed_item = value_i32(item_rows->front()[0U]);
    auto price = value_f32(item_rows->front()[1U]);
    if (!observed_item) {
        return base::fail(std::move(observed_item).error());
    }
    if (!price) {
        return base::fail(std::move(price).error());
    }
    if (std::cmp_not_equal(observed_item.value(), line.item)) {
        return transaction_error("NewOrder item lookup returned the wrong item");
    }

    auto stock_rows = query_rows(response, stock_index);
    if (!stock_rows) {
        return base::fail(std::move(stock_rows).error());
    }
    if (stock_rows->size() != 1U || stock_rows->front().size() != 2U) {
        return transaction_error("NewOrder stock lookup was malformed");
    }
    auto observed_quantity = value_i32(stock_rows->front()[0U]);
    auto district_info = value_text(stock_rows->front()[1U]);
    if (!observed_quantity) {
        return base::fail(std::move(observed_quantity).error());
    }
    if (!district_info) {
        return base::fail(std::move(district_info).error());
    }
    const auto key = std::pair{line.supply_warehouse, line.item};
    const auto retained = pending_quantity.find(key);
    const std::int32_t old_quantity =
        retained == pending_quantity.end() ? observed_quantity.value() : retained->second;
    pending_quantity[key] =
        next_stock_quantity(old_quantity, static_cast<std::int32_t>(line.quantity));
    return std::optional<ResolvedLine>{ResolvedLine{
        .old_quantity = old_quantity,
        .district_info = std::string{district_info.value()},
        .amount = price.value() * static_cast<float>(line.quantity),
    }};
}

auto resolve_lines(
    SqlSession& session,
    const protocol::RmdbBatchResponse& response,
    const NewOrderInput& input,
    const std::span<const std::pair<std::size_t, std::size_t>> response_indices
) -> base::Result<std::optional<std::vector<ResolvedLine>>> {
    std::vector<ResolvedLine> resolved;
    resolved.reserve(input.lines.size());
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::int32_t> pending_quantity;
    for (std::size_t index = 0U; index < input.lines.size(); ++index) {
        auto line = resolve_line(response, input, index, response_indices[index], pending_quantity);
        if (!line) {
            return base::fail(std::move(line).error());
        }
        if (!line->has_value()) {
            auto aborted = session.abort_open_transaction();
            if (!aborted) {
                return base::fail(std::move(aborted).error());
            }
            return std::optional<std::vector<ResolvedLine>>{};
        }
        resolved.push_back(std::move(**line));
    }
    return std::optional<std::vector<ResolvedLine>>{std::move(resolved)};
}

auto write_batch(
    const NewOrderInput& input,
    const std::int32_t order,
    const std::span<const ResolvedLine> resolved
) -> std::vector<protocol::RmdbBatchOperation> {
    std::vector<protocol::RmdbBatchOperation> operations{
        operation(
            statement::kNewOrderInsertOrder,
            {
                signed_integer(order),
                integer(input.district),
                integer(input.warehouse),
                integer(input.customer),
                text(timestamp()),
                integer(static_cast<std::uint32_t>(input.lines.size())),
                integer(static_cast<std::uint32_t>(input.all_local)),
            }
        ),
        operation(
            statement::kNewOrderInsertNewOrder,
            {signed_integer(order), integer(input.district), integer(input.warehouse)}
        ),
    };
    operations.reserve(3U + input.lines.size() * 2U);
    for (std::size_t index = 0U; index < input.lines.size(); ++index) {
        const NewOrderLine& line = input.lines[index];
        const ResolvedLine& current = resolved[index];
        const std::uint32_t threshold = line.quantity + 10U;
        operations.push_back(operation(
            std::cmp_greater_equal(current.old_quantity, threshold)
                ? statement::kNewOrderStockUpdateHigh
                : statement::kNewOrderStockUpdateLow,
            {
                integer(line.quantity),
                integer(static_cast<std::uint32_t>(line.supply_warehouse != input.warehouse)),
                integer(line.supply_warehouse),
                integer(line.item),
                integer(threshold),
            }
        ));
        operations.push_back(operation(
            statement::kNewOrderInsertLine,
            {
                signed_integer(order),
                integer(input.district),
                integer(input.warehouse),
                integer(static_cast<std::uint32_t>(index + 1U)),
                integer(line.item),
                integer(line.supply_warehouse),
                integer(line.quantity),
                money(current.amount),
                text(current.district_info),
            }
        ));
    }
    operations.push_back(operation(statement::kCommit));
    return operations;
}

auto write_order(
    SqlSession& session,
    const NewOrderInput& input,
    const std::int32_t order,
    const std::span<const ResolvedLine> resolved
) -> base::Result<AttemptResult> {
    const auto operations = write_batch(input, order, resolved);
    auto response = session.batch(operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    return AttemptResult{
        .disposition =
            response->has_value() ? AttemptDisposition::kCommitted : AttemptDisposition::kAborted,
    };
}

auto attempt(SqlSession& session, const NewOrderInput& input, const DatasetScale scale)
    -> base::Result<AttemptResult> {
    ReadBatch batch = read_batch(input, scale);
    auto response = session.batch(batch.operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (!response->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    auto next_order = order_id(**response);
    if (!next_order) {
        return base::fail(std::move(next_order).error());
    }
    auto lines = resolve_lines(session, **response, input, batch.response_indices);
    if (!lines) {
        return base::fail(std::move(lines).error());
    }
    if (!lines->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kExpectedRollback};
    }
    return write_order(session, input, next_order.value(), lines->value());
}

}  // namespace

auto run_new_order(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData> {
    const NewOrderInput input = generate_input(
        execution.generator,
        execution.route,
        execution.scale,
        execution.routing,
        execution.hot_items
    );
    return retry_transaction(
        execution.session,
        TransactionKind::kNewOrder,
        execution.retry_limit,
        counters,
        [&execution, &input]() {
            return attempt(execution.session, input, execution.scale);
        }
    );
}

}  // namespace db::tpcc
