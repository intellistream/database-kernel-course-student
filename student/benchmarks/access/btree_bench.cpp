#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/access/btree_wal.hpp"
#include "db/access/dead_tid_store.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/recovery/wal_durability.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/durability.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"
#include "db/wal/manager.hpp"

namespace db::benchmarks {
namespace {

constexpr auto kRelationId = base::RelationId{8'101U};
constexpr std::size_t kHotPoolPages = 4'096U;
constexpr std::size_t kLargeHotPoolPages = 8'192U;
constexpr std::size_t kConstrainedPoolPages = 64U;
constexpr std::size_t kLookupWorkloadSize = 4'096U;

class ImmediateDurability final : public storage::PageDurabilityPort {
public:
    auto flush_through(base::Lsn /*target_lsn*/) -> base::Status override {
        return {};
    }
};

class CountingBtreeLog final : public access::BtreeMutationLogPort {
public:
    explicit CountingBtreeLog(const bool enabled = true) noexcept
        : enabled_{enabled} {}

    auto log(const access::BtreeWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.pages.empty()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "benchmark B-tree logger received an empty mutation",
                }
            );
        }
        records_.fetch_add(1U, std::memory_order_relaxed);
        pages_.fetch_add(mutation.pages.size(), std::memory_order_relaxed);
        return base::Lsn{next_lsn_.fetch_add(1U, std::memory_order_relaxed)};
    }

    auto writes_wal() const noexcept -> bool override {
        return enabled_;
    }

    void reset() noexcept {
        records_.store(0U, std::memory_order_relaxed);
        pages_.store(0U, std::memory_order_relaxed);
    }

    auto records() const noexcept -> std::uint64_t {
        return records_.load(std::memory_order_relaxed);
    }

    auto pages() const noexcept -> std::uint64_t {
        return pages_.load(std::memory_order_relaxed);
    }

private:
    bool enabled_;
    std::atomic<std::uint64_t> next_lsn_{1U};
    std::atomic<std::uint64_t> records_{0U};
    std::atomic<std::uint64_t> pages_{0U};
};

struct TreeBundle final {
    std::unique_ptr<storage::BufferPool> pool;
    benchmark_support::CountingMemoryDisk* disk{nullptr};
    std::unique_ptr<CountingBtreeLog> log;
    access::BtreeRelation relation;
};

enum class KeyShape : std::uint8_t {
    kInteger,
    kWideInteger,
    kCharacter,
    kComposite,
};

auto key_types(const KeyShape shape) -> std::vector<types::DataType> {
    switch (shape) {
        case KeyShape::kInteger:
            return {types::kInt64Type};
        case KeyShape::kWideInteger:
            return {
                types::kInt64Type,
                types::kInt64Type,
                types::kInt64Type,
                types::kInt64Type,
                types::kInt64Type,
                types::kInt64Type,
                types::kInt64Type,
                types::kInt64Type,
            };
        case KeyShape::kCharacter:
            return {types::DataType{types::kCharTypeId, types::TypeModifier{32}}};
        case KeyShape::kComposite:
            return {
                types::kInt64Type,
                types::DataType{types::kCharTypeId, types::TypeModifier{8}},
            };
    }
    return {};
}

auto padded_key(const std::int64_t key, const std::size_t width) -> std::string {
    auto text = std::to_string(key);
    if (text.size() < width) {
        text.insert(text.begin(), width - text.size(), '0');
    }
    return text;
}

auto values_for(const KeyShape shape, const std::int64_t key) -> std::vector<types::Value> {
    switch (shape) {
        case KeyShape::kInteger:
            return {types::Value::integer(key)};
        case KeyShape::kWideInteger:
            return {
                types::Value::integer(key),
                types::Value::integer(key + 1),
                types::Value::integer(key + 2),
                types::Value::integer(key + 3),
                types::Value::integer(key + 4),
                types::Value::integer(key + 5),
                types::Value::integer(key + 6),
                types::Value::integer(key + 7),
            };
        case KeyShape::kCharacter:
            return {types::Value::character(padded_key(key, 20U), types::TypeModifier{32})};
        case KeyShape::kComposite:
            return {
                types::Value::integer(key / 100),
                types::Value::character(padded_key(key % 100, 4U), types::TypeModifier{8}),
            };
    }
    return {};
}

constexpr auto rid_for(const std::uint64_t ordinal) noexcept -> base::Rid {
    return base::Rid{
        base::PageId{static_cast<std::uint32_t>(ordinal + 1U)},
        base::SlotId{static_cast<std::uint16_t>(ordinal % 65'534U)},
    };
}

auto make_tree(
    const std::size_t pool_pages,
    const KeyShape shape = KeyShape::kInteger,
    const bool logged = true,
    const bool unique = false
) -> base::Result<TreeBundle> {
    auto disk = std::make_unique<benchmark_support::CountingMemoryDisk>();
    auto* const disk_view = disk.get();
    auto log = std::make_unique<CountingBtreeLog>(logged);
    auto* const log_view = log.get();
    auto layout = access::BtreeKeyLayout::create(key_types(shape));
    if (!layout) {
        return base::fail(std::move(layout).error());
    }
    auto pool = storage::BufferPool::create(
        pool_pages,
        std::move(disk),
        nullptr,
        std::make_unique<ImmediateDurability>()
    );
    if (!pool) {
        return base::fail(std::move(pool).error());
    }
    auto relation = access::BtreeRelation::create(
        kRelationId,
        *pool.value(),
        std::move(layout).value(),
        unique,
        *log_view
    );
    if (!relation) {
        return base::fail(std::move(relation).error());
    }
    disk_view->reset_counters();
    log_view->reset();
    return TreeBundle{
        .pool = std::move(pool).value(),
        .disk = disk_view,
        .log = std::move(log),
        .relation = std::move(relation).value(),
    };
}

void skip_with_error(benchmark::State& state, const base::Error& error) {
    const auto message = error.to_string();
    state.SkipWithError(message);
}

auto insert_keys(TreeBundle& tree, const KeyShape shape, const std::vector<std::int64_t>& keys)
    -> base::Status {
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const auto values = values_for(shape, keys[index]);
        auto inserted = tree.relation.insert(*tree.pool, values, rid_for(index));
        if (!inserted) {
            return inserted;
        }
    }
    return {};
}

auto ordered_keys(const std::size_t count) -> std::vector<std::int64_t> {
    std::vector<std::int64_t> keys(count);
    std::iota(keys.begin(), keys.end(), 0);
    return keys;
}

auto shuffled_keys(const std::size_t count) -> std::vector<std::int64_t> {
    auto keys = ordered_keys(count);
    benchmark_support::DeterministicRandom random;
    for (std::size_t index = keys.size(); index > 1U; --index) {
        std::swap(keys[index - 1U], keys[random.uniform(index)]);
    }
    return keys;
}

auto make_populated_tree(
    const std::size_t count,
    const std::size_t pool_pages,
    const KeyShape shape = KeyShape::kInteger
) -> base::Result<TreeBundle> {
    auto tree = make_tree(pool_pages, shape);
    if (!tree) {
        return base::fail(std::move(tree).error());
    }
    std::vector<access::BtreeBulkBuildEntry> entries;
    entries.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto values = values_for(shape, static_cast<std::int64_t>(index));
        auto entry = tree->relation.make_bulk_build_entry(values, rid_for(index));
        if (!entry) {
            return base::fail(std::move(entry).error());
        }
        entries.push_back(std::move(entry).value());
    }
    auto built = tree->relation.bulk_build(*tree->pool, entries);
    if (!built) {
        return base::fail(std::move(built).error());
    }
    if (auto flushed = tree->pool->flush_all(); !flushed) {
        return base::fail(std::move(flushed).error());
    }
    tree->disk->reset_counters();
    (*tree->log).reset();
    return tree;
}

auto consume_scan(access::BtreeScan& scan, storage::BufferPool& pool) -> base::Result<std::size_t> {
    std::size_t count = 0U;
    for (;;) {
        auto next = scan.next(pool);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next->has_value()) {
            return count;
        }
        benchmark::DoNotOptimize(next->value().page_id().value());
        ++count;
    }
}

struct ScanCounters {
    std::uint64_t operations;
    std::uint64_t pages;
    std::uint64_t items;
    std::uint64_t root_hints;
};

