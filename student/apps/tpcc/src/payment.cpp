#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <set>
#include <span>
#include <sstream>
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

struct PaymentInput final {
    std::uint32_t warehouse{0U};
    std::uint32_t district{0U};
    std::uint32_t customer_warehouse{0U};
    std::uint32_t customer_district{0U};
    std::uint32_t customer{0U};
    std::string customer_last;
    bool by_last{false};
    float amount{0.0F};
    std::uint32_t amount_cents{0U};
};

struct PaymentCustomer final {
    std::uint32_t id{0U};
    std::string credit;
    std::string old_data;
    float balance{0.0F};
    float ytd_payment{0.0F};
};

struct LocationCheck final {
    std::size_t before_index{0U};
    std::size_t after_index{0U};
    std::string_view label;
};

auto generate_input(
    TransactionGenerator& generator,
    const RouteSelection route,
    const DatasetScale scale,
    const RoutingConfig routing
) -> PaymentInput {
    const bool remote =
        scale.warehouses > 1U && generator.value(1U, 100U) <= routing.payment_remote_percent;
    const std::uint32_t customer_warehouse =
        remote ? other_warehouse(generator, scale.warehouses, route.home_warehouse)
               : route.home_warehouse;
    const std::uint32_t customer_district =
        remote ? generator.value(1U, scale.districts_per_warehouse) : route.district;
    const std::uint32_t amount_cents = generator.value(100U, 500'000U);
    return PaymentInput{
        .warehouse = route.home_warehouse,
        .district = route.district,
        .customer_warehouse = customer_warehouse,
        .customer_district = customer_district,
        .customer = generator.nurand(1'023U, 1U, scale.customers_per_district),
        .customer_last = tpcc_last_name(generator.nurand(255U, 0U, 999U)),
        .by_last = generator.value(1U, 100U) <= 60U,
        .amount = static_cast<float>(amount_cents) / 100.0F,
        .amount_cents = amount_cents,
    };
}

auto exact_float(const float left, const float right) noexcept -> bool {
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}

auto bad_credit_data(
    const PaymentInput& input,
    const std::uint32_t customer,
    const std::string_view old_data
) -> std::string {
    std::ostringstream prefix;
    prefix << customer << ' ' << input.customer_district << ' ' << input.customer_warehouse << ' '
           << input.district << ' ' << input.warehouse << ' ' << input.amount_cents / 100U << '.'
           << std::setfill('0') << std::setw(2) << input.amount_cents % 100U << "0000 | ";
    std::string data = prefix.str();
    data.append(old_data);
    data.resize(std::min<std::size_t>(50U, data.size()));
    return data;
}

auto read_batch(const PaymentInput& input) -> std::vector<protocol::RmdbBatchOperation> {
    const std::uint16_t customer_statement =
        input.by_last ? statement::kPaymentCustomerByLast : statement::kPaymentCustomerById;
    std::vector<types::Value> customer_parameters{
        integer(input.customer_warehouse),
        integer(input.customer_district),
    };
    customer_parameters.push_back(
        input.by_last ? text(input.customer_last) : integer(input.customer)
    );
    std::vector<protocol::RmdbBatchOperation> operations{
        operation(statement::kBegin),
        operation(statement::kPaymentWarehouseSelect, {integer(input.warehouse)}),
        operation(
            statement::kPaymentWarehouseUpdate,
            {money(input.amount), integer(input.warehouse)}
        ),
        operation(statement::kPaymentWarehouseAfterUpdate, {integer(input.warehouse)}),
        operation(
            statement::kPaymentDistrictSelect,
            {integer(input.warehouse), integer(input.district)}
        ),
        operation(
            statement::kPaymentDistrictUpdate,
            {money(input.amount), integer(input.warehouse), integer(input.district)}
        ),
        operation(
            statement::kPaymentDistrictAfterUpdate,
            {integer(input.warehouse), integer(input.district)}
        ),
        operation(customer_statement, std::move(customer_parameters)),
    };
    if (input.by_last) {
        operations.push_back(operation(
            statement::kCustomerByLastCount,
            {
                integer(input.customer_warehouse),
                integer(input.customer_district),
                text(input.customer_last),
            }
        ));
    }
    return operations;
}

auto validate_location_update(
    const protocol::RmdbBatchResponse& response,
    const LocationCheck check,
    const float amount
) -> base::Result<std::string> {
    auto rows = query_rows(response, check.before_index);
    if (!rows) {
        return base::fail(std::move(rows).error());
    }
    if (rows->size() != 1U || rows->front().size() != 2U
        || !valid_char(rows->front()[0U], 10U, false)) {
        return transaction_error(
            std::string{"Payment "}.append(check.label).append(" row was malformed")
        );
    }
    auto name = value_text(rows->front()[0U]);
    auto before = value_f32(rows->front()[1U]);
    auto after = scalar_f32(response, check.after_index);
    if (!name) {
        return base::fail(std::move(name).error());
    }
    if (!before) {
        return base::fail(std::move(before).error());
    }
    if (!after) {
        return base::fail(std::move(after).error());
    }
    if (!exact_float(before.value() + amount, after.value())) {
        return transaction_error(
            std::string{"Payment "}.append(check.label).append(" FLOAT32 update was not bit-exact")
        );
    }
    return std::string{name.value()};
}

auto validate_by_last_rows(
    const Rows customers,
    const PaymentInput& input,
    const DatasetScale scale
) -> base::Status {
    std::set<std::int32_t> ids;
    std::string_view previous_first;
    bool have_previous = false;
    for (const auto& row : customers) {
        if (row.size() != 7U) {
            return transaction_error("Payment by-last customer row was malformed");
        }
        auto id = value_i32(row[0U]);
        auto first_name = value_text(row[3U]);
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
            || !ids.insert(id.value()).second || !valid_char(row[3U], 16U, false)
            || last_name.value() != input.customer_last
            || (have_previous && previous_first > first_name.value())) {
            return transaction_error("Payment by-last customer set was malformed");
        }
        previous_first = first_name.value();
        have_previous = true;
    }
    return {};
}

auto select_customer(
    const protocol::RmdbBatchResponse& response,
    const PaymentInput& input,
    const DatasetScale scale
) -> base::Result<PaymentCustomer> {
    auto customers = query_rows(response, 7U);
    if (!customers) {
        return base::fail(std::move(customers).error());
    }
    if (customers->empty()) {
        return transaction_error("Payment customer lookup returned no rows");
    }
    if (input.by_last) {
        auto count = scalar_i32(response, 8U);
        if (!count) {
            return base::fail(std::move(count).error());
        }
        if (std::cmp_not_equal(count.value(), customers->size())) {
            return transaction_error("Payment by-last lookup omitted or duplicated customers");
        }
        auto valid = validate_by_last_rows(customers.value(), input, scale);
        if (!valid) {
            return base::fail(std::move(valid).error());
        }
    } else if (customers->size() != 1U) {
        return transaction_error("Payment customer-by-id was not unique");
    }

    const auto& selected =
        input.by_last ? customers.value()[(customers->size() - 1U) / 2U] : customers->front();
    const std::size_t expected_columns = input.by_last ? 7U : 5U;
    if (selected.size() != expected_columns) {
        return transaction_error("Payment selected customer was malformed");
    }
    auto selected_id = value_i32(selected[0U]);
    auto credit = value_text(selected[1U]);
    auto old_data = value_text(selected[2U]);
    const std::size_t balance_index = input.by_last ? 5U : 3U;
    const std::size_t ytd_index = input.by_last ? 6U : 4U;
    auto balance = value_f32(selected[balance_index]);
    auto ytd = value_f32(selected[ytd_index]);
    if (!selected_id) {
        return base::fail(std::move(selected_id).error());
    }
    if (!credit) {
        return base::fail(std::move(credit).error());
    }
    if (!old_data) {
        return base::fail(std::move(old_data).error());
    }
    if (!balance) {
        return base::fail(std::move(balance).error());
    }
    if (!ytd) {
        return base::fail(std::move(ytd).error());
    }
    if (selected_id.value() < 1
        || std::cmp_greater(selected_id.value(), scale.customers_per_district)
        || (!input.by_last && std::cmp_not_equal(selected_id.value(), input.customer))
        || (credit.value() != "BC" && credit.value() != "GC")
        || !valid_char(selected[2U], 500U, true)) {
        return transaction_error("Payment selected customer fields were malformed");
    }
    return PaymentCustomer{
        .id = static_cast<std::uint32_t>(selected_id.value()),
        .credit = std::string{credit.value()},
        .old_data = std::string{old_data.value()},
        .balance = balance.value(),
        .ytd_payment = ytd.value(),
    };
}

auto customer_update(const PaymentInput& input, const PaymentCustomer& customer)
    -> protocol::RmdbBatchOperation {
    if (customer.credit == "BC") {
        return operation(
            statement::kPaymentCustomerUpdateBad,
            {
                money(input.amount),
                text(bad_credit_data(input, customer.id, customer.old_data)),
                integer(input.customer_warehouse),
                integer(input.customer_district),
                integer(customer.id),
            }
        );
    }
    return operation(
        statement::kPaymentCustomerUpdateGood,
        {
            money(input.amount),
            integer(input.customer_warehouse),
            integer(input.customer_district),
            integer(customer.id),
        }
    );
}

auto write_payment(
    SqlSession& session,
    const PaymentInput& input,
    const PaymentCustomer& customer,
    const std::string_view warehouse_name,
    const std::string_view district_name
) -> base::Result<AttemptResult> {
    std::string history_data{warehouse_name};
    history_data.append("    ");
    history_data.append(district_name);
    history_data.resize(std::min<std::size_t>(24U, history_data.size()));
    std::vector<protocol::RmdbBatchOperation> operations{
        customer_update(input, customer),
        operation(
            statement::kPaymentInsertHistory,
            {
                integer(customer.id),
                integer(input.customer_district),
                integer(input.customer_warehouse),
                integer(input.district),
                integer(input.warehouse),
                text(timestamp()),
                money(input.amount),
                text(std::move(history_data)),
            }
        ),
        operation(
            statement::kPaymentCustomerAfterUpdate,
            {
                integer(input.customer_warehouse),
                integer(input.customer_district),
                integer(customer.id),
            }
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
    auto updated = query_rows(**response, 2U);
    if (!updated) {
        return base::fail(std::move(updated).error());
    }
    if (updated->size() != 1U || updated->front().size() != 2U) {
        return transaction_error("Payment customer post-update row was malformed");
    }
    auto balance_after = value_f32(updated->front()[0U]);
    auto ytd_after = value_f32(updated->front()[1U]);
    if (!balance_after) {
        return base::fail(std::move(balance_after).error());
    }
    if (!ytd_after) {
        return base::fail(std::move(ytd_after).error());
    }
    if (!exact_float(customer.balance - input.amount, balance_after.value())) {
        return transaction_error("Payment customer balance FLOAT32 update was not bit-exact");
    }
    if (!exact_float(customer.ytd_payment + input.amount, ytd_after.value())) {
        return transaction_error("Payment customer YTD FLOAT32 update was not bit-exact");
    }
    return AttemptResult{.disposition = AttemptDisposition::kCommitted};
}

auto attempt(SqlSession& session, const PaymentInput& input, const DatasetScale scale)
    -> base::Result<AttemptResult> {
    const auto operations = read_batch(input);
    auto response = session.batch(operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (!response->has_value()) {
        return AttemptResult{.disposition = AttemptDisposition::kAborted};
    }
    auto warehouse_name = validate_location_update(
        **response,
        LocationCheck{.before_index = 1U, .after_index = 3U, .label = "warehouse"},
        input.amount
    );
    if (!warehouse_name) {
        return base::fail(std::move(warehouse_name).error());
    }
    auto district_name = validate_location_update(
        **response,
        LocationCheck{.before_index = 4U, .after_index = 6U, .label = "district"},
        input.amount
    );
    if (!district_name) {
        return base::fail(std::move(district_name).error());
    }
    auto customer = select_customer(**response, input, scale);
    if (!customer) {
        return base::fail(std::move(customer).error());
    }
    return write_payment(
        session,
        input,
        customer.value(),
        warehouse_name.value(),
        district_name.value()
    );
}

}  // namespace

auto run_payment(const TransactionExecution& execution, TransactionCounters& counters)
    -> base::Result<CompletionData> {
    const PaymentInput input =
        generate_input(execution.generator, execution.route, execution.scale, execution.routing);
    return retry_transaction(
        execution.session,
        TransactionKind::kPayment,
        execution.retry_limit,
        counters,
        [&execution, &input]() {
            return attempt(execution.session, input, execution.scale);
        }
    );
}

}  // namespace db::tpcc
