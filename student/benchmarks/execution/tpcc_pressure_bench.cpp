#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <latch>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/api/database.hpp"
#include "db/api/session.hpp"
#include "db/base/error.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::benchmarks {
namespace {

constexpr std::size_t kWarehouseCount = 50U;
constexpr std::size_t kDistrictsPerWarehouse = 10U;
constexpr std::size_t kDistrictCount = kWarehouseCount * kDistrictsPerWarehouse;
constexpr std::size_t kClientCount = 32U;
constexpr std::size_t kRouletteSlots = 160U;
constexpr std::size_t kRetryLimit = 5U;
constexpr std::size_t kWarmupTransactionsPerClient = 50U;
constexpr std::size_t kMeasuredTransactionsPerClient = 200U;
constexpr std::int64_t kInitialNextOrderId = 3'001;

constexpr std::array<std::uint32_t, 4U> kHotWarehouses{8U, 14U, 16U, 30U};
constexpr std::array<std::uint32_t, 4U> kHotDistricts{10U, 10U, 4U, 2U};

enum class TransactionKind : std::uint8_t {
    kNewOrder,
    kPayment,
    kOrderStatus,
    kDelivery,
    kStockLevel,
};

enum class StatementIndex : std::size_t {
    kWarehouseSelect,
    kWarehouseUpdate,
    kDistrictSelect,
    kDistrictPaymentUpdate,
    kDistrictNewOrderUpdate,
    kCount,
};

enum class ExpectedResult : std::uint8_t {
    kOneRow,
    kOneAffectedRow,
};

template <typename T, std::size_t N>
auto array_element(std::array<T, N>& values, const std::size_t index) noexcept -> T& {
    return std::span<T, N>{values}[index];
}

template <typename T, std::size_t N>
auto array_element(const std::array<T, N>& values, const std::size_t index) noexcept -> const T& {
    return std::span<const T, N>{values}[index];
}

struct Route final {
    std::uint32_t warehouse{0U};
    std::uint32_t district{0U};
    bool hot_warehouse{false};
};

struct PressureLedger final {
    std::array<std::uint64_t, kWarehouseCount> warehouse_payments{};
    std::array<std::uint64_t, kDistrictCount> district_payments{};
    std::array<std::uint64_t, kDistrictCount> district_new_orders{};
    std::uint64_t logical_transactions{0U};
    std::uint64_t attempts{0U};
    std::uint64_t committed{0U};
    std::uint64_t database_aborts{0U};
    std::uint64_t abandoned{0U};
    std::uint64_t hot_routes{0U};

    void merge(const PressureLedger& other) noexcept {
        for (std::size_t index = 0U; index < kWarehouseCount; ++index) {
            array_element(warehouse_payments, index) +=
                array_element(other.warehouse_payments, index);
        }
        for (std::size_t index = 0U; index < kDistrictCount; ++index) {
            array_element(district_payments, index) +=
                array_element(other.district_payments, index);
            array_element(district_new_orders, index) +=
                array_element(other.district_new_orders, index);
        }
        logical_transactions += other.logical_transactions;
        attempts += other.attempts;
        committed += other.committed;
        database_aborts += other.database_aborts;
        abandoned += other.abandoned;
        hot_routes += other.hot_routes;
    }
};

struct WorkerSlot final {
    PressureLedger ledger;
    std::optional<base::Error> error;
};

auto pressure_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)}.with_context(
        "benchmark.tpcc",
        "run hot-row pressure benchmark"
    );
}

template <typename T, std::size_t N>
void deterministic_shuffle(
    std::array<T, N>& values,
    benchmark_support::DeterministicRandom& random
) noexcept {
    for (std::size_t remaining = values.size(); remaining > 1U; --remaining) {
        const std::size_t selected = random.uniform(remaining);
        std::swap(array_element(values, remaining - 1U), array_element(values, selected));
    }
}

class ClientInput final {
public:
    ClientInput(
        const std::uint32_t phase,
        const std::size_t client,
        const std::array<std::uint32_t, kRouletteSlots>& roulette
    ) noexcept
        : client_{client},
          roulette_{&roulette},
          random_{
              benchmark_support::kWorkloadSeed ^ (static_cast<std::uint64_t>(phase) << 32U)
              ^ static_cast<std::uint64_t>(client)
          } {}

    auto next_kind() noexcept -> TransactionKind {
        if (deck_index_ == deck_.size()) {
            refill_deck();
        }
        return array_element(deck_, deck_index_++);
    }

    auto next_route(const std::uint64_t transaction_number) noexcept -> Route {
        constexpr std::uint64_t slot_group_width = 32U;
        constexpr std::uint64_t slot_groups = 5U;
        constexpr std::uint64_t slot_rotation = 13U;
        const auto group = transaction_number % slot_groups;
        const auto rotation = transaction_number / slot_groups;
        const auto slot = (static_cast<std::uint64_t>(client_) + slot_group_width * group
                           + slot_rotation * rotation)
                          % kRouletteSlots;
        const std::uint32_t warehouse = array_element(*roulette_, slot);
        const std::uint32_t* const hot = std::ranges::find(kHotWarehouses, warehouse);
        if (hot == kHotWarehouses.end()) {
            return Route{
                .warehouse = warehouse,
                .district = uniform_district(),
                .hot_warehouse = false,
            };
        }

        const auto hot_index = static_cast<std::size_t>(hot - kHotWarehouses.begin());
        const std::uint32_t hot_district = array_element(kHotDistricts, hot_index);
        if (random_.uniform(100U) < 65U) {
            return Route{
                .warehouse = warehouse,
                .district = hot_district,
                .hot_warehouse = true,
            };
        }
        const auto candidate = static_cast<std::uint32_t>(random_.uniform(9U) + 1U);
        return Route{
            .warehouse = warehouse,
            .district = candidate >= hot_district ? candidate + 1U : candidate,
            .hot_warehouse = true,
        };
    }

private:
    auto uniform_district() noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(random_.uniform(kDistrictsPerWarehouse) + 1U);
    }

    void refill_deck() noexcept {
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
        deterministic_shuffle(deck_, random_);
        deck_index_ = 0U;
    }

    std::size_t client_;
    const std::array<std::uint32_t, kRouletteSlots>* roulette_;
    benchmark_support::DeterministicRandom random_;
    std::array<TransactionKind, 100U> deck_{};
    std::size_t deck_index_{deck_.size()};
};

auto make_roulette(const std::uint32_t phase) -> std::array<std::uint32_t, kRouletteSlots> {
    std::array<std::uint32_t, kRouletteSlots> roulette{};
    std::array<std::uint32_t, kWarehouseCount - kHotWarehouses.size()> cold{};
    std::size_t cold_position = 0U;
    for (std::uint32_t warehouse = 1U; warehouse <= kWarehouseCount; ++warehouse) {
        if (std::ranges::find(kHotWarehouses, warehouse) == kHotWarehouses.end()) {
            array_element(cold, cold_position++) = warehouse;
        }
    }

    std::size_t position = 0U;
    for (const std::uint32_t warehouse : kHotWarehouses) {
        for (std::size_t slot = 0U; slot < 26U; ++slot) {
            array_element(roulette, position++) = warehouse;
        }
    }
    for (const std::uint32_t warehouse : cold) {
        array_element(roulette, position++) = warehouse;
    }

    benchmark_support::DeterministicRandom random{
        benchmark_support::kWorkloadSeed ^ static_cast<std::uint64_t>(phase)
    };
    deterministic_shuffle(cold, random);
    for (std::size_t index = 0U; position < roulette.size(); ++index) {
        array_element(roulette, position++) = array_element(cold, index);
    }
    deterministic_shuffle(roulette, random);
    return roulette;
}

auto district_index(const Route route) noexcept -> std::size_t {
    return (static_cast<std::size_t>(route.warehouse) - 1U) * kDistrictsPerWarehouse
           + static_cast<std::size_t>(route.district) - 1U;
}

auto is_retryable_conflict(const base::ErrorCode code) noexcept -> bool {
    return code == base::ErrorCode::kSerializationFailure
           || code == base::ErrorCode::kDeadlockDetected;
}

auto rollback_after_failure(api::Session& session, base::Error primary, const bool retryable)
    -> base::Result<bool> {
    auto rolled_back = session.rollback();
    if (!rolled_back) {
        return base::fail(
            std::move(primary).with_context(
                "benchmark.tpcc",
                "rollback also failed: " + rolled_back.error().to_string()
            )
        );
    }
    if (retryable) {
        return false;
    }
    return base::fail(std::move(primary));
}

auto execute_checked(
    api::Database& database,
    api::Session& session,
    const api::PreparedStatement& statement,
    const std::span<const types::Value> parameters,
    const ExpectedResult expected
) -> base::Result<bool> {
    auto executed = database.execute_prepared(session, statement, parameters);
    if (!executed) {
        const bool retryable = is_retryable_conflict(executed.error().code());
        return rollback_after_failure(session, std::move(executed).error(), retryable);
    }

    const bool valid =
        expected == ExpectedResult::kOneAffectedRow
            ? executed->affected_row_count == 1U
            : executed->result_sets.size() == 1U && executed->result_sets.front().size() == 1U;
    if (valid) {
        return true;
    }
    return rollback_after_failure(
        session,
        pressure_error("prepared pressure statement returned an unexpected result shape"),
        false
    );
}

auto run_new_order_attempt(
    api::Database& database,
    api::Session& session,
    const std::vector<api::PreparedStatement>& statements,
    const Route route
) -> base::Result<bool> {
    const std::array parameters{
        types::Value::integer(route.warehouse),
        types::Value::integer(route.district),
    };
    auto updated = execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kDistrictNewOrderUpdate)],
        parameters,
        ExpectedResult::kOneAffectedRow
    );
    if (!updated || !updated.value()) {
        return updated;
    }
    return execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kDistrictSelect)],
        parameters,
        ExpectedResult::kOneRow
    );
}

auto run_payment_attempt(
    api::Database& database,
    api::Session& session,
    const std::vector<api::PreparedStatement>& statements,
    const Route route
) -> base::Result<bool> {
    const std::array warehouse_key{types::Value::integer(route.warehouse)};
    auto warehouse_before = execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kWarehouseSelect)],
        warehouse_key,
        ExpectedResult::kOneRow
    );
    if (!warehouse_before || !warehouse_before.value()) {
        return warehouse_before;
    }
    const std::array warehouse_update{
        types::Value::floating(1.0F),
        types::Value::integer(route.warehouse),
    };
    auto warehouse_updated = execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kWarehouseUpdate)],
        warehouse_update,
        ExpectedResult::kOneAffectedRow
    );
    if (!warehouse_updated || !warehouse_updated.value()) {
        return warehouse_updated;
    }
    auto warehouse_after = execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kWarehouseSelect)],
        warehouse_key,
        ExpectedResult::kOneRow
    );
    if (!warehouse_after || !warehouse_after.value()) {
        return warehouse_after;
    }

    const std::array district_key{
        types::Value::integer(route.warehouse),
        types::Value::integer(route.district),
    };
    auto district_before = execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kDistrictSelect)],
        district_key,
        ExpectedResult::kOneRow
    );
    if (!district_before || !district_before.value()) {
        return district_before;
    }
    const std::array district_update{
        types::Value::floating(1.0F),
        types::Value::integer(route.warehouse),
        types::Value::integer(route.district),
    };
    auto district_updated = execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kDistrictPaymentUpdate)],
        district_update,
        ExpectedResult::kOneAffectedRow
    );
    if (!district_updated || !district_updated.value()) {
        return district_updated;
    }
    return execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kDistrictSelect)],
        district_key,
        ExpectedResult::kOneRow
    );
}

auto run_stock_level_attempt(
    api::Database& database,
    api::Session& session,
    const std::vector<api::PreparedStatement>& statements,
    const Route route
) -> base::Result<bool> {
    const std::array parameters{
        types::Value::integer(route.warehouse),
        types::Value::integer(route.district),
    };
    return execute_checked(
        database,
        session,
        statements[static_cast<std::size_t>(StatementIndex::kDistrictSelect)],
        parameters,
        ExpectedResult::kOneRow
    );
}

auto run_attempt(
    api::Database& database,
    api::Session& session,
    const std::vector<api::PreparedStatement>& statements,
    const TransactionKind kind,
    const Route route
) -> base::Result<bool> {
    auto begun = session.begin();
    if (!begun) {
        return base::fail(std::move(begun).error());
    }

    base::Result<bool> executed = true;
    switch (kind) {
        case TransactionKind::kNewOrder:
            executed = run_new_order_attempt(database, session, statements, route);
            break;
        case TransactionKind::kPayment:
            executed = run_payment_attempt(database, session, statements, route);
            break;
        case TransactionKind::kStockLevel:
            executed = run_stock_level_attempt(database, session, statements, route);
            break;
        case TransactionKind::kOrderStatus:
        case TransactionKind::kDelivery:
            break;
    }
    if (!executed || !executed.value()) {
        return executed;
    }
    auto committed = session.commit();
    if (!committed) {
        return base::fail(std::move(committed).error());
    }
    return true;
}

void record_commit(PressureLedger& ledger, const TransactionKind kind, const Route route) noexcept {
    ++ledger.committed;
    const auto warehouse = static_cast<std::size_t>(route.warehouse) - 1U;
    const auto district = district_index(route);
    switch (kind) {
        case TransactionKind::kNewOrder:
            ++array_element(ledger.district_new_orders, district);
            break;
        case TransactionKind::kPayment:
            ++array_element(ledger.warehouse_payments, warehouse);
            ++array_element(ledger.district_payments, district);
            break;
        case TransactionKind::kOrderStatus:
        case TransactionKind::kDelivery:
        case TransactionKind::kStockLevel:
            break;
    }
}

auto run_logical_transaction(
    api::Database& database,
    api::Session& session,
    const std::vector<api::PreparedStatement>& statements,
    PressureLedger& ledger,
    const TransactionKind kind,
    const Route route
) -> base::Status {
    ++ledger.logical_transactions;
    ledger.hot_routes += static_cast<std::uint64_t>(route.hot_warehouse);
    for (std::size_t attempt = 0U; attempt < kRetryLimit; ++attempt) {
        ++ledger.attempts;
        auto executed = run_attempt(database, session, statements, kind, route);
        if (!executed) {
            return base::fail(std::move(executed).error());
        }
        if (executed.value()) {
            record_commit(ledger, kind, route);
            return {};
        }
        ++ledger.database_aborts;
    }
    ++ledger.abandoned;
    return {};
}

auto run_worker(
    api::Database& database,
    api::Session& session,
    const std::vector<api::PreparedStatement>& statements,
    const std::array<std::uint32_t, kRouletteSlots>& roulette,
    const std::uint32_t phase,
    const std::size_t client,
    const std::size_t transaction_count
) -> base::Result<PressureLedger> {
    PressureLedger ledger;
    ClientInput input{phase, client, roulette};
    for (std::uint64_t transaction = 0U; transaction < transaction_count; ++transaction) {
        const auto kind = input.next_kind();
        const auto route = input.next_route(transaction);
        auto status = run_logical_transaction(database, session, statements, ledger, kind, route);
        if (!status) {
            return base::fail(std::move(status).error());
        }
    }
    return ledger;
}

struct PhaseWorkerContext final {
    api::Database& database;
    std::vector<api::Session>& sessions;
    const std::vector<api::PreparedStatement>& statements;
    const std::array<std::uint32_t, kRouletteSlots>& roulette;
    std::uint32_t phase;
    std::size_t transactions_per_client;
    std::latch& start_gate;
    std::vector<WorkerSlot>& slots;
};

void pressure_worker_body(PhaseWorkerContext& context, const std::size_t client) noexcept {
    context.start_gate.wait();
    WorkerSlot& slot = context.slots[client];
    try {
        auto result = run_worker(
            context.database,
            context.sessions[client],
            context.statements,
            context.roulette,
            context.phase,
            client,
            context.transactions_per_client
        );
        if (!result) {
            slot.error = std::move(result).error();
            return;
        }
        slot.ledger = std::move(result).value();
    } catch (const std::exception& exception) {
        slot.error = pressure_error(
            std::string{"pressure worker threw an exception: "}.append(exception.what())
        );
    } catch (...) {
        slot.error = pressure_error("pressure worker threw an exception");
    }
}

auto run_phase(
    api::Database& database,
    std::vector<api::Session>& sessions,
    const std::vector<api::PreparedStatement>& statements,
    const std::uint32_t phase,
    const std::size_t transactions_per_client
) -> base::Result<PressureLedger> {
    const auto roulette = make_roulette(phase);
    std::latch start_gate{1U};
    std::vector<WorkerSlot> slots(kClientCount);
    PhaseWorkerContext context{
        .database = database,
        .sessions = sessions,
        .statements = statements,
        .roulette = roulette,
        .phase = phase,
        .transactions_per_client = transactions_per_client,
        .start_gate = start_gate,
        .slots = slots,
    };
    std::vector<std::jthread> workers;
    workers.reserve(kClientCount);
    try {
        for (std::size_t client = 0U; client < kClientCount; ++client) {
            workers.emplace_back([&context, client]() noexcept {
                pressure_worker_body(context, client);
            });
        }
    } catch (const std::exception& exception) {
        start_gate.count_down();
        return base::fail(pressure_error(
            std::string{"could not start pressure workers: "}.append(exception.what())
        ));
    }
    start_gate.count_down();
    workers.clear();

    PressureLedger combined;
    for (WorkerSlot& slot : slots) {
        if (slot.error.has_value()) {
            return base::fail(std::move(slot.error).value());
        }
        combined.merge(slot.ledger);
    }
    return combined;
}

class TpccPressureFixture final {
public:
    TpccPressureFixture() = default;
    TpccPressureFixture(const TpccPressureFixture&) = delete;
    auto operator=(const TpccPressureFixture&) -> TpccPressureFixture& = delete;
    TpccPressureFixture(TpccPressureFixture&&) = delete;
    auto operator=(TpccPressureFixture&&) -> TpccPressureFixture& = delete;

    ~TpccPressureFixture() {
        close_noexcept();
    }

    auto initialize() -> base::Status {
        auto directory = benchmark_support::BenchmarkDirectory::create("tpcc-pressure");
        if (!directory) {
            return base::fail(std::move(directory).error());
        }
        directory_.emplace(std::move(directory).value());
        auto database = api::Database::create(
            api::DatabaseConfig{
                .data_directory = directory_->join("database"),
                .buffer_pool_pages = 4'096U,
                .recovery_page_cache_pages = 64U,
                .wal_ring_pages = 128U,
            }
        );
        if (!database) {
            return base::fail(std::move(database).error());
        }
        database_.emplace(std::move(database).value());

        auto setup = database_->new_session();
        if (!setup) {
            return base::fail(std::move(setup).error());
        }
        auto setup_session = std::move(setup).value();
        for (const std::string_view sql : {
                 "CREATE TABLE pressure_warehouse (w_id INT, w_ytd FLOAT)",
                 "CREATE TABLE pressure_district "
                 "(d_w_id INT, d_id INT, d_ytd FLOAT, d_next_o_id INT)",
             }) {
            auto dispatched = database_->dispatch(setup_session, sql);
            if (!dispatched) {
                return base::fail(std::move(dispatched).error());
            }
        }
        auto warehouses = load_warehouses(setup_session);
        if (!warehouses) {
            return warehouses;
        }
        auto districts = load_districts(setup_session);
        if (!districts) {
            return districts;
        }
        for (const std::string_view sql : {
                 "CREATE UNIQUE INDEX pressure_warehouse_pk ON pressure_warehouse (w_id)",
                 "CREATE UNIQUE INDEX pressure_district_pk ON pressure_district (d_w_id, d_id)",
             }) {
            auto dispatched = database_->dispatch(setup_session, sql);
            if (!dispatched) {
                return base::fail(std::move(dispatched).error());
            }
        }
        if (auto closed = setup_session.close(); !closed) {
            return closed;
        }

        auto prepared = prepare_statements();
        if (!prepared) {
            return prepared;
        }
        sessions_.reserve(kClientCount);
        for (std::size_t client = 0U; client < kClientCount; ++client) {
            auto session = database_->new_session();
            if (!session) {
                return base::fail(std::move(session).error());
            }
            auto isolation = session->set_isolation(api::SessionIsolation::kRepeatableRead);
            if (!isolation) {
                return isolation;
            }
            sessions_.push_back(std::move(session).value());
        }

        auto warmup =
            run_phase(*database_, sessions_, statements_, 1U, kWarmupTransactionsPerClient);
        if (!warmup) {
            return base::fail(std::move(warmup).error());
        }
        warmup_ = std::move(warmup).value();
        return {};
    }

    auto run_measured() -> base::Result<PressureLedger> {
        return run_phase(*database_, sessions_, statements_, 2U, kMeasuredTransactionsPerClient);
    }

    auto validate(const PressureLedger& measured) -> base::Status {
        PressureLedger expected = warmup_;
        expected.merge(measured);
        auto session = database_->new_session();
        if (!session) {
            return base::fail(std::move(session).error());
        }
        auto verifier = std::move(session).value();
        auto warehouses = database_->dispatch(
            verifier,
            "SELECT w_id, w_ytd FROM pressure_warehouse ORDER BY w_id"
        );
        if (!warehouses) {
            return base::fail(std::move(warehouses).error());
        }
        auto warehouse_validation = validate_warehouses(*warehouses, expected);
        if (!warehouse_validation) {
            return warehouse_validation;
        }
        auto districts = database_->dispatch(
            verifier,
            "SELECT d_w_id, d_id, d_ytd, d_next_o_id FROM pressure_district "
            "ORDER BY d_w_id, d_id"
        );
        if (!districts) {
            return base::fail(std::move(districts).error());
        }
        auto district_validation = validate_districts(*districts, expected);
        if (!district_validation) {
            return district_validation;
        }
        return verifier.close();
    }

    auto close() -> base::Status {
        for (api::Session& session : sessions_) {
            if (auto closed = session.close(); !closed) {
                return closed;
            }
        }
        sessions_.clear();
        statements_.clear();
        if (database_.has_value()) {
            auto closed = database_->close();
            if (!closed) {
                return closed;
            }
            database_.reset();
        }
        directory_.reset();
        return {};
    }

private:
    auto load_warehouses(api::Session& session) -> base::Status {
        std::size_t next = 1U;
        auto copied = database_->copy_from_rows(
            "COPY pressure_warehouse FROM STDIN",
            [&next]() -> base::Result<std::optional<std::vector<types::Value>>> {
                if (next > kWarehouseCount) {
                    return std::optional<std::vector<types::Value>>{};
                }
                std::vector<types::Value> row;
                row.reserve(2U);
                row.push_back(types::Value::integer(static_cast<std::int64_t>(next++)));
                row.push_back(types::Value::floating(0.0F));
                return std::optional<std::vector<types::Value>>{std::move(row)};
            },
            session
        );
        if (!copied) {
            return base::fail(std::move(copied).error());
        }
        if (copied.value() != kWarehouseCount) {
            return base::fail(pressure_error("warehouse setup row count mismatch"));
        }
        return {};
    }

    auto load_districts(api::Session& session) -> base::Status {
        std::size_t next = 0U;
        auto copied = database_->copy_from_rows(
            "COPY pressure_district FROM STDIN",
            [&next]() -> base::Result<std::optional<std::vector<types::Value>>> {
                if (next == kDistrictCount) {
                    return std::optional<std::vector<types::Value>>{};
                }
                const auto warehouse = next / kDistrictsPerWarehouse + 1U;
                const auto district = next % kDistrictsPerWarehouse + 1U;
                ++next;
                std::vector<types::Value> row;
                row.reserve(4U);
                row.push_back(types::Value::integer(static_cast<std::int64_t>(warehouse)));
                row.push_back(types::Value::integer(static_cast<std::int64_t>(district)));
                row.push_back(types::Value::floating(0.0F));
                row.push_back(types::Value::integer(kInitialNextOrderId));
                return std::optional<std::vector<types::Value>>{std::move(row)};
            },
            session
        );
        if (!copied) {
            return base::fail(std::move(copied).error());
        }
        if (copied.value() != kDistrictCount) {
            return base::fail(pressure_error("district setup row count mismatch"));
        }
        return {};
    }

    auto prepare_statements() -> base::Status {
        const std::array<types::DataType, 1U> warehouse_key{types::kInt64Type};
        const std::array<types::DataType, 2U> warehouse_update{
            types::kFloat32Type,
            types::kInt64Type,
        };
        const std::array<types::DataType, 2U> district_key{
            types::kInt64Type,
            types::kInt64Type,
        };
        const std::array<types::DataType, 3U> district_update{
            types::kFloat32Type,
            types::kInt64Type,
            types::kInt64Type,
        };
        const auto append = [this](
                                const std::string_view sql,
                                const std::span<const types::DataType> parameter_types
                            ) -> base::Status {
            auto prepared = database_->prepare(sql, parameter_types);
            if (!prepared) {
                return base::fail(std::move(prepared).error());
            }
            statements_.push_back(std::move(prepared).value());
            return {};
        };
        statements_.reserve(static_cast<std::size_t>(StatementIndex::kCount));
        if (auto status =
                append("SELECT w_ytd FROM pressure_warehouse WHERE w_id=$1", warehouse_key);
            !status) {
            return status;
        }
        if (auto status = append(
                "UPDATE pressure_warehouse SET w_ytd=w_ytd+$1 WHERE w_id=$2",
                warehouse_update
            );
            !status) {
            return status;
        }
        if (auto status = append(
                "SELECT d_ytd, d_next_o_id FROM pressure_district "
                "WHERE d_w_id=$1 AND d_id=$2",
                district_key
            );
            !status) {
            return status;
        }
        if (auto status = append(
                "UPDATE pressure_district SET d_ytd=d_ytd+$1 "
                "WHERE d_w_id=$2 AND d_id=$3",
                district_update
            );
            !status) {
            return status;
        }
        return append(
            "UPDATE pressure_district SET d_next_o_id=d_next_o_id+1 "
            "WHERE d_w_id=$1 AND d_id=$2",
            district_key
        );
    }

    static auto single_result_set(const api::DispatchResult& result) -> const api::ResultSet* {
        return result.result_sets.size() == 1U ? &result.result_sets.front() : nullptr;
    }

    static auto
    validate_warehouses(const api::DispatchResult& result, const PressureLedger& expected)
        -> base::Status {
        const api::ResultSet* const rows = single_result_set(result);
        if (rows == nullptr || rows->size() != kWarehouseCount) {
            return base::fail(pressure_error("warehouse validation shape mismatch"));
        }
        for (std::size_t index = 0U; index < rows->size(); ++index) {
            const auto values = rows->rows()[index].values();
            if (values.size() != 2U || values[0U].as_integer() == nullptr
                || values[1U].as_float() == nullptr
                || std::cmp_not_equal(*values[0U].as_integer(), index + 1U)
                || *values[1U].as_float()
                       != static_cast<float>(array_element(expected.warehouse_payments, index))) {
                return base::fail(pressure_error("warehouse committed-update ledger mismatch"));
            }
        }
        return {};
    }

    static auto
    validate_districts(const api::DispatchResult& result, const PressureLedger& expected)
        -> base::Status {
        const api::ResultSet* const rows = single_result_set(result);
        if (rows == nullptr || rows->size() != kDistrictCount) {
            return base::fail(pressure_error("district validation shape mismatch"));
        }
        for (std::size_t index = 0U; index < rows->size(); ++index) {
            const auto values = rows->rows()[index].values();
            const auto expected_warehouse =
                static_cast<std::int64_t>(index / kDistrictsPerWarehouse + 1U);
            const auto expected_district =
                static_cast<std::int64_t>(index % kDistrictsPerWarehouse + 1U);
            if (values.size() != 4U || values[0U].as_integer() == nullptr
                || values[1U].as_integer() == nullptr || values[2U].as_float() == nullptr
                || values[3U].as_integer() == nullptr
                || *values[0U].as_integer() != expected_warehouse
                || *values[1U].as_integer() != expected_district
                || *values[2U].as_float()
                       != static_cast<float>(array_element(expected.district_payments, index))
                || *values[3U].as_integer()
                       != kInitialNextOrderId
                              + static_cast<std::int64_t>(
                                  array_element(expected.district_new_orders, index)
                              )) {
                return base::fail(pressure_error("district committed-update ledger mismatch"));
            }
        }
        return {};
    }

    void close_noexcept() noexcept {
        for (api::Session& session : sessions_) {
            static_cast<void>(session.close());
        }
        sessions_.clear();
        statements_.clear();
        if (database_.has_value()) {
            static_cast<void>(database_->close());
            database_.reset();
        }
        directory_.reset();
    }

    std::optional<benchmark_support::BenchmarkDirectory> directory_;
    std::optional<api::Database> database_;
    std::vector<api::Session> sessions_;
    std::vector<api::PreparedStatement> statements_;
    PressureLedger warmup_;
};

void tpcc_hot_row_snapshot_pressure(benchmark::State& state) {
    TpccPressureFixture fixture;
    auto initialized = fixture.initialize();
    if (!initialized) {
        state.SkipWithError(initialized.error().to_string());
        return;
    }

    std::optional<PressureLedger> measured;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto phase = fixture.run_measured();
        if (!phase) {
            state.SkipWithError(phase.error().to_string());
            return;
        }
        measured = std::move(phase).value();

        state.PauseTiming();
        auto validated = fixture.validate(*measured);
        if (!validated) {
            state.SkipWithError(validated.error().to_string());
            return;
        }
        auto closed = fixture.close();
        if (!closed) {
            state.SkipWithError(closed.error().to_string());
            return;
        }
        state.ResumeTiming();
    }

    const auto logical = static_cast<double>(measured->logical_transactions);
    const auto attempts = static_cast<double>(measured->attempts);
    state.SetItemsProcessed(static_cast<std::int64_t>(measured->logical_transactions));
    state.counters["committed"] = static_cast<double>(measured->committed);
    state.counters["committed/s"] =
        benchmark::Counter(static_cast<double>(measured->committed), benchmark::Counter::kIsRate);
    state.counters["database_aborts"] = static_cast<double>(measured->database_aborts);
    state.counters["abandoned"] = static_cast<double>(measured->abandoned);
    state.counters["attempts/logical"] = logical == 0.0 ? 0.0 : attempts / logical;
    state.counters["abort_pct"] =
        attempts == 0.0 ? 0.0 : static_cast<double>(measured->database_aborts) * 100.0 / attempts;
    state.counters["hot_route_pct"] =
        logical == 0.0 ? 0.0 : static_cast<double>(measured->hot_routes) * 100.0 / logical;
    state.SetLabel(
        "50 warehouse + 500 district rows; 32 Snapshot Isolation clients; exact "
        "45/43/4/4/4 deck and hot-row routing; five same-input attempts"
    );
}

// Google Benchmark registers named benchmark state during process startup.
// NOLINTBEGIN(cert-err58-cpp,bugprone-throwing-static-initialization)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(tpcc_hot_row_snapshot_pressure)
    ->Name("tpcc/hot_row_snapshot_pressure")
    ->Iterations(1)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cert-err58-cpp,bugprone-throwing-static-initialization)

}  // namespace
}  // namespace db::benchmarks
