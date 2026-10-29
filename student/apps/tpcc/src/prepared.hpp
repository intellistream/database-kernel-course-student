#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/protocol/rmdb_client.hpp"

namespace db::tpcc::statement {

inline constexpr std::uint16_t kBegin = 1U;
inline constexpr std::uint16_t kCommit = 2U;
inline constexpr std::uint16_t kAbort = 3U;
inline constexpr std::uint16_t kNewOrderCustomer = 10U;
inline constexpr std::uint16_t kNewOrderDistrictUpdate = 11U;
inline constexpr std::uint16_t kNewOrderDistrictSelect = 12U;
inline constexpr std::uint16_t kNewOrderInsertOrder = 13U;
inline constexpr std::uint16_t kNewOrderInsertNewOrder = 14U;
inline constexpr std::uint16_t kNewOrderItem = 15U;
inline constexpr std::uint16_t kNewOrderStockLock = 16U;
inline constexpr std::uint16_t kNewOrderStockUpdateHigh = 27U;
inline constexpr std::uint16_t kNewOrderStockUpdateLow = 28U;
inline constexpr std::uint16_t kNewOrderInsertLine = 29U;
inline constexpr std::uint16_t kPaymentWarehouseUpdate = 40U;
inline constexpr std::uint16_t kPaymentWarehouseSelect = 41U;
inline constexpr std::uint16_t kPaymentDistrictUpdate = 42U;
inline constexpr std::uint16_t kPaymentDistrictSelect = 43U;
inline constexpr std::uint16_t kPaymentCustomerById = 44U;
inline constexpr std::uint16_t kPaymentCustomerByLast = 45U;
inline constexpr std::uint16_t kPaymentCustomerUpdateGood = 46U;
inline constexpr std::uint16_t kPaymentCustomerUpdateBad = 47U;
inline constexpr std::uint16_t kPaymentInsertHistory = 48U;
inline constexpr std::uint16_t kCustomerByLastCount = 49U;
inline constexpr std::uint16_t kPaymentWarehouseAfterUpdate = 50U;
inline constexpr std::uint16_t kPaymentDistrictAfterUpdate = 51U;
inline constexpr std::uint16_t kPaymentCustomerAfterUpdate = 52U;
inline constexpr std::uint16_t kOrderStatusCustomerById = 60U;
inline constexpr std::uint16_t kOrderStatusCustomerByLast = 61U;
inline constexpr std::uint16_t kOrderStatusMaxOrder = 62U;
inline constexpr std::uint16_t kOrderStatusLines = 63U;
inline constexpr std::uint16_t kOrderStatusOrderDetails = 64U;
inline constexpr std::uint16_t kOrderStatusNewerCount = 65U;
inline constexpr std::uint16_t kDeliveryMinOrder = 70U;
inline constexpr std::uint16_t kDeliveryLockOrder = 71U;
inline constexpr std::uint16_t kDeliveryClaimCount = 72U;
inline constexpr std::uint16_t kDeliveryDeleteOrder = 73U;
inline constexpr std::uint16_t kDeliveryCustomer = 74U;
inline constexpr std::uint16_t kDeliveryUpdateOrder = 75U;
inline constexpr std::uint16_t kDeliveryUpdateLines = 76U;
inline constexpr std::uint16_t kDeliveryLines = 77U;
inline constexpr std::uint16_t kDeliveryUpdateCustomer = 78U;
inline constexpr std::uint16_t kDeliveryEarlierCount = 79U;
inline constexpr std::uint16_t kDeliveryCustomerBalanceBefore = 80U;
inline constexpr std::uint16_t kDeliveryCustomerBalanceAfter = 81U;
inline constexpr std::uint16_t kDeliverySumAmount = 82U;
inline constexpr std::uint16_t kStockLevelNextOrder = 90U;
inline constexpr std::uint16_t kStockLevelCount = 91U;
inline constexpr std::size_t kRankedStatementCount = 57U;

constexpr auto stock_district(const std::uint32_t district) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(16U + district);
}

auto definitions() -> std::vector<protocol::RmdbPreparedStatement>;
auto name(std::uint16_t statement_id) noexcept -> const char*;

}  // namespace db::tpcc::statement
