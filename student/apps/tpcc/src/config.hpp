#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

#include "db/base/result.hpp"
#include "db/io/socket.hpp"

namespace db::tpcc {

enum class BenchmarkProfile : std::uint8_t {
    kQuick,
    kDebug,
    kFull,
};

struct DatasetScale final {
    std::uint32_t warehouses{50U};
    std::uint32_t districts_per_warehouse{10U};
    std::uint32_t customers_per_district{3'000U};
    std::uint32_t orders_per_district{3'000U};
    std::uint32_t first_unprocessed_order_id{2'101U};
    std::uint32_t items{100'000U};
    std::uint32_t stock_per_warehouse{100'000U};

    auto operator==(const DatasetScale&) const noexcept -> bool = default;
};

struct RoutingConfig final {
    std::uint32_t hot_warehouses{4U};
    std::uint32_t hot_warehouse_slots{26U};
    std::uint32_t roulette_slots{160U};
    std::uint32_t hot_district_percent{65U};
    std::uint32_t hot_items{24U};
    std::uint32_t hot_item_line_percent{25U};
    std::uint32_t new_order_remote_line_percent{8U};
    std::uint32_t payment_remote_percent{30U};

    auto operator==(const RoutingConfig&) const noexcept -> bool = default;
};

struct BenchmarkOptions final {
    io::TcpEndpoint endpoint{io::TcpEndpoint::loopback(8765U)};
    BenchmarkProfile profile{BenchmarkProfile::kDebug};
    std::uint32_t seed{1U};
    std::optional<std::filesystem::path> load_directory;
    bool sql_stats{false};
    bool show_help{false};
};

struct BenchmarkConfig final {
    DatasetScale scale{};
    RoutingConfig routing{};
    std::size_t clients{32U};
    std::chrono::seconds warmup{10};
    std::size_t ranked_rounds{3U};
    std::chrono::seconds ranked_duration{10};
    std::chrono::seconds phase_grace{120};
    std::uint32_t retry_limit{5U};
    std::chrono::seconds io_timeout{60};
    std::chrono::seconds load_timeout{900};
    std::chrono::seconds performance_timeout{3'300};
    std::array<std::uint32_t, 5> mix{45U, 43U, 4U, 4U, 4U};

    static auto for_profile(BenchmarkProfile profile) noexcept -> BenchmarkConfig;
};

auto parse_options(std::span<const std::string_view> arguments) -> base::Result<BenchmarkOptions>;
auto usage() noexcept -> std::string_view;

}  // namespace db::tpcc
