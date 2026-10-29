#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/access/heap_page.hpp"
#include "db/access/heap_scan.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/heap_wal.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/proc_array.hpp"

namespace db::benchmarks {
namespace {

constexpr auto kRelationId = base::RelationId{8'102U};
constexpr auto kWriterXid = base::Xid{5U};
constexpr auto kReaderXid = base::Xid{9U};
constexpr std::size_t kPoolPages = 256U;
constexpr std::size_t kPayloadSize = 16U;
constexpr storage::PageAddress kFragmentationAddress{
    .relation_id = base::RelationId{8'103U},
    .page_id = base::PageId{0U},
};

class DirectLoadHeapLog final : public access::HeapMutationLogPort {
public:
    auto log(const access::HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        static_cast<void>(mutation);
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "direct-load heap benchmark unexpectedly requested WAL",
            }
        );
    }

    auto writes_wal() const noexcept -> bool override {
        return false;
    }
};

struct HeapBundle final {
    std::unique_ptr<storage::BufferPool> pool;
    benchmark_support::CountingMemoryDisk* disk{nullptr};
    std::unique_ptr<DirectLoadHeapLog> log;
    std::unique_ptr<access::HeapSpaceRegistry> registry;
    access::HeapTable table;
    transaction::CommitLog commit_log;
    transaction::Snapshot snapshot;
};

void skip_with_error(benchmark::State& state, const base::Error& error) {
    const auto message = error.to_string();
    state.SkipWithError(message);
}

auto make_populated_heap(const std::size_t row_count) -> base::Result<HeapBundle> {
    auto disk = std::make_unique<benchmark_support::CountingMemoryDisk>();
    auto* const disk_view = disk.get();
    auto pool = storage::BufferPool::create(kPoolPages, std::move(disk));
    if (!pool) {
        return base::fail(std::move(pool).error());
    }
    auto log = std::make_unique<DirectLoadHeapLog>();
    auto registry = std::make_unique<access::HeapSpaceRegistry>();
    auto table = access::HeapTable::create(kRelationId, *pool.value(), *registry, *log);
    if (!table) {
        return base::fail(std::move(table).error());
    }

    std::array<std::byte, kPayloadSize> payload{};
    std::fill(payload.begin(), payload.end(), std::byte{0x5A});
    std::size_t inserted_count = 0U;
    while (inserted_count < row_count) {
        const auto batch_size =
            std::min(row_count - inserted_count, access::kMaximumHeapMultiInsertRows);
        const base::Bytes payload_bytes{payload.data(), payload.size()};
        std::vector<base::Bytes> rows(batch_size, payload_bytes);
        auto inserted = table->multi_insert(kWriterXid, 0U, rows);
        if (!inserted) {
            return base::fail(std::move(inserted).error());
        }
        if (inserted->size() != batch_size) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "heap benchmark multi-insert returned the wrong row count",
                }
            );
        }
        inserted_count += inserted->size();
    }

    transaction::CommitLog commit_log;
    if (auto allocated = commit_log.ensure_xid_allocated(kWriterXid); !allocated) {
        return base::fail(std::move(allocated).error());
    }
    if (auto committed =
            commit_log.set_status(kWriterXid, transaction::TransactionStatus::kCommitted);
        !committed) {
        return base::fail(std::move(committed).error());
    }
    disk_view->reset_counters();
    return HeapBundle{
        .pool = std::move(pool).value(),
        .disk = disk_view,
        .log = std::move(log),
        .registry = std::move(registry),
        .table = std::move(table).value(),
        .commit_log = std::move(commit_log),
        .snapshot = transaction::Snapshot{
            .xmin = base::Xid{6U},
            .xmax = base::Xid{20U},
            .in_progress = {},
            .command_id = 1U,
        },
    };
}

auto scan_context(const HeapBundle& heap) noexcept -> access::HeapScanContext {
    return access::HeapScanContext{
        .mvcc = access::HeapMvccContext{
            .own_xid = kReaderXid,
            .snapshot = &heap.snapshot,
            .commit_log = &heap.commit_log,
            .serializable_conflicts = nullptr,
        },
    };
}

auto consume_scan(access::HeapScan& scan) -> base::Result<std::size_t> {
    std::size_t rows = 0U;
    for (;;) {
        auto next = scan.next();
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next->has_value()) {
            return rows;
        }
        benchmark::DoNotOptimize(next->value().payload.data());
        ++rows;
    }
}

auto consume_scan_reused(access::HeapScan& scan) -> base::Result<std::size_t> {
    std::size_t rows = 0U;
    access::HeapScanTuple tuple;
    for (;;) {
        auto found = scan.next_into(tuple);
        if (!found) {
            return base::fail(std::move(found).error());
        }
        if (!found.value()) {
            return rows;
        }
        benchmark::DoNotOptimize(tuple.payload.data());
        ++rows;
    }
}

void hot_scan(benchmark::State& state) {
    const auto expected_rows = static_cast<std::size_t>(state.range(0));
    auto heap = make_populated_heap(expected_rows);
    if (!heap) {
        skip_with_error(state, heap.error());
        return;
    }

    std::uint64_t scanned_rows = 0U;
    std::uint64_t loaded_pages = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto scan = access::HeapScan::begin(heap->table, scan_context(heap.value()));
        if (!scan) {
            skip_with_error(state, scan.error());
            break;
        }
        auto rows = consume_scan(scan.value());
        if (!rows) {
            skip_with_error(state, rows.error());
            break;
        }
        if (rows.value() != expected_rows) {
            state.SkipWithError("heap scan returned the wrong row count");
            break;
        }
        scanned_rows += rows.value();
        loaded_pages += scan->pages_loaded();
    }

    const auto operations = static_cast<double>(std::max<std::int64_t>(state.iterations(), 1));
    const auto disk = heap->disk->counters();
    state.counters["pages/scan"] = static_cast<double>(loaded_pages) / operations;
    state.counters["disk_reads/scan"] = static_cast<double>(disk.reads) / operations;
    state.SetItemsProcessed(static_cast<std::int64_t>(scanned_rows));
}

void hot_scan_reused(benchmark::State& state) {
    const auto expected_rows = static_cast<std::size_t>(state.range(0));
    auto heap = make_populated_heap(expected_rows);
    if (!heap) {
        skip_with_error(state, heap.error());
        return;
    }

    std::uint64_t scanned_rows = 0U;
    std::uint64_t loaded_pages = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto scan = access::HeapScan::begin(heap->table, scan_context(heap.value()));
        if (!scan) {
            skip_with_error(state, scan.error());
            break;
        }
        auto rows = consume_scan_reused(scan.value());
        if (!rows) {
            skip_with_error(state, rows.error());
            break;
        }
        if (rows.value() != expected_rows) {
            state.SkipWithError("reused heap scan returned the wrong row count");
            break;
        }
        scanned_rows += rows.value();
        loaded_pages += scan->pages_loaded();
    }

    const auto operations = static_cast<double>(std::max<std::int64_t>(state.iterations(), 1));
    const auto disk = heap->disk->counters();
    state.counters["pages/scan"] = static_cast<double>(loaded_pages) / operations;
    state.counters["disk_reads/scan"] = static_cast<double>(disk.reads) / operations;
    state.SetItemsProcessed(static_cast<std::int64_t>(scanned_rows));
}

void insert_batch(benchmark::State& state) {
    const auto row_count = static_cast<std::size_t>(state.range(0));
    auto heap = make_populated_heap(0U);
    if (!heap) {
        skip_with_error(state, heap.error());
        return;
    }
    std::array<std::byte, kPayloadSize> payload{};
    std::fill(payload.begin(), payload.end(), std::byte{0x5A});
    const base::Bytes payload_bytes{payload.data(), payload.size()};
    std::vector<base::Bytes> rows(access::kMaximumHeapMultiInsertRows, payload_bytes);
    std::size_t inserted_count = 0U;

    for (auto iteration : state) {
        static_cast<void>(iteration);
        while (inserted_count < row_count) {
            const auto batch_size =
                std::min(row_count - inserted_count, access::kMaximumHeapMultiInsertRows);
            auto inserted = heap->table.multi_insert(
                kWriterXid,
                0U,
                std::span<const base::Bytes>{rows}.first(batch_size)
            );
            if (!inserted) {
                skip_with_error(state, inserted.error());
                return;
            }
            if (inserted->size() != batch_size) {
                state.SkipWithError("heap insert benchmark returned the wrong row count");
                return;
            }
            inserted_count += inserted->size();
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(inserted_count));
}

auto fragmented_heap_page(const std::size_t row_count) -> base::Result<storage::Page> {
    auto page = storage::Page::create(storage::PageKind::kHeap, kFragmentationAddress);
    if (!page) {
        return base::fail(std::move(page).error());
    }
    auto heap = access::HeapPage::open(page.value(), kFragmentationAddress);
    if (!heap) {
        return base::fail(std::move(heap).error());
    }
    std::array<std::byte, kPayloadSize> payload{};
    std::fill(payload.begin(), payload.end(), std::byte{0x5A});
    std::vector<base::SlotId> slots;
    slots.reserve(row_count);
    for (std::size_t row = 0U; row < row_count; ++row) {
        auto inserted = heap->insert(kWriterXid, 0U, payload);
        if (!inserted) {
            return base::fail(std::move(inserted).error());
        }
        slots.push_back(inserted->slot_id());
    }
    for (std::size_t row = 1U; row < slots.size(); row += 2U) {
        if (auto dead = heap->mark_dead(slots[row]); !dead) {
            return base::fail(std::move(dead).error());
        }
        if (auto unused = heap->mark_unused(slots[row]); !unused) {
            return base::fail(std::move(unused).error());
        }
    }
    return page.value();
}

void prune_fragmentation(benchmark::State& state) {
    const auto row_count = static_cast<std::size_t>(state.range(0));
    auto source = fragmented_heap_page(row_count);
    if (!source) {
        skip_with_error(state, source.error());
        return;
    }
    transaction::CommitLog commit_log;
    if (auto allocated = commit_log.ensure_xid_allocated(kWriterXid); !allocated) {
        skip_with_error(state, allocated.error());
        return;
    }
    if (auto aborted = commit_log.set_status(kWriterXid, transaction::TransactionStatus::kAborted);
        !aborted) {
        skip_with_error(state, aborted.error());
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        storage::Page page = source.value();
        state.ResumeTiming();
        auto heap = access::HeapPage::open(page, kFragmentationAddress);
        if (!heap) {
            skip_with_error(state, heap.error());
            return;
        }
        auto pruned = heap->prune(commit_log, base::Xid{kWriterXid.value() + 1U});
        if (!pruned || !pruned->has_storage_changes()) {
            state.SkipWithError("heap prune failed to reclaim fragmented tuples");
            return;
        }
        benchmark::DoNotOptimize(pruned->space_reclaimed);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(row_count));
}

// Google Benchmark's registration API intentionally creates process-lifetime global objects.
// NOLINTBEGIN(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(hot_scan)->Name("heap/scan_hot")->ArgName("rows")->Arg(64)->Arg(4'096);
BENCHMARK(hot_scan_reused)->Name("heap/scan_hot_reused")->ArgName("rows")->Arg(64)->Arg(4'096);
BENCHMARK(insert_batch)->Name("heap/insert_batch")->ArgName("rows")->Arg(16'384)->Iterations(1);
BENCHMARK(prune_fragmentation)->Name("heap/prune_fragmentation")->ArgName("rows")->Arg(64);
// NOLINTEND(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks
