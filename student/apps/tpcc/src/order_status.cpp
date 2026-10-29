#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "prepared.hpp"
#include "transaction_common.hpp"
#include "transaction_internal.hpp"

namespace db::tpcc {
namespace {

using Rows = std::span<const std::vector<types::Value>>;

struct OrderStatusInput final {
    std::uint32_t warehouse{0U};
    std::uint32_t district{0U};
    std::uint32_t customer{0U};
    std::string customer_last;
    bool by_last{false};
};

struct OrderMetadata final {
    std::int32_t declared_lines{0};
};

auto generate_input(
    TransactionGenerator& generator,
    const RouteSelection route,
    const DatasetScale scale
) -> OrderStatusInput {
    return OrderStatusInput{
        .warehouse = route.home_warehouse,
        .district = route.district,
        .customer = generator.nurand(1'023U, 1U, scale.customers_per_district),
        .customer_last = tpcc_last_name(generator.nurand(255U, 0U, 999U)),
        .by_last = generator.value(1U, 100U) <= 60U,
    };
}

auto customer_batch(const OrderStatusInput& input) -> std::vector<protocol::RmdbBatchOperation> {
    const std::uint16_t statement_id =
        input.by_last ? statement::kOrderStatusCustomerByLast : statement::kOrderStatusCustomerById;
    std::vector<types::Value> parameters{integer(input.warehouse), integer(input.district)};
    parameters.push_back(input.by_last ? text(input.customer_last) : integer(input.customer));
    std::vector<protocol::RmdbBatchOperation> operations{
        operation(statement::kBegin),
        operation(statement_id, std::move(parameters)),
    };
    if (input.by_last) {
        operations.push_back(operation(
            statement::kCustomerByLastCount,
            {integer(input.warehouse), integer(input.district), text(input.customer_last)}
        ));
    }
    return operations;
}

auto validate_by_last_rows(
    const Rows customers,
    const OrderStatusInput& input,
    const DatasetScale scale
) -> base::Status {
    std::set<std::int32_t> ids;
    std::string_view previous_first;
    bool have_previous = false;
    for (const auto& row : customers) {
        if (row.size() != 5U) {
            return transaction_error("OrderStatus by-last customer row was malformed");
        }
        auto id = value_i32(row[0U]);
        auto first_name = value_text(row[2U]);
        auto last_name = value_text(row[4U]);
        if (!id) {
            return base::fail(std::move(id).error());
        }
        if (!first_name) {
            return base::fail(std::move(first_name).error());
        }
        if (!last_name) {
            return base::fail(std::move(last_name).error());
        }
        if (id.value() < 1 || std::cmp_greater(id.value(), scale.customers_per_district)
            || !ids.insert(id.value()).second || !valid_char(row[2U], 16U, false)
            || last_name.value() != input.customer_last
            || (have_previous && previous_first > first_name.value())) {
            return transaction_error(
                "OrderStatus by-last rows were not a sorted unique matching set"
            );
        }
        previous_first = first_name.value();
        have_previous = true;
    }
    return {};
}

auto validate_selected_customer(
    const std::vector<types::Value>& selected,
    const OrderStatusInput& input,
    const DatasetScale scale
) -> base::Result<std::uint32_t> {
    if (selected.size() != 5U) {
        return transaction_error("OrderStatus selected customer was malformed");
    }
    auto customer_id = value_i32(selected[0U]);
    auto balance = value_f32(selected[1U]);
    auto middle = value_text(selected[3U]);
    if (!customer_id) {
        return base::fail(std::move(customer_id).error());
    }
    if (!balance) {
        return base::fail(std::move(balance).error());
    }
    if (!middle) {
        return base::fail(std::move(middle).error());
    }
    if (customer_id.value() < 1
        || std::cmp_greater(customer_id.value(), scale.customers_per_district)
        || (!input.by_last && std::cmp_not_equal(customer_id.value(), input.customer))
        || !valid_char(selected[2U], 16U, false) || middle.value() != "OE"
        || !valid_char(selected[4U], 16U, false)) {
        return transaction_error("OrderStatus selected customer fields were malformed");
    }
    return static_cast<std::uint32_t>(customer_id.value());
}

auto select_customer(
    const protocol::RmdbBatchResponse& response,
    const OrderStatusInput& input,
    const DatasetScale scale
) -> base::Result<std::uint32_t> {
    auto customers = query_rows(response, 1U);
    if (!customers) {
        return base::fail(std::move(customers).error());
    }
    if (customers->empty()) {
        return transaction_error("OrderStatus customer lookup returned no rows");
    }
    std::size_t selected_index = 0U;
    if (input.by_last) {
        auto count = scalar_i32(response, 2U);
        if (!count) {
            return base::fail(std::move(count).error());
        }
        if (std::cmp_not_equal(count.value(), customers->size())) {
            return transaction_error("OrderStatus by-last lookup omitted or duplicated customers");
        }
        auto valid = validate_by_last_rows(customers.value(), input, scale);
        if (!valid) {
            return base::fail(std::move(valid).error());
        }
        selected_index = (customers->size() - 1U) / 2U;
    } else if (customers->size() != 1U) {
        return transaction_error("OrderStatus customer-by-id was not unique");
    }
    return validate_selected_customer(customers.value()[selected_index], input, scale);
}

auto latest_order(SqlSession& session, const OrderStatusInput& input, const std::uint32_t customer)
    -> base::Result<std::optional<std::int32_t>> {
    const std::vector<protocol::RmdbBatchOperation> operations{operation(
        statement::kOrderStatusMaxOrder,
        {integer(input.warehouse), integer(input.district), integer(customer)}
    )};
    auto response = session.batch(operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (!response->has_value()) {
        return std::optional<std::int32_t>{};
    }
    auto latest = scalar_i32(**response, 0U);
    if (!latest) {
        return base::fail(std::move(latest).error());
    }
    if (latest.value() <= 0) {
        return transaction_error("OrderStatus latest order was invalid");
    }
    return std::optional<std::int32_t>{latest.value()};
}

auto validate_order_metadata(
    const protocol::RmdbBatchResponse& response,
    const std::uint32_t customer
) -> base::Result<OrderMetadata> {
    auto details = query_rows(response, 0U);
    if (!details) {
        return base::fail(std::move(details).error());
    }
    if (details->size() != 1U || details->front().size() != 4U) {
        return transaction_error("OrderStatus latest order metadata was malformed");
    }
    auto details_customer = value_i32(details->front()[0U]);
    auto declared_lines = value_i32(details->front()[1U]);
    auto carrier = value_i32(details->front()[3U]);
    if (!details_customer) {
        return base::fail(std::move(details_customer).error());
    }
    if (!declared_lines) {
        return base::fail(std::move(declared_lines).error());
    }
    if (!carrier) {
        return base::fail(std::move(carrier).error());
    }
    if (std::cmp_not_equal(details_customer.value(), customer) || declared_lines.value() < 5
        || declared_lines.value() > 15 || !valid_char(details->front()[2U], 30U, false)
        || carrier.value() < 0 || carrier.value() > 10) {
        return transaction_error("OrderStatus latest order fields were malformed");
    }
    auto newer_count = scalar_i32(response, 1U);
    if (!newer_count) {
        return base::fail(std::move(newer_count).error());
    }
    if (newer_count.value() != 0) {
        return transaction_error("OrderStatus selected order was not the latest");
    }
    return OrderMetadata{.declared_lines = declared_lines.value()};
}

auto validate_order_line(
    const std::vector<types::Value>& row,
    const std::size_t index,
    const DatasetScale scale
) -> base::Status {
    if (row.size() != 6U) {
        return transaction_error("OrderStatus order line shape was malformed");
    }
    auto number = value_i32(row[0U]);
    auto item = value_i32(row[1U]);
    auto supply = value_i32(row[2U]);
    auto quantity = value_i32(row[3U]);
    auto amount = value_f32(row[4U]);
    if (!number) {
        return base::fail(std::move(number).error());
    }
    if (!item) {
        return base::fail(std::move(item).error());
    }
    if (!supply) {
        return base::fail(std::move(supply).error());
    }
    if (!quantity) {
        return base::fail(std::move(quantity).error());
    }
    if (!amount) {
        return base::fail(std::move(amount).error());
    }
    if (std::cmp_not_equal(number.value(), index + 1U) || item.value() < 1
        || std::cmp_greater(item.value(), scale.items) || supply.value() < 1
        || std::cmp_greater(supply.value(), scale.warehouses) || quantity.value() < 1
        || quantity.value() > 10 || amount.value() < 0.0F || amount.value() > 10'000.0F
        || !valid_char(row[5U], 19U, true)) {
        return transaction_error("OrderStatus order line values were malformed");
    }
    return {};
}

auto validate_order_lines(
    const protocol::RmdbBatchResponse& response,
    const OrderMetadata metadata,
    const DatasetScale scale
) -> base::Status {
    auto lines = query_rows(response, 2U);
    if (!lines) {
        return base::fail(std::move(lines).error());
    }
    if (std::cmp_not_equal(metadata.declared_lines, lines->size())) {
        return transaction_error("OrderStatus line cardinality disagreed with the order row");
    }
    for (std::size_t index = 0U; index < lines->size(); ++index) {
        auto valid = validate_order_line(lines.value()[index], index, scale);
        if (!valid) {
            return valid;
        }
    }
    return {};
}

auto inspect_order(
    SqlSession& session,
    const OrderStatusInput& input,
    const DatasetScale scale,
    const std::uint32_t customer,
    const std::int32_t latest
) -> base::Result<AttemptResult> {
    const std::vector<protocol::RmdbBatchOperation> operations{
        operation(
            statement::kOrderStatusOrderDetails,
            {integer(input.warehouse), integer(input.district), signed_integer(latest)}
        ),
        operation(
            statement::kOrderStatusNewerCount,
            {integer(input.warehouse),
             integer(input.district),
             integer(customer),
             signed_integer(latest)}
        ),
        operation(
            statement::kOrderStatusLines,
            {integer(input.warehouse), integer(input.district), signed_integer(latest)}
        ),
        operation(statement::kCommit),
    };
    auto response = session.batch(operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (!response->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    auto metadata = validate_order_metadata(**response, customer);
    if (!metadata) {
        return base::fail(std::move(metadata).error());
    }
    auto valid = validate_order_lines(**response, metadata.value(), scale);
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    return AttemptResult{.disposition = AttemptDisposition::kCommitted};
}

auto attempt(SqlSession& session, const OrderStatusInput& input, const DatasetScale scale)
    -> base::Result<AttemptResult> {
    const auto first = customer_batch(input);
    auto first_response = session.batch(first);
    if (!first_response) {
        return base::fail(std::move(first_response).error());
    }
    if (!first_response->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    auto customer = select_customer(**first_response, input, scale);
    if (!customer) {
        return base::fail(std::move(customer).error());
    }
    auto latest = latest_order(session, input, customer.value());
    if (!latest) {
        return base::fail(std::move(latest).error());
    }
    if (!latest->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    return inspect_order(session, input, scale, customer.value(), **latest);
}

}  // namespace

auto run_order_status(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData> {
    const OrderStatusInput input =
        generate_input(execution.generator, execution.route, execution.scale);
    return retry_transaction(
        execution.session,
        TransactionKind::kOrderStatus,
        execution.retry_limit,
        counters,
        [&execution, &input]() {
            return attempt(execution.session, input, execution.scale);
        }
    );
}

}  // namespace db::tpcc
