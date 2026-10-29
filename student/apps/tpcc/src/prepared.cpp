#include "prepared.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace db::tpcc::statement {
namespace {

using protocol::WireDataType;
using Statement = protocol::RmdbPreparedStatement;

inline constexpr WireDataType kInteger = WireDataType::kInt32;
inline constexpr WireDataType kFloat = WireDataType::kFloat32;
inline constexpr WireDataType kCharacter = WireDataType::kChar;

auto command(const std::uint16_t id, std::string sql, std::vector<WireDataType> parameters = {})
    -> Statement {
    return Statement::command(id, std::move(sql), std::move(parameters));
}

auto query(
    const std::uint16_t id,
    std::string sql,
    std::vector<WireDataType> parameters,
    std::vector<WireDataType> results
) -> Statement {
    return Statement::query(id, std::move(sql), std::move(parameters), std::move(results));
}

}  // namespace

// Keep the protocol-assigned 57-entry dictionary contiguous for one-pass contract review.
// NOLINTNEXTLINE(readability-function-size)
auto definitions() -> std::vector<Statement> {
    std::vector<Statement> statements{
        command(kBegin, "begin;"),
        command(kCommit, "commit;"),
        command(kAbort, "abort;"),
        query(
            kNewOrderCustomer,
            "select c_discount, c_last, c_credit, w_tax from customer, warehouse "
            "where w_id=$1 and c_w_id=w_id and c_d_id=$2 and c_id=$3;",
            {kInteger, kInteger, kInteger},
            {kFloat, kCharacter, kCharacter, kFloat}
        ),
        command(
            kNewOrderDistrictUpdate,
            "update district set d_next_o_id=d_next_o_id+1 where d_w_id=$1 and d_id=$2;",
            {kInteger, kInteger}
        ),
        query(
            kNewOrderDistrictSelect,
            "select d_next_o_id, d_tax from district where d_w_id=$1 and d_id=$2;",
            {kInteger, kInteger},
            {kInteger, kFloat}
        ),
        command(
            kNewOrderInsertOrder,
            "insert into orders values($1,$2,$3,$4,$5,0,$6,$7);",
            {kInteger, kInteger, kInteger, kInteger, kCharacter, kInteger, kInteger}
        ),
        command(
            kNewOrderInsertNewOrder,
            "insert into new_orders values($1,$2,$3);",
            {kInteger, kInteger, kInteger}
        ),
        query(
            kNewOrderItem,
            "select i_id, i_price, i_name, i_data from item where i_id=$1;",
            {kInteger},
            {kInteger, kFloat, kCharacter, kCharacter}
        ),
        command(
            kNewOrderStockLock,
            "update stock set s_ytd=s_ytd where s_w_id=$1 and s_i_id=$2;",
            {kInteger, kInteger}
        ),
        command(
            kNewOrderStockUpdateHigh,
            "update stock set s_quantity=s_quantity-$1, s_ytd=s_ytd+$1, "
            "s_order_cnt=s_order_cnt+1, s_remote_cnt=s_remote_cnt+$2 "
            "where s_w_id=$3 and s_i_id=$4 and s_quantity>=$5;",
            {kInteger, kInteger, kInteger, kInteger, kInteger}
        ),
        command(
            kNewOrderStockUpdateLow,
            "update stock set s_quantity=s_quantity-$1+91, s_ytd=s_ytd+$1, "
            "s_order_cnt=s_order_cnt+1, s_remote_cnt=s_remote_cnt+$2 "
            "where s_w_id=$3 and s_i_id=$4 and s_quantity<$5;",
            {kInteger, kInteger, kInteger, kInteger, kInteger}
        ),
        command(
            kNewOrderInsertLine,
            "insert into order_line values($1,$2,$3,$4,$5,$6,'',$7,$8,$9);",
            {
                kInteger,
                kInteger,
                kInteger,
                kInteger,
                kInteger,
                kInteger,
                kInteger,
                kFloat,
                kCharacter,
            }
        ),
        command(
            kPaymentWarehouseUpdate,
            "update warehouse set w_ytd=w_ytd+$1 where w_id=$2;",
            {kFloat, kInteger}
        ),
        query(
            kPaymentWarehouseSelect,
            "select w_name, w_ytd from warehouse where w_id=$1;",
            {kInteger},
            {kCharacter, kFloat}
        ),
        command(
            kPaymentDistrictUpdate,
            "update district set d_ytd=d_ytd+$1 where d_w_id=$2 and d_id=$3;",
            {kFloat, kInteger, kInteger}
        ),
        query(
            kPaymentDistrictSelect,
            "select d_name, d_ytd from district where d_w_id=$1 and d_id=$2;",
            {kInteger, kInteger},
            {kCharacter, kFloat}
        ),
        query(
            kPaymentCustomerById,
            "select c_id, c_credit, c_data, c_balance, c_ytd_payment from customer "
            "where c_w_id=$1 and c_d_id=$2 and c_id=$3;",
            {kInteger, kInteger, kInteger},
            {kInteger, kCharacter, kCharacter, kFloat, kFloat}
        ),
        query(
            kPaymentCustomerByLast,
            "select c_id, c_credit, c_data, c_first, c_last, c_balance, c_ytd_payment "
            "from customer where c_w_id=$1 and c_d_id=$2 and c_last=$3 order by c_first;",
            {kInteger, kInteger, kCharacter},
            {kInteger, kCharacter, kCharacter, kCharacter, kCharacter, kFloat, kFloat}
        ),
        command(
            kPaymentCustomerUpdateGood,
            "update customer set c_balance=c_balance-$1, c_ytd_payment=c_ytd_payment+$1, "
            "c_payment_cnt=c_payment_cnt+1 where c_w_id=$2 and c_d_id=$3 and c_id=$4;",
            {kFloat, kInteger, kInteger, kInteger}
        ),
        command(
            kPaymentCustomerUpdateBad,
            "update customer set c_balance=c_balance-$1, c_ytd_payment=c_ytd_payment+$1, "
            "c_payment_cnt=c_payment_cnt+1, c_data=$2 "
            "where c_w_id=$3 and c_d_id=$4 and c_id=$5;",
            {kFloat, kCharacter, kInteger, kInteger, kInteger}
        ),
        command(
            kPaymentInsertHistory,
            "insert into history values($1,$2,$3,$4,$5,$6,$7,$8);",
            {
                kInteger,
                kInteger,
                kInteger,
                kInteger,
                kInteger,
                kCharacter,
                kFloat,
                kCharacter,
            }
        ),
        query(
            kCustomerByLastCount,
            "select COUNT(*) from customer where c_w_id=$1 and c_d_id=$2 and c_last=$3;",
            {kInteger, kInteger, kCharacter},
            {kInteger}
        ),
        query(
            kPaymentWarehouseAfterUpdate,
            "select w_ytd from warehouse where w_id=$1;",
            {kInteger},
            {kFloat}
        ),
        query(
            kPaymentDistrictAfterUpdate,
            "select d_ytd from district where d_w_id=$1 and d_id=$2;",
            {kInteger, kInteger},
            {kFloat}
        ),
        query(
            kPaymentCustomerAfterUpdate,
            "select c_balance, c_ytd_payment from customer "
            "where c_w_id=$1 and c_d_id=$2 and c_id=$3;",
            {kInteger, kInteger, kInteger},
            {kFloat, kFloat}
        ),
        query(
            kOrderStatusCustomerById,
            "select c_id, c_balance, c_first, c_middle, c_last from customer "
            "where c_w_id=$1 and c_d_id=$2 and c_id=$3;",
            {kInteger, kInteger, kInteger},
            {kInteger, kFloat, kCharacter, kCharacter, kCharacter}
        ),
        query(
            kOrderStatusCustomerByLast,
            "select c_id, c_balance, c_first, c_middle, c_last from customer "
            "where c_w_id=$1 and c_d_id=$2 and c_last=$3 order by c_first;",
            {kInteger, kInteger, kCharacter},
            {kInteger, kFloat, kCharacter, kCharacter, kCharacter}
        ),
        query(
            kOrderStatusMaxOrder,
            "select MAX(o_id) from orders where o_w_id=$1 and o_d_id=$2 and o_c_id=$3;",
            {kInteger, kInteger, kInteger},
            {kInteger}
        ),
        query(
            kOrderStatusLines,
            "select ol_number, ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d "
            "from order_line where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3 "
            "order by ol_number;",
            {kInteger, kInteger, kInteger},
            {kInteger, kInteger, kInteger, kInteger, kFloat, kCharacter}
        ),
        query(
            kOrderStatusOrderDetails,
            "select o_c_id, o_ol_cnt, o_entry_d, o_carrier_id from orders "
            "where o_w_id=$1 and o_d_id=$2 and o_id=$3;",
            {kInteger, kInteger, kInteger},
            {kInteger, kInteger, kCharacter, kInteger}
        ),
        query(
            kOrderStatusNewerCount,
            "select COUNT(*) from orders where o_w_id=$1 and o_d_id=$2 and o_c_id=$3 "
            "and o_id>$4;",
            {kInteger, kInteger, kInteger, kInteger},
            {kInteger}
        ),
        query(
            kDeliveryMinOrder,
            "select MIN(no_o_id) from new_orders where no_w_id=$1 and no_d_id=$2;",
            {kInteger, kInteger},
            {kInteger}
        ),
        command(
            kDeliveryLockOrder,
            "update new_orders set no_o_id=no_o_id where no_w_id=$1 and no_d_id=$2 "
            "and no_o_id=$3;",
            {kInteger, kInteger, kInteger}
        ),
        query(
            kDeliveryClaimCount,
            "select COUNT(*) from new_orders where no_w_id=$1 and no_d_id=$2 and no_o_id=$3;",
            {kInteger, kInteger, kInteger},
            {kInteger}
        ),
        command(
            kDeliveryDeleteOrder,
            "delete from new_orders where no_w_id=$1 and no_d_id=$2 and no_o_id=$3;",
            {kInteger, kInteger, kInteger}
        ),
        query(
            kDeliveryCustomer,
            "select o_c_id, o_ol_cnt, o_carrier_id from orders "
            "where o_w_id=$1 and o_d_id=$2 and o_id=$3;",
            {kInteger, kInteger, kInteger},
            {kInteger, kInteger, kInteger}
        ),
        command(
            kDeliveryUpdateOrder,
            "update orders set o_carrier_id=$1 where o_w_id=$2 and o_d_id=$3 and o_id=$4;",
            {kInteger, kInteger, kInteger, kInteger}
        ),
        command(
            kDeliveryUpdateLines,
            "update order_line set ol_delivery_d=$1 where ol_w_id=$2 and ol_d_id=$3 "
            "and ol_o_id=$4;",
            {kCharacter, kInteger, kInteger, kInteger}
        ),
        query(
            kDeliveryLines,
            "select ol_number, ol_amount, ol_delivery_d from order_line "
            "where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3 order by ol_number;",
            {kInteger, kInteger, kInteger},
            {kInteger, kFloat, kCharacter}
        ),
        command(
            kDeliveryUpdateCustomer,
            "update customer set c_balance=c_balance+$1, c_delivery_cnt=c_delivery_cnt+1 "
            "where c_w_id=$2 and c_d_id=$3 and c_id=$4;",
            {kFloat, kInteger, kInteger, kInteger}
        ),
        query(
            kDeliveryEarlierCount,
            "select COUNT(*) from new_orders where no_w_id=$1 and no_d_id=$2 and no_o_id<$3;",
            {kInteger, kInteger, kInteger},
            {kInteger}
        ),
        query(
            kDeliveryCustomerBalanceBefore,
            "select c_balance from customer where c_w_id=$1 and c_d_id=$2 and c_id=$3;",
            {kInteger, kInteger, kInteger},
            {kFloat}
        ),
        query(
            kDeliveryCustomerBalanceAfter,
            "select c_balance from customer where c_w_id=$1 and c_d_id=$2 and c_id=$3;",
            {kInteger, kInteger, kInteger},
            {kFloat}
        ),
        query(
            kDeliverySumAmount,
            "select SUM(ol_amount) from order_line where ol_w_id=$1 and ol_d_id=$2 "
            "and ol_o_id=$3;",
            {kInteger, kInteger, kInteger},
            {kFloat}
        ),
        query(
            kStockLevelNextOrder,
            "select d_next_o_id from district where d_w_id=$1 and d_id=$2;",
            {kInteger, kInteger},
            {kInteger}
        ),
        query(
            kStockLevelCount,
            "select COUNT(DISTINCT (s_i_id)) from order_line, stock "
            "where ol_w_id=$1 and ol_d_id=$2 and ol_o_id<$3 and ol_o_id>=$4 "
            "and s_w_id=$1 and s_i_id=ol_i_id and s_quantity<$5;",
            {kInteger, kInteger, kInteger, kInteger, kInteger},
            {kInteger}
        ),
    };

    for (std::uint32_t district = 1U; district <= 10U; ++district) {
        const std::string suffix =
            district < 10U ? "0" + std::to_string(district) : std::to_string(district);
        statements.push_back(query(
            stock_district(district),
            "select s_quantity, s_dist_" + suffix + " from stock where s_w_id=$1 and s_i_id=$2;",
            {kInteger, kInteger},
            {kInteger, kCharacter}
        ));
    }
    std::sort(
        statements.begin(),
        statements.end(),
        [](const Statement& left, const Statement& right) {
            return left.statement_id < right.statement_id;
        }
    );
    return statements;
}

auto name(const std::uint16_t statement_id) noexcept -> const char* {
    switch (statement_id) {
        case kBegin:
            return "begin";
        case kCommit:
            return "commit";
        case kAbort:
            return "abort";
        case kNewOrderCustomer:
            return "new_order_customer";
        case kNewOrderDistrictUpdate:
            return "new_order_district_update";
        case kNewOrderDistrictSelect:
            return "new_order_district_select";
        case kNewOrderInsertOrder:
            return "new_order_insert_order";
        case kNewOrderInsertNewOrder:
            return "new_order_insert_new_order";
        case kNewOrderItem:
            return "new_order_item";
        case kNewOrderStockLock:
            return "new_order_stock_lock";
        case 17U:
        case 18U:
        case 19U:
        case 20U:
        case 21U:
        case 22U:
        case 23U:
        case 24U:
        case 25U:
        case 26U:
            return "new_order_stock_district";
        case kNewOrderStockUpdateHigh:
            return "new_order_stock_update_high";
        case kNewOrderStockUpdateLow:
            return "new_order_stock_update_low";
        case kNewOrderInsertLine:
            return "new_order_insert_line";
        case kPaymentWarehouseUpdate:
            return "payment_warehouse_update";
        case kPaymentWarehouseSelect:
            return "payment_warehouse_select";
        case kPaymentDistrictUpdate:
            return "payment_district_update";
        case kPaymentDistrictSelect:
            return "payment_district_select";
        case kPaymentCustomerById:
            return "payment_customer_by_id";
        case kPaymentCustomerByLast:
            return "payment_customer_by_last";
        case kPaymentCustomerUpdateGood:
            return "payment_customer_update_good";
        case kPaymentCustomerUpdateBad:
            return "payment_customer_update_bad";
        case kPaymentInsertHistory:
            return "payment_insert_history";
        case kCustomerByLastCount:
            return "customer_by_last_count";
        case kPaymentWarehouseAfterUpdate:
            return "payment_warehouse_after_update";
        case kPaymentDistrictAfterUpdate:
            return "payment_district_after_update";
        case kPaymentCustomerAfterUpdate:
            return "payment_customer_after_update";
        case kOrderStatusCustomerById:
            return "order_status_customer_by_id";
        case kOrderStatusCustomerByLast:
            return "order_status_customer_by_last";
        case kOrderStatusMaxOrder:
            return "order_status_max_order";
        case kOrderStatusLines:
            return "order_status_lines";
        case kOrderStatusOrderDetails:
            return "order_status_order_details";
        case kOrderStatusNewerCount:
            return "order_status_newer_count";
        case kDeliveryMinOrder:
            return "delivery_min_order";
        case kDeliveryLockOrder:
            return "delivery_lock_order";
        case kDeliveryClaimCount:
            return "delivery_claim_count";
        case kDeliveryDeleteOrder:
            return "delivery_delete_order";
        case kDeliveryCustomer:
            return "delivery_customer";
        case kDeliveryUpdateOrder:
            return "delivery_update_order";
        case kDeliveryUpdateLines:
            return "delivery_update_lines";
        case kDeliveryLines:
            return "delivery_lines";
        case kDeliveryUpdateCustomer:
            return "delivery_update_customer";
        case kDeliveryEarlierCount:
            return "delivery_earlier_count";
        case kDeliveryCustomerBalanceBefore:
            return "delivery_customer_balance_before";
        case kDeliveryCustomerBalanceAfter:
            return "delivery_customer_balance_after";
        case kDeliverySumAmount:
            return "delivery_sum_amount";
        case kStockLevelNextOrder:
            return "stock_level_next_order";
        case kStockLevelCount:
            return "stock_level_count";
        default:
            return "unknown";
    }
}

}  // namespace db::tpcc::statement
