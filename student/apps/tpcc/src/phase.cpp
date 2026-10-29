#include "phase.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/protocol/rmdb_client.hpp"

#include "array_access.hpp"
#include "prepared.hpp"
#include "transaction.hpp"

namespace db::tpcc {
namespace {

using Clock = std::chrono::steady_clock;
inline constexpr std::size_t kLatencySampleLimitPerClient = 8'192U;

struct PhaseWindow final {
    Clock::time_point started;
    Clock::time_point deadline;
    Clock::time_point cleanup_deadline;
};

class StartGate final {
public:
    auto wait() -> std::optional<PhaseWindow> {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this]() {
            return released_;
        });
        return window_;
    }

    void release(const std::optional<PhaseWindow> window) {
        {
            std::scoped_lock lock{mutex_};
            window_ = window;
            released_ = true;
        }
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool released_{false};
    std::optional<PhaseWindow> window_;
};

class SetupCoordinator final {
public:
    void report_success() {
        {
            std::scoped_lock lock{mutex_};
            ++reported_;
        }
        changed_.notify_one();
    }

    void report_error(const base::Error& error) {
        {
            std::scoped_lock lock{mutex_};
            if (!first_error_.has_value()) {
                first_error_ = error;
            }
            ++reported_;
        }
        changed_.notify_one();
    }

    auto wait_until(const std::size_t expected, const Clock::time_point deadline)
        -> std::optional<base::Error> {
        std::unique_lock lock{mutex_};
        if (!changed_.wait_until(lock, deadline, [this, expected]() {
                return reported_ == expected;
            })) {
            return base::Error{
                base::ErrorCode::kUnavailable,
                "TPC-C ranked client setup timed out",
            };
        }
        return first_error_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t reported_{0U};
    std::optional<base::Error> first_error_;
};

struct PreparedClient final {
    protocol::RmdbClient client;
    protocol::RmdbPreparedCatalog catalog;
};

struct ClientMetrics final {
    TransactionCounters window_counters;
    TransactionCounters lifetime_counters;
    std::array<std::uint64_t, 50> window_warehouse_completions{};
    std::array<std::uint64_t, 50> lifetime_warehouse_completions{};
    std::array<std::vector<std::uint64_t>, 5> latency_us;
    SqlTelemetry sql;
};

struct WorkerSlot final {
    std::optional<ClientMetrics> metrics;
    std::optional<base::Error> error;
};

struct ClientRunContext final {
    const BenchmarkConfig& config;
    std::uint32_t workload_seed{0U};
    std::uint32_t phase_id{0U};
    std::uint32_t client_id{0U};
    const WorkloadRouting& routing;
    const PhaseRouting& phase_routing;
    PhaseWindow window{};
    bool sql_stats{false};
    const std::atomic_bool& cancelled;
};

struct WorkerContext final {
    const BenchmarkOptions& options;
    const BenchmarkConfig& config;
    std::uint32_t phase_id{0U};
    const WorkloadRouting& routing;
    const PhaseRouting& phase_routing;
    const std::vector<protocol::RmdbPreparedStatement>& definitions;
    bool sql_stats{false};
    Clock::time_point setup_deadline;
    StartGate& gate;
    SetupCoordinator& setup;
    std::atomic_bool& cancelled;
    std::vector<WorkerSlot>& slots;
};

auto benchmark_error(std::string message) -> base::detail::Failure {
    return base::fail(base::Error{base::ErrorCode::kExecution, std::move(message)});
}

auto worker_exception(const std::string_view operation) -> base::Error {
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return base::Error{
            base::ErrorCode::kResourceExhausted,
            std::string{operation}.append(": allocation failed"),
        };
    } catch (const std::length_error& error) {
        return base::Error{
            base::ErrorCode::kResourceExhausted,
            std::string{operation}.append(": ").append(error.what()),
        };
    } catch (const std::exception& error) {
        return base::Error{
            base::ErrorCode::kInternal,
            std::string{operation}.append(": ").append(error.what()),
        };
    } catch (...) {
        return base::Error{
            base::ErrorCode::kInternal,
            std::string{operation}.append(": non-standard exception"),
        };
    }
}

auto connect_and_prepare(
    const io::TcpEndpoint& endpoint,
    const BenchmarkConfig& config,
    const std::vector<protocol::RmdbPreparedStatement>& definitions
) -> base::Result<PreparedClient> {
    auto client = protocol::RmdbClient::connect(
        endpoint,
        protocol::RmdbClientOptions{
            .io_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(config.io_timeout),
            .maximum_response_bytes = protocol::kMaximumRmdbFramePayloadSize,
        }
    );
    if (!client) {
        return base::fail(std::move(client).error());
    }
    auto isolation = client->execute("SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;");
    if (!isolation) {
        return base::fail(std::move(isolation).error());
    }
    if (isolation->terminal.kind != protocol::RmdbTerminalKind::kCommandOk) {
        return benchmark_error(
            "RMDB rejected SNAPSHOT ISOLATION: " + isolation->terminal.diagnostic
        );
    }
    auto catalog = client->prepare_set(definitions);
    if (!catalog) {
        return base::fail(std::move(catalog).error());
    }
    if (catalog->entries().size() != statement::kRankedStatementCount) {
        return benchmark_error("PREPARE_SET did not acknowledge all 57 TPC-C statements");
    }
    return PreparedClient{
        .client = std::move(client).value(),
        .catalog = std::move(catalog).value(),
    };
}

auto is_transport_error(const base::Error& error) noexcept -> bool {
    if (error.code() != base::ErrorCode::kIo && error.code() != base::ErrorCode::kUnavailable) {
        return false;
    }
    if (!error.has_system_error()) {
        return true;
    }
    return error.system_error() != std::errc::timed_out
           && error.system_error() != std::errc::operation_would_block;
}

auto establish_session(
    const io::TcpEndpoint& endpoint,
    const BenchmarkConfig& config,
    const Clock::time_point setup_deadline,
    const std::vector<protocol::RmdbPreparedStatement>& definitions
) -> base::Result<PreparedClient> {
    static constexpr std::array<std::chrono::milliseconds, 3> kBackoff{
        std::chrono::milliseconds{100},
        std::chrono::milliseconds{400},
        std::chrono::milliseconds{1'000},
    };
    std::uint32_t transport_failures = 0U;
    std::uint32_t prepare_failures = 0U;
    std::size_t backoff_index = 0U;
    while (true) {
        if (Clock::now() >= setup_deadline) {
            return benchmark_error("TPC-C ranked client setup budget expired");
        }
        auto prepared = connect_and_prepare(endpoint, config, definitions);
        if (prepared) {
            return prepared;
        }
        const base::Error& error = prepared.error();
        bool retry = false;
        if (is_transport_error(error)) {
            ++transport_failures;
            retry = transport_failures < 4U;
        } else if (
            error.code() == base::ErrorCode::kExecution
            && error.message().starts_with("RMDB server rejected PREPARE_SET:")
        ) {
            ++prepare_failures;
            retry = prepare_failures < 2U;
        }
        if (!retry) {
            return base::fail(std::move(prepared).error());
        }
        const auto backoff = array_element(kBackoff, std::min(backoff_index, kBackoff.size() - 1U));
        ++backoff_index;
        if (Clock::now() + backoff >= setup_deadline) {
            return base::fail(std::move(prepared).error());
        }
        std::this_thread::sleep_for(backoff);
    }
}

void merge_coverage(
    std::array<std::uint64_t, 50>& target,
    const std::array<std::uint64_t, 50>& source
) noexcept {
    for (std::size_t index = 0U; index < target.size(); ++index) {
        array_element(target, index) += array_element(source, index);
    }
}

auto run_active_client(PreparedClient prepared, const ClientRunContext& context)
    -> base::Result<ClientMetrics> {
    ClientMetrics metrics;
    TransactionGenerator generator{context.workload_seed, context.phase_id, context.client_id};
    ClientRouter router = context.phase_routing.client(context.client_id);
    std::uint64_t transaction_number = 0U;
    while (Clock::now() < context.window.deadline
           && !context.cancelled.load(std::memory_order_acquire)) {
        const TransactionKind kind = generator.next_kind();
        const RouteSelection route = router.next(transaction_number++);
        const auto transaction_started = Clock::now();
        SqlSession session{
            prepared.client,
            prepared.catalog,
            context.sql_stats ? &metrics.sql : nullptr,
            context.window.deadline,
            context.window.cleanup_deadline,
        };
        const TransactionExecution execution{
            .session = session,
            .generator = generator,
            .route = route,
            .scale = context.config.scale,
            .routing = context.config.routing,
            .hot_items = context.routing.hot_items(),
            .retry_limit = context.config.retry_limit,
        };
        auto record = execute_transaction(kind, execution);
        if (!record) {
            return base::fail(std::move(record).error());
        }
        if (record->completion == TransactionCompletion::kDeadlineStopped) {
            break;
        }
        const auto completed_at = Clock::now();
        metrics.lifetime_counters.merge(record->counters);
        const bool inside_window = completed_at <= context.window.deadline;
        if (inside_window) {
            metrics.window_counters.merge(record->counters);
        }
        if (record->completed()) {
            ++array_element(metrics.lifetime_warehouse_completions, route.home_warehouse - 1U);
            if (inside_window) {
                ++array_element(metrics.window_warehouse_completions, route.home_warehouse - 1U);
            }
        }
        std::vector<std::uint64_t>& samples =
            array_element(metrics.latency_us, transaction_index(kind));
        if (inside_window && record->completion == TransactionCompletion::kCommitted
            && samples.size() < kLatencySampleLimitPerClient) {
            samples.push_back(
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               completed_at - transaction_started
                )
                                               .count())
            );
        }
    }
    return metrics;
}

void worker_body(const std::size_t index, WorkerContext& context) noexcept {
    bool setup_reported = false;
    try {
        auto prepared = establish_session(
            context.options.endpoint,
            context.config,
            context.setup_deadline,
            context.definitions
        );
        if (!prepared) {
            context.cancelled.store(true, std::memory_order_release);
            context.slots[index].error = prepared.error();
            context.setup.report_error(prepared.error());
            setup_reported = true;
            static_cast<void>(context.gate.wait());
            return;
        }
        context.setup.report_success();
        setup_reported = true;
        const std::optional<PhaseWindow> window = context.gate.wait();
        if (!window.has_value()) {
            context.slots[index].metrics = ClientMetrics{};
            return;
        }
        const ClientRunContext client_context{
            .config = context.config,
            .workload_seed = context.options.seed,
            .phase_id = context.phase_id,
            .client_id = static_cast<std::uint32_t>(index),
            .routing = context.routing,
            .phase_routing = context.phase_routing,
            .window = window.value(),
            .sql_stats = context.sql_stats,
            .cancelled = context.cancelled,
        };
        auto metrics = run_active_client(std::move(prepared).value(), client_context);
        if (!metrics) {
            context.cancelled.store(true, std::memory_order_release);
            context.slots[index].error = std::move(metrics).error();
            return;
        }
        context.slots[index].metrics = std::move(metrics).value();
    } catch (...) {
        context.cancelled.store(true, std::memory_order_release);
        context.slots[index].error = worker_exception("TPC-C client worker");
        if (!setup_reported) {
            context.setup.report_error(*context.slots[index].error);
            static_cast<void>(context.gate.wait());
        }
    }
}

void join_workers(std::vector<std::jthread>& workers) noexcept {
    for (std::jthread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

auto percentile(std::vector<std::uint64_t>& samples, const std::size_t percent) -> std::uint64_t {
    if (samples.empty()) {
        return 0U;
    }
    std::sort(samples.begin(), samples.end());
    return samples[(samples.size() - 1U) * percent / 100U];
}

auto aggregate_slots(
    std::vector<WorkerSlot>& slots,
    const WorkloadRouting& routing,
    const std::uint32_t phase_id,
    const std::chrono::seconds duration
) -> base::Result<PhaseResult> {
    PhaseResult phase;
    phase.metrics.phase_id = phase_id;
    phase.metrics.duration = duration;
    std::array<std::vector<std::uint64_t>, 5> latency;
    for (WorkerSlot& slot : slots) {
        if (slot.error.has_value()) {
            return base::fail(std::move(slot.error).value());
        }
        ClientMetrics& metrics = *slot.metrics;
        phase.metrics.counters.merge(metrics.window_counters);
        phase.lifetime_counters.merge(metrics.lifetime_counters);
        merge_coverage(phase.metrics.completed_by_warehouse, metrics.window_warehouse_completions);
        merge_coverage(
            phase.lifetime_warehouse_completions,
            metrics.lifetime_warehouse_completions
        );
        for (std::size_t index = 0U; index < latency.size(); ++index) {
            auto& combined = array_element(latency, index);
            auto& samples = array_element(metrics.latency_us, index);
            combined.insert(
                combined.end(),
                std::make_move_iterator(samples.begin()),
                std::make_move_iterator(samples.end())
            );
        }
        phase.sql.merge(std::move(metrics.sql));
    }
    phase.metrics.covered_warehouses = static_cast<std::uint32_t>(
        std::ranges::count_if(phase.metrics.completed_by_warehouse, [](const std::uint64_t count) {
            return count > 0U;
        })
    );
    for (const std::uint32_t warehouse : routing.hot_warehouses()) {
        phase.metrics.hot_warehouses_covered += static_cast<std::uint32_t>(
            array_element(phase.metrics.completed_by_warehouse, warehouse - 1U) > 0U
        );
    }
    phase.metrics.new_order_per_minute =
        static_cast<double>(phase.metrics.counters.committed.front()) * 60.0
        / static_cast<double>(duration.count());
    for (std::size_t index = 0U; index < latency.size(); ++index) {
        array_element(phase.metrics.latency_p50_us, index) =
            percentile(array_element(latency, index), 50U);
        array_element(phase.metrics.latency_p95_us, index) =
            percentile(array_element(latency, index), 95U);
    }
    return phase;
}

}  // namespace

auto run_phase(const PhaseRequest& request) -> base::Result<PhaseResult> {
    const Clock::time_point setup_deadline =
        std::min(request.task_deadline, Clock::now() + request.config.phase_grace);
    const PhaseRouting phase_routing = request.routing.phase(request.phase_id);
    StartGate gate;
    SetupCoordinator setup;
    std::atomic_bool cancelled{false};
    std::vector<WorkerSlot> slots(request.config.clients);
    WorkerContext context{
        .options = request.options,
        .config = request.config,
        .phase_id = request.phase_id,
        .routing = request.routing,
        .phase_routing = phase_routing,
        .definitions = request.definitions,
        .sql_stats = request.sql_stats,
        .setup_deadline = setup_deadline,
        .gate = gate,
        .setup = setup,
        .cancelled = cancelled,
        .slots = slots,
    };
    std::vector<std::jthread> workers;
    workers.reserve(request.config.clients);
    try {
        for (std::size_t index = 0U; index < request.config.clients; ++index) {
            workers.emplace_back([index, &context]() {
                worker_body(index, context);
            });
        }
    } catch (...) {
        cancelled.store(true, std::memory_order_release);
        gate.release(std::nullopt);
        join_workers(workers);
        return base::fail(worker_exception("starting TPC-C client workers"));
    }

    std::optional<base::Error> setup_error =
        setup.wait_until(request.config.clients, setup_deadline);
    const Clock::time_point phase_started = Clock::now();
    if (!setup_error.has_value()
        && phase_started + request.duration + request.config.phase_grace > request.task_deadline) {
        setup_error = base::Error{
            base::ErrorCode::kUnavailable,
            "TPC-C performance budget cannot contain the complete ranked phase",
        };
    }
    std::optional<Clock::time_point> cleanup_deadline;
    if (setup_error.has_value()) {
        cancelled.store(true, std::memory_order_release);
        gate.release(std::nullopt);
    } else {
        const Clock::time_point deadline = phase_started + request.duration;
        cleanup_deadline = deadline + request.config.phase_grace;
        gate.release(
            PhaseWindow{
                .started = phase_started,
                .deadline = deadline,
                .cleanup_deadline = cleanup_deadline.value(),
            }
        );
    }
    join_workers(workers);
    if (setup_error.has_value()) {
        return base::fail(std::move(setup_error).value());
    }
    if (Clock::now() > cleanup_deadline.value()) {
        return benchmark_error("TPC-C ranked phase exceeded its 120-second cleanup grace");
    }
    return aggregate_slots(slots, request.routing, request.phase_id, request.duration);
}

auto health_check(
    const BenchmarkOptions& options,
    const BenchmarkConfig& config,
    const Clock::time_point task_deadline
) -> base::Status {
    if (Clock::now() >= task_deadline) {
        return benchmark_error("TPC-C performance deadline expired before health check");
    }
    auto client = protocol::RmdbClient::connect(
        options.endpoint,
        protocol::RmdbClientOptions{
            .io_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(config.io_timeout),
            .maximum_response_bytes = protocol::kMaximumRmdbFramePayloadSize,
        }
    );
    if (!client) {
        return base::fail(std::move(client).error());
    }
    auto response = client->execute("select COUNT(*) from warehouse;");
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (response->terminal.kind != protocol::RmdbTerminalKind::kResultEnd
        || response->rows.size() != 1U || response->rows.front().size() != 1U) {
        return benchmark_error("TPC-C health check did not return one warehouse count");
    }
    const std::int64_t* const count = response->rows.front().front().as_integer();
    if (count == nullptr || std::cmp_not_equal(*count, config.scale.warehouses)) {
        return benchmark_error("TPC-C health check requires exactly 50 canonical warehouse rows");
    }
    return client->close();
}

}  // namespace db::tpcc
