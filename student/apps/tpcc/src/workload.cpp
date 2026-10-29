#include "workload.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "array_access.hpp"

namespace db::tpcc {
namespace {

constexpr std::uint64_t kSlotGroupWidth = 32U;
constexpr std::uint64_t kSlotGroups = 5U;
constexpr std::uint64_t kSlotRotation = 13U;
constexpr std::uint32_t kNurandLastName = 223U;
constexpr std::uint32_t kNurandCustomerId = 259U;
constexpr std::uint32_t kNurandItemId = 7'911U;

constexpr auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
    value += 0x9e37'79b9'7f4a'7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58'476d'1ce4'e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d0'49bb'1331'11ebULL;
    return value ^ (value >> 31U);
}

constexpr auto domain_hash(const std::string_view domain) noexcept -> std::uint64_t {
    std::uint64_t hash = 1'469'598'103'934'665'603ULL;
    for (const char value : domain) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

auto stream_seed(
    const std::uint32_t seed,
    const std::string_view domain,
    const std::initializer_list<std::uint64_t> fields = {}
) noexcept -> std::uint64_t {
    std::uint64_t value = mix64(std::uint64_t{seed} ^ domain_hash(domain));
    for (const std::uint64_t field : fields) {
        value = mix64(value ^ mix64(field));
    }
    return value;
}

}  // namespace

auto transaction_index(const TransactionKind kind) noexcept -> std::size_t {
    switch (kind) {
        case TransactionKind::kNewOrder:
            return 0U;
        case TransactionKind::kPayment:
            return 1U;
        case TransactionKind::kOrderStatus:
            return 2U;
        case TransactionKind::kDelivery:
            return 3U;
        case TransactionKind::kStockLevel:
            return 4U;
    }
    std::unreachable();
}

auto transaction_name(const TransactionKind kind) noexcept -> const char* {
    switch (kind) {
        case TransactionKind::kNewOrder:
            return "new_order";
        case TransactionKind::kPayment:
            return "payment";
        case TransactionKind::kOrderStatus:
            return "order_status";
        case TransactionKind::kDelivery:
            return "delivery";
        case TransactionKind::kStockLevel:
            return "stock_level";
    }
    std::unreachable();
}

auto WorkloadRouting::derive(const std::uint32_t seed) -> WorkloadRouting {
    WorkloadRouting routing;
    routing.seed_ = seed;

    std::array<std::uint32_t, 50> warehouses{};
    for (std::size_t index = 0U; index < warehouses.size(); ++index) {
        array_element(warehouses, index) = static_cast<std::uint32_t>(index + 1U);
    }
    DeterministicRandom warehouse_random{stream_seed(seed, "rmdbroute/hot-warehouses/v1")};
    warehouse_random.shuffle<std::uint32_t>(warehouses);
    std::copy_n(
        warehouses.begin(),
        routing.hot_warehouses_.size(),
        routing.hot_warehouses_.begin()
    );
    std::sort(routing.hot_warehouses_.begin(), routing.hot_warehouses_.end());
    for (const std::uint32_t warehouse : routing.hot_warehouses_) {
        array_element(routing.hot_mask_, warehouse) = true;
    }

    DeterministicRandom district_random{stream_seed(seed, "rmdbroute/hot-districts/v1")};
    for (const std::uint32_t warehouse : routing.hot_warehouses_) {
        array_element(routing.hot_districts_, warehouse) =
            district_random.uniform(1U, routing.scale_.districts_per_warehouse);
    }

    DeterministicRandom item_random{stream_seed(seed, "rmdbroute/hot-items/v1")};
    std::set<std::uint32_t> selected_items;
    while (selected_items.size() < routing.hot_items_.size()) {
        selected_items.insert(item_random.uniform(1U, routing.scale_.items));
    }
    std::copy(selected_items.begin(), selected_items.end(), routing.hot_items_.begin());
    return routing;
}

auto WorkloadRouting::phase(const std::uint32_t phase_id) const -> PhaseRouting {
    PhaseRouting phase;
    phase.seed_ = seed_;
    phase.phase_id_ = phase_id;
    phase.scale_ = scale_;
    phase.config_ = config_;
    phase.hot_mask_ = hot_mask_;
    phase.hot_districts_ = hot_districts_;

    std::vector<std::uint32_t> cold;
    cold.reserve(scale_.warehouses - config_.hot_warehouses);
    for (std::uint32_t warehouse = 1U; warehouse <= scale_.warehouses; ++warehouse) {
        if (!array_element(hot_mask_, warehouse)) {
            cold.push_back(warehouse);
        }
    }
    std::vector<std::uint32_t> extra = cold;
    DeterministicRandom random{stream_seed(seed_, "rmdbroute/phase-roulette/v1", {phase_id})};
    random.shuffle<std::uint32_t>(extra);

    std::size_t position = 0U;
    for (const std::uint32_t warehouse : hot_warehouses_) {
        for (std::uint32_t slot = 0U; slot < config_.hot_warehouse_slots; ++slot) {
            array_element(phase.roulette_, position) = warehouse;
            ++position;
        }
    }
    for (const std::uint32_t warehouse : cold) {
        array_element(phase.roulette_, position) = warehouse;
        ++position;
    }
    while (position < phase.roulette_.size()) {
        array_element(phase.roulette_, position) =
            extra[position - cold.size() - hot_warehouses_.size() * config_.hot_warehouse_slots];
        ++position;
    }
    random.shuffle<std::uint32_t>(phase.roulette_);
    return phase;
}

auto WorkloadRouting::hot_items() const noexcept -> std::span<const std::uint32_t> {
    return hot_items_;
}

auto WorkloadRouting::hot_warehouses() const noexcept -> std::span<const std::uint32_t> {
    return hot_warehouses_;
}

auto WorkloadRouting::is_hot(const std::uint32_t warehouse) const noexcept -> bool {
    return array_element(hot_mask_, warehouse);
}

auto PhaseRouting::client(const std::uint32_t client_id) const noexcept -> ClientRouter {
    return ClientRouter{*this, client_id};
}

auto PhaseRouting::slot_index(
    const std::uint32_t client_id,
    const std::uint64_t transaction_number
) const noexcept -> std::size_t {
    const std::uint64_t group = transaction_number % kSlotGroups;
    const std::uint64_t rotation = transaction_number / kSlotGroups;
    return (std::uint64_t{client_id} + kSlotGroupWidth * group + kSlotRotation * rotation)
           % config_.roulette_slots;
}

auto PhaseRouting::roulette() const noexcept -> std::span<const std::uint32_t> {
    return roulette_;
}

ClientRouter::ClientRouter(const PhaseRouting& phase, const std::uint32_t client_id) noexcept
    : phase_{&phase},
      client_id_{client_id},
      random_{stream_seed(phase.seed_, "rmdbroute/client/v1", {phase.phase_id_, client_id, 32U})} {}

auto ClientRouter::next(const std::uint64_t transaction_number) noexcept -> RouteSelection {
    const std::uint32_t warehouse =
        array_element(phase_->roulette_, phase_->slot_index(client_id_, transaction_number));
    const bool hot_warehouse = array_element(phase_->hot_mask_, warehouse);
    if (!hot_warehouse) {
        return RouteSelection{
            .home_warehouse = warehouse,
            .district = random_.uniform(1U, phase_->scale_.districts_per_warehouse),
            .hot_warehouse = false,
            .hot_district = false,
        };
    }

    const std::uint32_t hot_district = array_element(phase_->hot_districts_, warehouse);
    if (random_.uniform(1U, 100U) <= phase_->config_.hot_district_percent) {
        return RouteSelection{
            .home_warehouse = warehouse,
            .district = hot_district,
            .hot_warehouse = true,
            .hot_district = true,
        };
    }
    const std::uint32_t candidate =
        random_.uniform(1U, phase_->scale_.districts_per_warehouse - 1U);
    return RouteSelection{
        .home_warehouse = warehouse,
        .district = candidate >= hot_district ? candidate + 1U : candidate,
        .hot_warehouse = true,
        .hot_district = false,
    };
}

TransactionGenerator::TransactionGenerator(
    const std::uint32_t workload_seed,
    const std::uint32_t phase,
    const std::uint32_t client
) noexcept
    : random_{stream_seed(workload_seed, "rmdbwork/client/v1", {phase, client, 32U})} {}

auto TransactionGenerator::next_kind() noexcept -> TransactionKind {
    if (deck_index_ == deck_.size()) {
        refill_deck();
    }
    const TransactionKind kind = array_element(deck_, deck_index_);
    ++deck_index_;
    return kind;
}

auto TransactionGenerator::value(const std::uint32_t minimum, const std::uint32_t maximum) noexcept
    -> std::uint32_t {
    return random_.uniform(minimum, maximum);
}

auto TransactionGenerator::nurand(
    const std::uint32_t a,
    const std::uint32_t minimum,
    const std::uint32_t maximum
) noexcept -> std::uint32_t {
    std::uint32_t constant = 0U;
    switch (a) {
        case 255U:
            constant = kNurandLastName;
            break;
        case 1'023U:
            constant = kNurandCustomerId;
            break;
        case 8'191U:
            constant = kNurandItemId;
            break;
        default:
            std::unreachable();
    }
    return ((value(0U, a) | value(minimum, maximum)) + constant) % (maximum - minimum + 1U)
           + minimum;
}

void TransactionGenerator::refill_deck() noexcept {
    std::size_t position = 0U;
    const auto append = [this, &position](const TransactionKind kind, const std::size_t count) {
        std::ranges::fill(std::span{deck_}.subspan(position, count), kind);
        position += count;
    };
    append(TransactionKind::kNewOrder, 45U);
    append(TransactionKind::kPayment, 43U);
    append(TransactionKind::kOrderStatus, 4U);
    append(TransactionKind::kDelivery, 4U);
    append(TransactionKind::kStockLevel, 4U);
    random_.shuffle<TransactionKind>(deck_);
    deck_index_ = 0U;
}

auto tpcc_last_name(const std::uint32_t number) -> std::string {
    static constexpr std::array<std::string_view, 10> kSyllables{
        "BARR",
        "OUGH",
        "ABLE",
        "PRII",
        "PRES",
        "ESEE",
        "ANTI",
        "CALL",
        "ATIO",
        "EING",
    };
    std::string result;
    result.reserve(16U);
    result.append(array_element(kSyllables, number / 100U));
    result.append(array_element(kSyllables, (number / 10U) % 10U));
    result.append(array_element(kSyllables, number % 10U));
    return result;
}

}  // namespace db::tpcc