void report_scan_counters(benchmark::State& state, const ScanCounters counters) {
    const auto denominator = static_cast<double>(std::max<std::uint64_t>(counters.operations, 1U));
    state.counters["leaf_pages/op"] = static_cast<double>(counters.pages) / denominator;
    state.counters["leaf_items/op"] = static_cast<double>(counters.items) / denominator;
    state.counters["root_hints/op"] = static_cast<double>(counters.root_hints) / denominator;
}

void incremental_insert_case(benchmark::State& state, const KeyShape shape) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto random_order = state.range(1) != 0;
    const auto keys = random_order ? shuffled_keys(count) : ordered_keys(count);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto tree = make_tree(kHotPoolPages, shape);
        if (!tree) {
            skip_with_error(state, tree.error());
            return;
        }
        state.ResumeTiming();
        if (auto inserted = insert_keys(tree.value(), shape, keys); !inserted) {
            skip_with_error(state, inserted.error());
            return;
        }
        state.PauseTiming();
        auto root = tree->relation.root(*tree->pool);
        if (!root || !root->has_value()) {
            state.SkipWithError("incremental insert did not publish a root");
            return;
        }
        state.counters["wal_records/item"] =
            static_cast<double>(tree->log->records()) / static_cast<double>(count);
        state.counters["wal_pages/item"] =
            static_cast<double>(tree->log->pages()) / static_cast<double>(count);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(count) * state.iterations());
}

void incremental_insert(benchmark::State& state) {
    incremental_insert_case(state, KeyShape::kInteger);
}

void wide_incremental_insert(benchmark::State& state) {
    incremental_insert_case(state, KeyShape::kWideInteger);
}

void unique_insert(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto random_order = state.range(1) != 0;
    const auto keys = random_order ? shuffled_keys(count) : ordered_keys(count);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto tree = make_tree(kHotPoolPages, KeyShape::kInteger, true, true);
        if (!tree) {
            skip_with_error(state, tree.error());
            return;
        }
        state.ResumeTiming();
        if (auto inserted = insert_keys(tree.value(), KeyShape::kInteger, keys); !inserted) {
            skip_with_error(state, inserted.error());
            return;
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(count) * state.iterations());
}

void bulk_build(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto random_order = state.range(1) != 0;
    const auto keys = random_order ? shuffled_keys(count) : ordered_keys(count);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto tree = make_tree(kHotPoolPages);
        if (!tree) {
            skip_with_error(state, tree.error());
            return;
        }
        std::vector<access::BtreeBulkBuildEntry> entries;
        entries.reserve(count);
        for (std::size_t index = 0U; index < keys.size(); ++index) {
            const auto values = values_for(KeyShape::kInteger, keys[index]);
            auto entry = tree->relation.make_bulk_build_entry(values, rid_for(index));
            if (!entry) {
                skip_with_error(state, entry.error());
                return;
            }
            entries.push_back(std::move(entry).value());
        }
        state.ResumeTiming();
        auto built = tree->relation.bulk_build(*tree->pool, entries);
        if (!built) {
            skip_with_error(state, built.error());
            return;
        }
        benchmark::DoNotOptimize(built->root);
        state.counters["leaf_pages"] = static_cast<double>(built->leaf_pages);
        state.counters["internal_pages"] = static_cast<double>(built->internal_pages);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(count) * state.iterations());
}

void point_lookup(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto constrained = state.range(1) != 0;
    const auto hit = state.range(2) != 0;
    const auto hot_pool_pages = count >= 500'000U ? kLargeHotPoolPages : kHotPoolPages;
    auto tree = make_populated_tree(count, constrained ? kConstrainedPoolPages : hot_pool_pages);
    if (!tree) {
        skip_with_error(state, tree.error());
        return;
    }
    auto root = tree->relation.root(*tree->pool);
    if (!root) {
        skip_with_error(state, root.error());
        return;
    }
    const auto tree_levels = root->has_value() ? root->value().level + 1U : 0U;
    if ((count == 100'000U || count == 500'000U) && tree_levels != 3U) {
        state.SkipWithError("same-height point lookup cases must retain three tree levels");
        return;
    }
    benchmark_support::DeterministicRandom random;
    std::vector<std::int64_t> keys(kLookupWorkloadSize);
    std::ranges::generate(keys, [&] {
        const auto key = static_cast<std::int64_t>(random.uniform(count));
        return hit ? key : key + static_cast<std::int64_t>(count);
    });
    std::uint64_t pages = 0U;
    std::uint64_t items = 0U;
    std::uint64_t root_hints = 0U;
    std::size_t cursor = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto values = std::array{types::Value::integer(keys[cursor++ % keys.size()])};
        auto scan = access::BtreeScan::for_eq(tree->relation, values, {});
        if (!scan) {
            skip_with_error(state, scan.error());
            break;
        }
        auto found = consume_scan(scan.value(), *tree->pool);
        if (!found) {
            skip_with_error(state, found.error());
            break;
        }
        if (found.value() != (hit ? 1U : 0U)) {
            state.SkipWithError("point lookup returned the wrong row count");
            break;
        }
        pages += scan->leaf_pages_visited();
        items += scan->leaf_items_examined();
        root_hints += scan->root_hint_uses();
    }
    const auto operations = static_cast<std::uint64_t>(state.iterations());
    state.SetItemsProcessed(state.iterations());
    report_scan_counters(
        state,
        ScanCounters{
            .operations = operations,
            .pages = pages,
            .items = items,
            .root_hints = root_hints,
        }
    );
    const auto disk = tree->disk->counters();
    state.counters["disk_reads/op"] =
        static_cast<double>(disk.reads)
        / static_cast<double>(std::max<std::uint64_t>(operations, 1U));
    state.counters["tree_levels"] = static_cast<double>(tree_levels);
}

void range_scan(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto match_count = static_cast<std::size_t>(state.range(1));
    const auto constrained = state.range(2) != 0;
    auto tree = make_populated_tree(count, constrained ? kConstrainedPoolPages : kHotPoolPages);
    if (!tree) {
        skip_with_error(state, tree.error());
        return;
    }
    const auto low_key = static_cast<std::int64_t>((count - match_count) / 2U);
    const auto high_key = low_key + static_cast<std::int64_t>(match_count) - 1;
    const auto low = std::array{types::Value::integer(low_key)};
    const auto high = std::array{types::Value::integer(high_key)};
    std::uint64_t pages = 0U;
    std::uint64_t items = 0U;
    std::uint64_t root_hints = 0U;
    std::uint64_t rows = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto scan = match_count == count ? access::BtreeScan::for_full(tree->relation, {})
                                         : access::BtreeScan::for_range(
                                               tree->relation,
                                               access::BtreeScanRange{.low = low, .high = high},
                                               {}
                                           );
        if (!scan) {
            skip_with_error(state, scan.error());
            break;
        }
        auto found = consume_scan(scan.value(), *tree->pool);
        if (!found) {
            skip_with_error(state, found.error());
            break;
        }
        if (found.value() != match_count) {
            state.SkipWithError("range scan returned the wrong row count");
            break;
        }
        rows += found.value();
        pages += scan->leaf_pages_visited();
        items += scan->leaf_items_examined();
        root_hints += scan->root_hint_uses();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(rows));
    report_scan_counters(
        state,
        ScanCounters{
            .operations = static_cast<std::uint64_t>(state.iterations()),
            .pages = pages,
            .items = items,
            .root_hints = root_hints,
        }
    );
}

void key_shape_lookup(benchmark::State& state) {
    constexpr std::size_t count = 10'000U;
    const auto shape = static_cast<KeyShape>(state.range(0));
    auto tree = make_populated_tree(count, kHotPoolPages, shape);
    if (!tree) {
        skip_with_error(state, tree.error());
        return;
    }
    std::size_t cursor = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto values = values_for(shape, static_cast<std::int64_t>(cursor++ % count));
        auto scan = access::BtreeScan::for_eq(tree->relation, values, {});
        if (!scan) {
            skip_with_error(state, scan.error());
            break;
        }
        auto found = consume_scan(scan.value(), *tree->pool);
        if (!found || found.value() != 1U) {
            state.SkipWithError("key-shape lookup failed");
            break;
        }
    }
    state.SetItemsProcessed(state.iterations());
}

void parameter_lookup(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto rebind = state.range(1) != 0;
    auto tree = make_populated_tree(count, kHotPoolPages);
    if (!tree) {
        skip_with_error(state, tree.error());
        return;
    }
    std::vector<std::array<types::Value, 1U>> queries;
    queries.reserve(kLookupWorkloadSize);
    benchmark_support::DeterministicRandom random;
    for (std::size_t index = 0U; index < kLookupWorkloadSize; ++index) {
        queries.push_back(
            {types::Value::integer(static_cast<std::int64_t>(random.uniform(count)))}
        );
    }
    auto reusable = access::BtreeScan::for_eq(tree->relation, queries.front(), {});
    if (!reusable) {
        skip_with_error(state, reusable.error());
        return;
    }
    std::size_t cursor = 0U;
    std::uint64_t root_hints = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto& values = queries[cursor++ % queries.size()];
        if (rebind) {
            if (auto rebound = reusable->rebind_eq(values); !rebound) {
                skip_with_error(state, rebound.error());
                break;
            }
            auto found = consume_scan(reusable.value(), *tree->pool);
            if (!found || found.value() != 1U) {
                state.SkipWithError("rebound lookup failed");
                break;
            }
            root_hints = reusable->root_hint_uses();
        } else {
            auto scan = access::BtreeScan::for_eq(tree->relation, values, {});
            if (!scan) {
                skip_with_error(state, scan.error());
                break;
            }
            auto found = consume_scan(scan.value(), *tree->pool);
            if (!found || found.value() != 1U) {
                state.SkipWithError("recreated lookup failed");
                break;
            }
            root_hints += scan->root_hint_uses();
        }
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["root_hints/op"] =
        static_cast<double>(root_hints)
        / static_cast<double>(std::max<std::int64_t>(state.iterations(), 1));
}

class ConcurrentTreeSlot final {
public:
    auto acquire(benchmark::State& state, const bool populated) -> std::shared_ptr<TreeBundle> {
        if (state.thread_index() == 0) {
            auto built =
                populated ? make_populated_tree(50'000U, kHotPoolPages) : make_tree(kHotPoolPages);
            const std::lock_guard lock{mutex_};
            error_.reset();
            if (built) {
                tree_ = std::make_shared<TreeBundle>(std::move(built).value());
            } else {
                error_ = built.error().to_string();
            }
            ready_ = true;
            changed_.notify_all();
        } else {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, [this] {
                return ready_;
            });
        }
        const std::lock_guard lock{mutex_};
        if (error_.has_value()) {
            state.SkipWithError(*error_);
        }
        return tree_;
    }

    void release(const int thread_index) {
        if (thread_index != 0) {
            return;
        }
        const std::lock_guard lock{mutex_};
        tree_.reset();
        error_.reset();
        ready_ = false;
    }

    auto next_rid() noexcept -> std::uint64_t {
        return next_rid_.fetch_add(1U, std::memory_order_relaxed);
    }

    void reset_rids() noexcept {
        next_rid_.store(0U, std::memory_order_relaxed);
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::shared_ptr<TreeBundle> tree_;
    std::optional<std::string> error_;
    bool ready_{false};
    std::atomic<std::uint64_t> next_rid_{0U};
};

auto concurrent_read_slot() -> ConcurrentTreeSlot& {
    static ConcurrentTreeSlot slot;
    return slot;
}

auto concurrent_insert_slot() -> ConcurrentTreeSlot& {
    static ConcurrentTreeSlot slot;
    return slot;
}

void concurrent_lookup(benchmark::State& state) {
    const auto shared_key = state.range(0) == 0;
    auto tree = concurrent_read_slot().acquire(state, true);
    if (!tree) {
        return;
    }
    std::uint64_t pages = 0U;
    std::size_t cursor = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto key =
            shared_key
                ? 25'000
                : static_cast<std::int64_t>(
                      (static_cast<std::size_t>(state.thread_index()) * 7'919U + cursor++) % 50'000U
                  );
        const auto values = std::array{types::Value::integer(key)};
        auto scan = access::BtreeScan::for_eq(tree->relation, values, {});
        if (!scan) {
            skip_with_error(state, scan.error());
            break;
        }
        auto found = consume_scan(scan.value(), *tree->pool);
        if (!found || found.value() != 1U) {
            state.SkipWithError("concurrent lookup failed");
            break;
        }
        pages += scan->leaf_pages_visited();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(state.threads()));
    if (state.thread_index() == 0) {
        state.counters["leaf_pages/op"] =
            static_cast<double>(pages)
            / static_cast<double>(std::max<std::int64_t>(state.iterations(), 1));
    }
    concurrent_read_slot().release(state.thread_index());
}

void concurrent_insert(benchmark::State& state) {
    const auto overlapping = state.range(0) != 0;
    auto tree = concurrent_insert_slot().acquire(state, false);
    if (!tree) {
        return;
    }
    if (state.thread_index() == 0) {
        concurrent_insert_slot().reset_rids();
    }
    std::uint64_t local = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto ordinal = concurrent_insert_slot().next_rid();
        const auto key =
            overlapping
                ? static_cast<std::int64_t>(local++ % 1'024U)
                : static_cast<std::int64_t>(
                      static_cast<std::uint64_t>(state.thread_index()) * 10'000'000U + local++
                  );
        const auto values = std::array{types::Value::integer(key)};
        auto inserted = tree->relation.insert(*tree->pool, values, rid_for(ordinal));
        if (!inserted) {
            skip_with_error(state, inserted.error());
            break;
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(state.threads()));
    concurrent_insert_slot().release(state.thread_index());
}

void vacuum_dead_entries(benchmark::State& state) {
    constexpr std::size_t count = 20'000U;
    const auto dead_percent = static_cast<std::size_t>(state.range(0));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto tree = make_populated_tree(count, kHotPoolPages);
        if (!tree) {
            skip_with_error(state, tree.error());
            return;
        }
        access::DeadTidStore dead;
        const auto dead_count = count * dead_percent / 100U;
        for (std::size_t index = 0U; index < dead_count; ++index) {
            static_cast<void>(dead.insert(rid_for(index)));
        }
        state.ResumeTiming();
        auto removed = tree->relation.bulk_delete(*tree->pool, dead);
        if (!removed) {
            skip_with_error(state, removed.error());
            return;
        }
        if (removed->rids_removed != dead_count) {
            state.SkipWithError("B-tree vacuum removed the wrong RID count");
            return;
        }
        state.counters["pages_scanned"] = static_cast<double>(removed->pages_scanned);
        state.counters["pages_compacted"] = static_cast<double>(removed->pages_compacted);
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(count * dead_percent / 100U)
    );
}

void real_wal_insert(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto keys = ordered_keys(count);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto directory = benchmark_support::BenchmarkDirectory::create("btree-real-wal");
        if (!directory) {
            skip_with_error(state, directory.error());
            return;
        }
        auto manager = wal::WalManager::open(
            directory->join("wal"),
            wal::WalManagerOptions{
                .system_identifier = 0x4254'5245'4542'454EULL,
                .timeline = 1U,
                .ring_page_count = 128U,
                .retention_lsn = base::kInvalidLsn,
                .redo_lsn = base::kInvalidLsn,
                .create = true,
                .storage_fault_port = nullptr,
            }
        );
        if (!manager) {
            skip_with_error(state, manager.error());
            return;
        }
        recovery::BtreeMutationWalAdapter log{manager.value()};
        auto disk = std::make_unique<benchmark_support::CountingMemoryDisk>();
        auto layout = access::BtreeKeyLayout::create({types::kInt64Type});
        if (!layout) {
            skip_with_error(state, layout.error());
            return;
        }
        auto pool = storage::BufferPool::create(
            kHotPoolPages,
            std::move(disk),
            nullptr,
            std::make_unique<recovery::WalPageDurabilityAdapter>(manager.value())
        );
        if (!pool) {
            skip_with_error(state, pool.error());
            return;
        }
        auto relation = access::BtreeRelation::create(
            base::RelationId{8'102U},
            *pool.value(),
            std::move(layout).value(),
            false,
            log
        );
        if (!relation) {
            skip_with_error(state, relation.error());
            return;
        }
        const auto start_lsn = manager->end_lsn();
        state.ResumeTiming();
        for (std::size_t index = 0U; index < keys.size(); ++index) {
            const auto values = std::array{types::Value::integer(keys[index])};
            auto inserted = relation->insert(*pool.value(), values, rid_for(index));
            if (!inserted) {
                skip_with_error(state, inserted.error());
                return;
            }
        }
        const auto wal_bytes = manager->end_lsn().value() - start_lsn.value();
        state.PauseTiming();
        state.counters["wal_bytes/item"] =
            static_cast<double>(wal_bytes) / static_cast<double>(count);
        if (auto shutdown = pool.value()->shutdown(); !shutdown) {
            skip_with_error(state, shutdown.error());
            return;
        }
        if (auto shutdown = manager->shutdown(); !shutdown) {
            skip_with_error(state, shutdown.error());
            return;
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(count) * state.iterations());
}

struct FileTreeBundle final {
    benchmark_support::BenchmarkDirectory directory;
    std::unique_ptr<storage::BufferPool> pool;
    std::unique_ptr<CountingBtreeLog> log;
    access::BtreeRelation relation;
};

auto make_file_tree(const std::size_t count) -> base::Result<FileTreeBundle> {
    auto directory = benchmark_support::BenchmarkDirectory::create("btree-file");
    if (!directory) {
        return base::fail(std::move(directory).error());
    }
    auto disk = storage::FileDiskManager::open(directory->join("relations"));
    if (!disk) {
        return base::fail(std::move(disk).error());
    }
    auto log = std::make_unique<CountingBtreeLog>();
    auto layout = access::BtreeKeyLayout::create({types::kInt64Type});
    if (!layout) {
        return base::fail(std::move(layout).error());
    }
    auto pool = storage::BufferPool::create(
        kConstrainedPoolPages,
        std::move(disk).value(),
        nullptr,
        std::make_unique<ImmediateDurability>()
    );
    if (!pool) {
        return base::fail(std::move(pool).error());
    }
    auto relation = access::BtreeRelation::create(
        base::RelationId{8'103U},
        *pool.value(),
        std::move(layout).value(),
        false,
        *log
    );
    if (!relation) {
        return base::fail(std::move(relation).error());
    }
    std::vector<access::BtreeBulkBuildEntry> entries;
    entries.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto values = std::array{types::Value::integer(static_cast<std::int64_t>(index))};
        auto entry = relation->make_bulk_build_entry(values, rid_for(index));
        if (!entry) {
            return base::fail(std::move(entry).error());
        }
        entries.push_back(std::move(entry).value());
    }
    auto built = relation->bulk_build(*pool.value(), entries);
    if (!built) {
        return base::fail(std::move(built).error());
    }
    if (auto flushed = pool.value()->flush_all(); !flushed) {
        return base::fail(std::move(flushed).error());
    }
    if (auto synced = pool.value()->sync_all(); !synced) {
        return base::fail(std::move(synced).error());
    }
    return FileTreeBundle{
        .directory = std::move(directory).value(),
        .pool = std::move(pool).value(),
        .log = std::move(log),
        .relation = std::move(relation).value(),
    };
}

void file_scan(benchmark::State& state) {
    constexpr std::size_t count = 50'000U;
    constexpr std::size_t range_matches = 500U;
    const auto full_range = state.range(0) != 0;
    auto tree = make_file_tree(count);
    if (!tree) {
        skip_with_error(state, tree.error());
        return;
    }
    benchmark_support::DeterministicRandom random;
    std::uint64_t rows = 0U;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        if (!full_range) {
            const auto values =
                std::array{types::Value::integer(static_cast<std::int64_t>(random.uniform(count)))};
            auto scan = access::BtreeScan::for_eq(tree->relation, values, {});
            if (!scan) {
                skip_with_error(state, scan.error());
                break;
            }
            auto found = consume_scan(scan.value(), *tree->pool);
            if (!found || found.value() != 1U) {
                state.SkipWithError("file-backed point lookup failed");
                break;
            }
            rows += found.value();
            continue;
        }
        const auto start = random.uniform(count - range_matches);
        const auto low = std::array{types::Value::integer(static_cast<std::int64_t>(start))};
        const auto high = std::array{
            types::Value::integer(static_cast<std::int64_t>(start + range_matches - 1U))
        };
        auto scan = access::BtreeScan::for_range(
            tree->relation,
            access::BtreeScanRange{.low = low, .high = high},
            {}
        );
        if (!scan) {
            skip_with_error(state, scan.error());
            break;
        }
        auto found = consume_scan(scan.value(), *tree->pool);
        if (!found || found.value() != range_matches) {
            state.SkipWithError("file-backed range scan failed");
            break;
        }
        rows += found.value();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(rows));
}

// Google Benchmark's registration API intentionally creates process-lifetime global objects.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(incremental_insert)
    ->Name("btree/build_incremental")
    ->ArgsProduct({{10'000, 100'000}, {0, 1}})
    ->ArgNames({"entries", "random"})
    ->Iterations(1);
BENCHMARK(wide_incremental_insert)
    ->Name("btree/build_incremental_wide")
    ->ArgsProduct({{10'000, 100'000}, {0, 1}})
    ->ArgNames({"entries", "random"})
    ->Iterations(1);
BENCHMARK(unique_insert)
    ->Name("btree/unique_insert")
    ->ArgsProduct({{10'000, 100'000}, {0, 1}})
    ->ArgNames({"entries", "random"})
    ->Iterations(1);
BENCHMARK(bulk_build)
    ->Name("btree/build_bulk")
    ->ArgsProduct({{10'000, 100'000}, {0, 1}})
    ->ArgNames({"entries", "random"})
    ->Iterations(1);

BENCHMARK(point_lookup)
    ->Name("btree/point_lookup")
    ->ArgsProduct({{10'000, 100'000, 500'000, 1'000'000}, {0, 1}, {0, 1}})
    ->ArgNames({"entries", "constrained", "hit"});

BENCHMARK(range_scan)
    ->Name("btree/range_scan")
    ->ArgsProduct({{50'000}, {1, 500, 50'000}, {0, 1}})
    ->ArgNames({"entries", "matches", "constrained"});

BENCHMARK(key_shape_lookup)
    ->Name("btree/key_shape")
    ->DenseRange(
        static_cast<std::int64_t>(KeyShape::kInteger),
        static_cast<std::int64_t>(KeyShape::kComposite)
    )
    ->ArgName("shape");

BENCHMARK(parameter_lookup)
    ->Name("btree/parameter_lookup")
    ->ArgsProduct({{10'000, 50'000}, {0, 1}})
    ->ArgNames({"entries", "rebind"});

BENCHMARK(concurrent_lookup)
    ->Name("btree/concurrent_lookup_shared")
    ->Arg(0)
    ->ThreadRange(1, 16)
    ->UseRealTime();
BENCHMARK(concurrent_lookup)
    ->Name("btree/concurrent_lookup_partitioned")
    ->Arg(1)
    ->ThreadRange(1, 16)
    ->UseRealTime();

BENCHMARK(concurrent_insert)
    ->Name("btree/concurrent_insert_disjoint")
    ->Arg(0)
    ->ThreadRange(1, 8)
    ->UseRealTime();
BENCHMARK(concurrent_insert)
    ->Name("btree/concurrent_insert_overlap")
    ->Arg(1)
    ->ThreadRange(1, 8)
    ->UseRealTime();

BENCHMARK(vacuum_dead_entries)
    ->Name("btree/vacuum")
    ->ArgName("dead_percent")
    ->Arg(10)
    ->Arg(50)
    ->Iterations(1);

BENCHMARK(real_wal_insert)
    ->Name("btree/real_wal_insert")
    ->ArgName("entries")
    ->Arg(10'000)
    ->Iterations(1)
    ->UseRealTime();
BENCHMARK(incremental_insert)
    ->Name("btree/counting_wal_insert")
    ->Args({10'000, 0})
    ->ArgNames({"entries", "random"})
    ->Iterations(1);

BENCHMARK(file_scan)->Name("btree/file_point_lookup")->Arg(0)->UseRealTime();
BENCHMARK(file_scan)->Name("btree/file_range_scan")->Arg(1)->UseRealTime();
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks
