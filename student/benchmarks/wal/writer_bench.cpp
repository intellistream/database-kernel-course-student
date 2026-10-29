#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/identifier.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/record.hpp"
#include "db/wal/ring_buffer.hpp"

namespace db::benchmarks {
namespace {

constexpr std::uint64_t kSystemIdentifier = 0x5741'4C57'5249'5445ULL;
constexpr std::uint32_t kTimeline = 1U;
constexpr std::size_t kInsertBatchSize = 4'096U;

auto transaction_record(const std::size_t payload_size) -> wal::WalRecord {
    return wal::WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{1U},
        .resource_manager = wal::WalResourceManager::kTransaction,
        .info = wal::opcode::transaction::kCommit,
        .blocks = {},
        .main_payload = std::vector<std::byte>(payload_size, std::byte{0x5A}),
    };
}

auto open_manager(const benchmark_support::BenchmarkDirectory& directory)
    -> base::Result<wal::WalManager> {
    return wal::WalManager::open(
        directory.join("wal"),
        wal::WalManagerOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = kTimeline,
            .ring_page_count = wal::kDefaultWalRingPages,
            .retention_lsn = base::kInvalidLsn,
            .redo_lsn = base::kInvalidLsn,
            .create = true,
        }
    );
}

void record_encode(benchmark::State& state) {
    const auto payload_size = static_cast<std::size_t>(state.range(0));
    const auto record = transaction_record(payload_size);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto encoded = wal::encode_record(record);
        if (!encoded) {
            state.SkipWithError("failed to encode WAL record");
            break;
        }
        benchmark::DoNotOptimize(encoded->data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(payload_size));
}

void ring_insert(benchmark::State& state) {
    auto encoded = wal::encode_record(transaction_record(64U));
    if (!encoded) {
        state.SkipWithError("failed to encode WAL ring input");
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto ring = wal::WalRingBuffer::create(wal::kDefaultWalRingPages);
        state.ResumeTiming();
        if (!ring) {
            state.SkipWithError("failed to create WAL ring");
            break;
        }
        for (std::size_t index = 0U; index < kInsertBatchSize; ++index) {
            auto outcome = ring->try_reserve(encoded->size());
            if (!outcome || !std::holds_alternative<wal::WalReservation>(outcome.value())) {
                state.SkipWithError("WAL ring insert reached unexpected backpressure");
                return;
            }
            if (auto copied = ring->copy_in(
                    std::get<wal::WalReservation>(outcome.value()),
                    base::Bytes{encoded.value()}
                );
                !copied) {
                state.SkipWithError("failed to copy WAL ring input");
                return;
            }
        }
        benchmark::DoNotOptimize(ring->inserted_lsn().value());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kInsertBatchSize));
    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(kInsertBatchSize * encoded->size())
    );
}

void manager_insert(benchmark::State& state) {
    const auto record = transaction_record(64U);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto directory = benchmark_support::BenchmarkDirectory::create("wal-manager-insert");
        auto manager =
            directory ? open_manager(directory.value())
                      : base::Result<wal::WalManager>{base::fail(std::move(directory).error())};
        state.ResumeTiming();
        if (!manager) {
            state.SkipWithError("failed to create WAL manager");
            break;
        }
        for (std::size_t index = 0U; index < kInsertBatchSize; ++index) {
            auto inserted = manager->insert(record);
            if (!inserted) {
                state.SkipWithError("failed to insert WAL record");
                return;
            }
            benchmark::DoNotOptimize(inserted->end_lsn.value());
        }
        state.PauseTiming();
        if (auto shutdown = manager->shutdown(); !shutdown) {
            state.SkipWithError("failed to shut down WAL manager");
            return;
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kInsertBatchSize));
}

void flush_batch(benchmark::State& state) {
    const auto record_count = static_cast<std::size_t>(state.range(0));
    const auto record = transaction_record(64U);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto directory = benchmark_support::BenchmarkDirectory::create("wal-flush-batch");
        auto manager =
            directory ? open_manager(directory.value())
                      : base::Result<wal::WalManager>{base::fail(std::move(directory).error())};
        base::Lsn target{base::kInvalidLsn};
        if (manager) {
            for (std::size_t index = 0U; index < record_count; ++index) {
                auto inserted = manager->insert(record);
                if (!inserted) {
                    state.ResumeTiming();
                    state.SkipWithError("failed to insert WAL flush input");
                    return;
                }
                target = inserted->end_lsn;
            }
        }
        state.ResumeTiming();
        if (!manager || target == base::kInvalidLsn) {
            state.SkipWithError("failed to initialize WAL flush batch");
            break;
        }
        if (auto flushed = manager->flush_commit(target, std::chrono::microseconds{0}); !flushed) {
            state.SkipWithError("failed to flush WAL batch");
            break;
        }
        benchmark::DoNotOptimize(manager->flushed_lsn().value());
        state.PauseTiming();
        if (auto shutdown = manager->shutdown(); !shutdown) {
            state.SkipWithError("failed to shut down flushed WAL manager");
            return;
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(record_count));
}

// Google Benchmark owns the process-lifetime registration object.
// NOLINTBEGIN(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(record_encode)
    ->Name("wal/record_encode")
    ->ArgName("payload_bytes")
    ->Arg(0)
    ->Arg(64)
    ->Arg(1'024)
    ->Arg(8'192);
BENCHMARK(ring_insert)->Name("wal/ring_insert");
BENCHMARK(manager_insert)->Name("wal/manager_insert")->Iterations(16);
BENCHMARK(flush_batch)
    ->Name("wal/flush_batch")
    ->ArgName("records")
    ->Arg(1)
    ->Arg(32)
    ->Arg(256)
    ->Iterations(16)
    ->UseRealTime();
// NOLINTEND(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks
