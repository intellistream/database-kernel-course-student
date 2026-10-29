#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/page.hpp"
#include "db/storage/replacement.hpp"

namespace db::benchmarks {
namespace {

using benchmark_support::CountingMemoryDisk;
using storage::PageAddress;

constexpr auto kRelationId = base::RelationId{8'001U};
constexpr std::size_t kMarkerOffset = 256U;
constexpr std::size_t kFastPoolPages = 64U;
constexpr std::size_t kCapacityPoolPages = 256U;
constexpr std::size_t kConcurrentPages = 64U;

struct PoolBundle final {
    std::unique_ptr<storage::BufferPool> pool;
    CountingMemoryDisk* disk{nullptr};
    std::vector<PageAddress> addresses;
};

auto make_policy(const std::size_t pool_pages, const bool lru)
    -> base::Result<std::unique_ptr<storage::ReplacementPolicy>> {
    if (!lru) {
        return std::unique_ptr<storage::ReplacementPolicy>{};
    }
    auto policy = storage::LruReplacer::create(pool_pages, 16U);
    if (!policy) {
        return base::fail(std::move(policy).error());
    }
    return std::unique_ptr<storage::ReplacementPolicy>{std::move(policy).value()};
}

auto make_memory_pool(
    const std::size_t pool_pages,
    const std::size_t relation_pages,
    const bool lru = false,
    const bool prewarm = true
) -> base::Result<PoolBundle> {
    auto disk = std::make_unique<CountingMemoryDisk>();
    auto* const disk_view = disk.get();
    std::vector<PageAddress> addresses;
    addresses.reserve(relation_pages);
    for (std::size_t index = 0U; index < relation_pages; ++index) {
        auto address = disk->allocate_page(kRelationId, storage::PageKind::kHeap);
        if (!address) {
            return base::fail(std::move(address).error());
        }
        addresses.push_back(address.value());
    }
    auto policy = make_policy(pool_pages, lru);
    if (!policy) {
        return base::fail(std::move(policy).error());
    }
    auto pool = storage::BufferPool::create(pool_pages, std::move(disk), std::move(policy).value());
    if (!pool) {
        return base::fail(std::move(pool).error());
    }
    if (prewarm) {
        for (const auto address : addresses) {
            auto page = pool.value()->read_page(address);
            if (!page) {
                return base::fail(std::move(page).error());
            }
        }
    }
    disk_view->reset_counters();
    return PoolBundle{
        .pool = std::move(pool).value(),
        .disk = disk_view,
        .addresses = std::move(addresses),
    };
}

void skip_with_error(benchmark::State& state, const base::Error& error) {
    const auto message = error.to_string();
    state.SkipWithError(message);
}

void report_disk_counters(
    benchmark::State& state,
    const CountingMemoryDisk& disk,
    const std::uint64_t operations
) {
    const auto counters = disk.counters();
    const auto denominator = static_cast<double>(std::max<std::uint64_t>(operations, 1U));
    state.counters["disk_reads/op"] = static_cast<double>(counters.reads) / denominator;
    state.counters["disk_writes/op"] = static_cast<double>(counters.writes) / denominator;
    state.counters["hit_ratio"] =
        1.0 - std::min(1.0, static_cast<double>(counters.reads) / denominator);
}

void checked_special_view(benchmark::State& state) {
    auto page = storage::Page::create(
        storage::PageKind::kBtree,
        PageAddress{kRelationId, base::PageId{1U}},
        64U
    );
    if (!page) {
        skip_with_error(state, page.error());
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto special = page->special_view();
        if (!special) {
            skip_with_error(state, special.error());
            break;
        }
        benchmark::DoNotOptimize(special->data());
        benchmark::DoNotOptimize(special->size());
    }
    state.SetItemsProcessed(state.iterations());
}

void hot_pin(benchmark::State& state) {
    auto bundle = make_memory_pool(kFastPoolPages, 1U);
    if (!bundle) {
        skip_with_error(state, bundle.error());
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto pin = bundle->pool->pin_page(bundle->addresses.front());
        if (!pin) {
            skip_with_error(state, pin.error());
            break;
        }
        benchmark::DoNotOptimize(pin->frame_id().value());
    }
    state.SetItemsProcessed(state.iterations());
    report_disk_counters(state, *bundle->disk, static_cast<std::uint64_t>(state.iterations()));
}

void hot_read(benchmark::State& state) {
    auto bundle = make_memory_pool(kFastPoolPages, 1U);
    if (!bundle) {
        skip_with_error(state, bundle.error());
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto page = bundle->pool->read_page(bundle->addresses.front());
        if (!page) {
            skip_with_error(state, page.error());
            break;
        }
        benchmark::DoNotOptimize(page->page()->bytes().data());
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(storage::kPageSize));
    report_disk_counters(state, *bundle->disk, static_cast<std::uint64_t>(state.iterations()));
}

void hot_write(benchmark::State& state) {
    auto bundle = make_memory_pool(kFastPoolPages, 1U);
    if (!bundle) {
        skip_with_error(state, bundle.error());
        return;
    }
    std::byte marker{0};
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto page = bundle->pool->write_page(bundle->addresses.front());
        if (!page) {
            skip_with_error(state, page.error());
            break;
        }
        auto bytes = page->page()->mutable_view(kMarkerOffset, 1U);
        if (!bytes) {
            skip_with_error(state, bytes.error());
            break;
        }
        marker ^= std::byte{1U};
        bytes->front() = marker;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    report_disk_counters(state, *bundle->disk, static_cast<std::uint64_t>(state.iterations()));
}

void random_capacity_read(benchmark::State& state) {
    const auto working_set_pages = static_cast<std::size_t>(state.range(0));
    const auto use_lru = state.range(1) != 0;
    auto bundle = make_memory_pool(kCapacityPoolPages, working_set_pages, use_lru);
    if (!bundle) {
        skip_with_error(state, bundle.error());
        return;
    }
    benchmark_support::DeterministicRandom random;
    std::vector<std::size_t> accesses(16'384U);
    std::ranges::generate(accesses, [&] {
        return random.uniform(working_set_pages);
    });
    std::size_t cursor = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto address = bundle->addresses[accesses[cursor++ % accesses.size()]];
        auto page = bundle->pool->read_page(address);
        if (!page) {
            skip_with_error(state, page.error());
            break;
        }
        benchmark::DoNotOptimize(page->page()->bytes().data());
    }
    const auto operations = static_cast<std::uint64_t>(state.iterations());
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(storage::kPageSize));
    report_disk_counters(state, *bundle->disk, operations);
}

void eviction(benchmark::State& state) {
    constexpr std::size_t pool_pages = 128U;
    constexpr std::size_t working_set_pages = pool_pages * 8U;
    const auto dirty = state.range(0) != 0;
    auto bundle = make_memory_pool(pool_pages, working_set_pages);
    if (!bundle) {
        skip_with_error(state, bundle.error());
        return;
    }
    std::size_t cursor = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto address = bundle->addresses[cursor++ % bundle->addresses.size()];
        if (dirty) {
            auto page = bundle->pool->write_page(address);
            if (!page) {
                skip_with_error(state, page.error());
                break;
            }
            auto bytes = page->page()->mutable_view(kMarkerOffset, 1U);
            if (!bytes) {
                skip_with_error(state, bytes.error());
                break;
            }
            bytes->front() ^= std::byte{1U};
        } else {
            auto page = bundle->pool->read_page(address);
            if (!page) {
                skip_with_error(state, page.error());
                break;
            }
            benchmark::DoNotOptimize(page->page()->bytes().data());
        }
    }
    state.SetItemsProcessed(state.iterations());
    report_disk_counters(state, *bundle->disk, static_cast<std::uint64_t>(state.iterations()));
}

auto concurrent_pool() -> PoolBundle* {
    static auto bundle = make_memory_pool(256U, kConcurrentPages);
    return bundle ? &bundle.value() : nullptr;
}

void concurrent_read(benchmark::State& state) {
    auto* const bundle = concurrent_pool();
    if (bundle == nullptr) {
        state.SkipWithError("cannot construct concurrent buffer-pool fixture");
        return;
    }
    if (state.thread_index() == 0) {
        bundle->disk->reset_counters();
    }
    const auto partitioned = state.range(0) != 0;
    const auto address =
        partitioned
            ? bundle->addresses
                  [static_cast<std::size_t>(state.thread_index()) % bundle->addresses.size()]
            : bundle->addresses.front();
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto page = bundle->pool->read_page(address);
        if (!page) {
            skip_with_error(state, page.error());
            break;
        }
        benchmark::DoNotOptimize(page->page()->bytes().data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(state.threads()));
    if (state.thread_index() == 0) {
        const auto operations = static_cast<std::uint64_t>(state.iterations())
                                * static_cast<std::uint64_t>(state.threads());
        report_disk_counters(state, *bundle->disk, operations);
    }
}

void checkpoint_batch(benchmark::State& state) {
    constexpr std::size_t pool_pages = 256U;
    const auto batch_pages = static_cast<std::size_t>(state.range(0));
    auto bundle = make_memory_pool(pool_pages, pool_pages);
    if (!bundle) {
        skip_with_error(state, bundle.error());
        return;
    }
    std::uint64_t flushed_total = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        for (const auto address : bundle->addresses) {
            auto page = bundle->pool->write_page(address);
            if (!page) {
                skip_with_error(state, page.error());
                return;
            }
            auto bytes = page->page()->mutable_view(kMarkerOffset, 1U);
            if (!bytes) {
                skip_with_error(state, bytes.error());
                return;
            }
            bytes->front() ^= std::byte{1U};
        }
        bundle->disk->reset_counters();
        state.ResumeTiming();

        auto snapshot = bundle->pool->mark_checkpoint_dirty_pages();
        if (!snapshot) {
            skip_with_error(state, snapshot.error());
            break;
        }
        std::size_t cursor = 0U;
        std::size_t flushed = 0U;
        while (flushed < snapshot->marked) {
            auto batch =
                bundle->pool->flush_checkpoint_dirty_batch(cursor, batch_pages, snapshot->max_lsn);
            if (!batch) {
                skip_with_error(state, batch.error());
                return;
            }
            if (batch.value() == 0U) {
                break;
            }
            flushed += batch.value();
        }
        flushed_total += flushed;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(flushed_total));
    state.SetBytesProcessed(static_cast<std::int64_t>(flushed_total * storage::kPageSize));
}

struct FileRelation final {
    benchmark_support::BenchmarkDirectory directory;
    std::vector<PageAddress> addresses;
};

auto make_file_relation(const std::size_t pages) -> base::Result<FileRelation> {
    auto directory = benchmark_support::BenchmarkDirectory::create("buffer-pool-file");
    if (!directory) {
        return base::fail(std::move(directory).error());
    }
    auto disk = storage::FileDiskManager::open(directory->join("relations"));
    if (!disk) {
        return base::fail(std::move(disk).error());
    }
    std::vector<PageAddress> addresses;
    addresses.reserve(pages);
    for (std::size_t index = 0U; index < pages; ++index) {
        auto address = disk.value()->allocate_page(kRelationId, storage::PageKind::kHeap);
        if (!address) {
            return base::fail(std::move(address).error());
        }
        addresses.push_back(address.value());
    }
    if (auto synced = disk.value()->sync_all(); !synced) {
        return base::fail(std::move(synced).error());
    }
    return FileRelation{
        .directory = std::move(directory).value(),
        .addresses = std::move(addresses),
    };
}

void file_read(benchmark::State& state) {
    constexpr std::size_t pages_per_iteration = 1'024U;
    const auto random_order = state.range(0) != 0;
    auto relation = make_file_relation(pages_per_iteration);
    if (!relation) {
        skip_with_error(state, relation.error());
        return;
    }
    std::vector<std::size_t> order(pages_per_iteration);
    for (std::size_t index = 0U; index < order.size(); ++index) {
        order[index] = index;
    }
    if (random_order) {
        benchmark_support::DeterministicRandom random;
        for (std::size_t index = order.size(); index > 1U; --index) {
            std::swap(order[index - 1U], order[random.uniform(index)]);
        }
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto disk = storage::FileDiskManager::open(relation->directory.join("relations"));
        if (!disk) {
            skip_with_error(state, disk.error());
            return;
        }
        auto pool = storage::BufferPool::create(128U, std::move(disk).value());
        if (!pool) {
            skip_with_error(state, pool.error());
            return;
        }
        state.ResumeTiming();
        for (const auto index : order) {
            auto page = pool.value()->read_page(relation->addresses[index]);
            if (!page) {
                skip_with_error(state, page.error());
                return;
            }
            benchmark::DoNotOptimize(page->page()->bytes().data());
        }
        state.PauseTiming();
        if (auto shutdown = pool.value()->shutdown(); !shutdown) {
            skip_with_error(state, shutdown.error());
            return;
        }
        state.ResumeTiming();
    }
    const auto pages = state.iterations() * static_cast<std::int64_t>(pages_per_iteration);
    state.SetItemsProcessed(pages);
    state.SetBytesProcessed(pages * static_cast<std::int64_t>(storage::kPageSize));
}

void file_flush(benchmark::State& state) {
    constexpr std::size_t pages_per_iteration = 256U;
    auto relation = make_file_relation(pages_per_iteration);
    if (!relation) {
        skip_with_error(state, relation.error());
        return;
    }
    auto disk = storage::FileDiskManager::open(relation->directory.join("relations"));
    if (!disk) {
        skip_with_error(state, disk.error());
        return;
    }
    auto pool = storage::BufferPool::create(pages_per_iteration, std::move(disk).value());
    if (!pool) {
        skip_with_error(state, pool.error());
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        for (const auto address : relation->addresses) {
            auto page = pool.value()->write_page(address);
            if (!page) {
                skip_with_error(state, page.error());
                return;
            }
            auto bytes = page->page()->mutable_view(kMarkerOffset, 1U);
            if (!bytes) {
                skip_with_error(state, bytes.error());
                return;
            }
            bytes->front() ^= std::byte{1U};
        }
        state.ResumeTiming();
        if (auto flushed = pool.value()->flush_all(); !flushed) {
            skip_with_error(state, flushed.error());
            break;
        }
        if (auto synced = pool.value()->sync_all(); !synced) {
            skip_with_error(state, synced.error());
            break;
        }
    }
    const auto pages = state.iterations() * static_cast<std::int64_t>(pages_per_iteration);
    state.SetItemsProcessed(pages);
    state.SetBytesProcessed(pages * static_cast<std::int64_t>(storage::kPageSize));
}

// Google Benchmark's registration API intentionally creates process-lifetime global objects.
// NOLINTBEGIN(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(hot_pin)->Name("buffer_pool/hot_pin");
BENCHMARK(hot_read)->Name("buffer_pool/hot_read");
BENCHMARK(hot_write)->Name("buffer_pool/hot_write");
BENCHMARK(checked_special_view)->Name("storage/page_special_view");

BENCHMARK(random_capacity_read)
    ->Name("buffer_pool/random_capacity")
    ->ArgsProduct({{128, 256, 1'024, 4'096}, {0}})
    ->ArgNames({"working_set_pages", "clock"});
BENCHMARK(random_capacity_read)
    ->Name("buffer_pool/policy_audit")
    ->ArgsProduct({{1'024, 4'096}, {0, 1}})
    ->ArgNames({"working_set_pages", "lru"});

BENCHMARK(eviction)->Name("buffer_pool/clean_eviction")->Arg(0);
BENCHMARK(eviction)->Name("buffer_pool/dirty_eviction")->Arg(1);

BENCHMARK(concurrent_read)
    ->Name("buffer_pool/concurrent_shared")
    ->Arg(0)
    ->ThreadRange(1, 16)
    ->UseRealTime();
BENCHMARK(concurrent_read)
    ->Name("buffer_pool/concurrent_partitioned")
    ->Arg(1)
    ->ThreadRange(1, 16)
    ->UseRealTime();

BENCHMARK(checkpoint_batch)
    ->Name("buffer_pool/checkpoint")
    ->ArgName("batch_pages")
    ->Args({16})
    ->Args({64})
    ->Args({256});

BENCHMARK(file_read)->Name("buffer_pool/file_sequential_read")->Arg(0)->UseRealTime();
BENCHMARK(file_read)->Name("buffer_pool/file_random_read")->Arg(1)->UseRealTime();
BENCHMARK(file_flush)->Name("buffer_pool/file_flush_sync")->UseRealTime();
// NOLINTEND(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks
