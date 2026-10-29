#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/identifier.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/recovery/wal_stream.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"

namespace db::benchmarks {
namespace {

constexpr std::uint64_t kSystemIdentifier = 0x5741'4C42'454E'4348ULL;
constexpr std::uint32_t kTimeline = 1U;

struct WalFixture final {
    benchmark_support::BenchmarkDirectory directory;
    wal::WalReader reader;
    std::size_t record_count{0U};
    base::Lsn redo_lsn{base::kInvalidLsn};
};

auto make_fixture(const std::size_t record_count) -> base::Result<WalFixture> {
    auto directory = benchmark_support::BenchmarkDirectory::create("wal-reader");
    if (!directory) {
        return base::fail(std::move(directory).error());
    }
    const auto wal_directory = directory->join("wal");
    auto manager = wal::WalManager::open(
        wal_directory,
        wal::WalManagerOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = kTimeline,
            .ring_page_count = 16U,
            .retention_lsn = base::kInvalidLsn,
            .redo_lsn = base::kInvalidLsn,
            .create = true,
        }
    );
    if (!manager) {
        return base::fail(std::move(manager).error());
    }
    base::Lsn redo_lsn{base::kInvalidLsn};
    for (std::size_t index = 0U; index < record_count; ++index) {
        auto inserted = manager->insert(
            wal::WalRecord{
                .previous_lsn = base::kInvalidLsn,
                .xid = base::Xid{static_cast<std::uint32_t>(index + 1U)},
                .resource_manager = wal::WalResourceManager::kTransaction,
                .info = wal::opcode::transaction::kCommit,
                .blocks = {},
                .main_payload = std::vector<std::byte>(64U, static_cast<std::byte>(index)),
            }
        );
        if (!inserted) {
            return base::fail(std::move(inserted).error());
        }
        if (index == record_count * 3U / 4U) {
            redo_lsn = inserted->start_lsn;
        }
    }
    if (auto shutdown = manager->shutdown(); !shutdown) {
        return base::fail(std::move(shutdown).error());
    }
    auto reader = wal::WalReader::open(
        wal_directory,
        wal::WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = kTimeline,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    if (!reader) {
        return base::fail(std::move(reader).error());
    }
    return WalFixture{
        .directory = std::move(directory).value(),
        .reader = std::move(reader).value(),
        .record_count = record_count,
        .redo_lsn = redo_lsn,
    };
}

void startup_open(benchmark::State& state) {
    const auto record_count = static_cast<std::size_t>(state.range(0));
    auto fixture = make_fixture(record_count);
    if (!fixture) {
        state.SkipWithError("failed to initialize WAL startup benchmark");
        return;
    }
    const auto wal_directory = fixture->directory.join("wal");
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto stream = recovery::RecoveryWalStream::open(
            recovery::RecoveryWalStreamOptions{
                .directory = wal_directory,
                .reader_options =
                    wal::WalReaderOptions{
                        .system_identifier = kSystemIdentifier,
                        .timeline = kTimeline,
                        .retention_lsn = base::kInvalidLsn,
                    },
                .redo_lsn = fixture->redo_lsn,
                .trusted_startup_validation = std::nullopt,
            }
        );
        if (!stream) {
            state.SkipWithError("failed to open recovery WAL stream");
            break;
        }
        benchmark::DoNotOptimize(stream->replay_start_lsn().value());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(record_count));
}

void sequential_decode(benchmark::State& state) {
    const auto record_count = static_cast<std::size_t>(state.range(0));
    auto fixture = make_fixture(record_count);
    if (!fixture) {
        state.SkipWithError("failed to initialize WAL reader benchmark");
        return;
    }
    const auto start = fixture->reader.stream_start();
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto seek = fixture->reader.seek(start);
        state.ResumeTiming();
        if (!seek) {
            state.SkipWithError("failed to rewind WAL reader");
            break;
        }
        auto scanned = fixture->reader.scan_valid_end(base::kInvalidLsn);
        if (!scanned || scanned->records_scanned != record_count || scanned->incomplete_tail) {
            state.SkipWithError("WAL reader benchmark scan failed");
            break;
        }
        benchmark::DoNotOptimize(scanned->valid_end_lsn.value());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(record_count));
}

// Google Benchmark owns the process-lifetime registration object.
// NOLINTBEGIN(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(sequential_decode)->Name("wal/sequential_decode")->ArgName("records")->Arg(4'096);
BENCHMARK(startup_open)->Name("wal/startup_open")->ArgName("records")->Arg(4'096);
// NOLINTEND(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks
