#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "config.hpp"
#include "random.hpp"

namespace db::tpcc {

enum class TransactionKind : std::uint8_t {
    kNewOrder,
    kPayment,
    kOrderStatus,
    kDelivery,
    kStockLevel,
};

inline constexpr std::array<TransactionKind, 5> kTransactionKinds{
    TransactionKind::kNewOrder,
    TransactionKind::kPayment,
    TransactionKind::kOrderStatus,
    TransactionKind::kDelivery,
    TransactionKind::kStockLevel,
};

auto transaction_index(TransactionKind kind) noexcept -> std::size_t;
auto transaction_name(TransactionKind kind) noexcept -> const char*;

struct RouteSelection final {
    std::uint32_t home_warehouse{0U};
    std::uint32_t district{0U};
    bool hot_warehouse{false};
    bool hot_district{false};
};

class ClientRouter;

class PhaseRouting final {
public:
    auto client(std::uint32_t client_id) const noexcept -> ClientRouter;
    auto slot_index(std::uint32_t client_id, std::uint64_t transaction_number) const noexcept
        -> std::size_t;
    auto roulette() const noexcept -> std::span<const std::uint32_t>;

private:
    std::uint32_t seed_{0U};
    std::uint32_t phase_id_{0U};
    DatasetScale scale_{};
    RoutingConfig config_{};
    std::array<std::uint32_t, 160> roulette_{};
    std::array<bool, 51> hot_mask_{};
    std::array<std::uint32_t, 51> hot_districts_{};

    friend class ClientRouter;
    friend class WorkloadRouting;
};

class ClientRouter final {
public:
    auto next(std::uint64_t transaction_number) noexcept -> RouteSelection;

private:
    ClientRouter(const PhaseRouting& phase, std::uint32_t client_id) noexcept;

    const PhaseRouting* phase_;
    std::uint32_t client_id_;
    DeterministicRandom random_;

    friend class PhaseRouting;
};

class WorkloadRouting final {
public:
    static auto derive(std::uint32_t seed) -> WorkloadRouting;

    auto phase(std::uint32_t phase_id) const -> PhaseRouting;
    auto hot_items() const noexcept -> std::span<const std::uint32_t>;
    auto hot_warehouses() const noexcept -> std::span<const std::uint32_t>;
    auto is_hot(std::uint32_t warehouse) const noexcept -> bool;

private:
    std::uint32_t seed_{0U};
    DatasetScale scale_{};
    RoutingConfig config_{};
    std::array<std::uint32_t, 4> hot_warehouses_{};
    std::array<bool, 51> hot_mask_{};
    std::array<std::uint32_t, 51> hot_districts_{};
    std::array<std::uint32_t, 24> hot_items_{};
};

class TransactionGenerator final {
public:
    TransactionGenerator(
        std::uint32_t workload_seed,
        std::uint32_t phase,
        std::uint32_t client
    ) noexcept;

    auto next_kind() noexcept -> TransactionKind;
    auto value(std::uint32_t minimum, std::uint32_t maximum) noexcept -> std::uint32_t;
    auto nurand(std::uint32_t a, std::uint32_t minimum, std::uint32_t maximum) noexcept
        -> std::uint32_t;

private:
    void refill_deck() noexcept;

    DeterministicRandom random_;
    std::array<TransactionKind, 100> deck_{};
    std::size_t deck_index_{100U};
};

auto tpcc_last_name(std::uint32_t number) -> std::string;

}  // namespace db::tpcc
