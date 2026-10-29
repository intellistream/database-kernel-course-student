#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/catalog/format.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/snapshot.hpp"
#include "db/catalog/statistics.hpp"
#include "db/catalog/store.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::catalog {
namespace {

constexpr RowSizeLimit kRowLimit{.maximum_encoded_size = 4'036U};

auto options() -> CatalogStoreOptions {
    return CatalogStoreOptions{
        .decode = CatalogDecodeOptions{.row_size_limit = kRowLimit},
    };
}

auto stress_schema() -> Schema {
    auto schema = Schema::create(
        std::vector<Column>{Column{.name = "id", .data_type = types::kInt64Type}},
        kRowLimit
    );
    EXPECT_TRUE(schema) << schema.error().to_string();
    return std::move(schema).value();
}

auto stress_statistics(const base::Oid table_oid) -> TableStatistics {
    return TableStatistics{
        .table_oid = table_oid,
        .relation_pages = 1U,
        .live_rows = 2U,
        .average_row_width = 8U,
        .sample_rows = 2U,
        .analyzed_xid = base::Xid{1U},
        .columns = {ColumnStatistics{
            .data_type = types::kInt64Type,
            .average_width = 8U,
            .sample_distinct = 2U,
            .minimum = types::Value::integer(1),
            .maximum = types::Value::integer(2),
        }},
    };
}

auto snapshot_is_self_consistent(const CatalogSnapshot& snapshot) -> bool {
    const auto tables = snapshot.list_tables();
    const auto indexes = snapshot.list_indexes();
    if (!std::is_sorted(
            tables.begin(),
            tables.end(),
            [](const auto& left, const auto& right) {
                return left.oid() < right.oid();
            }
        )
        || !std::is_sorted(indexes.begin(), indexes.end(), [](const auto& left, const auto& right) {
               return left.oid() < right.oid();
           })) {
        return false;
    }

    std::unordered_set<base::Oid> object_oids;
    std::unordered_set<base::RelationId> relation_ids;
    std::unordered_set<base::IndexId> index_ids;
    std::unordered_set<std::string> table_names;
    std::unordered_set<std::string> index_names;
    for (const auto& table : tables) {
        if (!object_oids.insert(table.oid()).second
            || !relation_ids.insert(table.relation_id()).second
            || !table_names.insert(std::string{table.name()}).second
            || snapshot.lookup_table(table.name()) != table
            || snapshot.lookup_table(table.oid()) != table) {
            return false;
        }
        const auto statistics = snapshot.table_statistics(table.oid());
        if (statistics && !validate_table_statistics(*statistics, table.schema())) {
            return false;
        }
    }

    for (const auto& index : indexes) {
        const auto owner = snapshot.lookup_table(index.table_oid());
        if (!owner || !object_oids.insert(index.oid()).second
            || !relation_ids.insert(index.relation_id()).second
            || !index_ids.insert(index.index_id()).second
            || !index_names.insert(std::string{index.name()}).second
            || snapshot.lookup_index(index.name()) != index) {
            return false;
        }
        const auto resolved = resolve_index_schema(owner->schema(), index.column_names());
        if (!resolved || resolved->signature != index.schema_signature()) {
            return false;
        }
        const auto owned = snapshot.indexes_for_table(index.table_oid());
        if (std::find(owned.begin(), owned.end(), index) == owned.end()) {
            return false;
        }
    }
    return true;
}

struct StressState final {
    std::atomic<bool> start{false};
    std::atomic<std::size_t> active_writers{0U};
    std::atomic<std::size_t> failures{0U};
    std::atomic<std::size_t> snapshot_observations{0U};
    std::atomic<std::size_t> reopen_observations{0U};
};

void wait_for_start(const StressState& state) {
    while (!state.start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void observe_snapshots(CatalogStore& store, StressState& state) {
    wait_for_start(state);
    std::uint64_t previous_generation = 0U;
    while (state.active_writers.load(std::memory_order_acquire) != 0U) {
        const auto captured = store.snapshot();
        if (!captured || captured->generation() < previous_generation
            || !snapshot_is_self_consistent(captured.value())) {
            state.failures.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        previous_generation = captured->generation();
        state.snapshot_observations.fetch_add(1U, std::memory_order_relaxed);
        std::this_thread::yield();
    }
}

void observe_reopens(
    const std::filesystem::path& path,
    const CatalogStoreOptions store_options,
    StressState& state
) {
    wait_for_start(state);
    std::uint64_t previous_generation = 0U;
    while (state.active_writers.load(std::memory_order_acquire) != 0U) {
        const auto reopened = CatalogStore::open(path, store_options);
        if (!reopened) {
            state.failures.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        const auto captured = reopened->snapshot();
        if (!captured || captured->generation() < previous_generation
            || !snapshot_is_self_consistent(captured.value())) {
            state.failures.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        previous_generation = captured->generation();
        state.reopen_observations.fetch_add(1U, std::memory_order_relaxed);
        std::this_thread::yield();
    }
}

auto run_ddl_cycle(
    CatalogStore& store,
    const Schema& schema,
    const std::size_t writer_index,
    const std::size_t cycle
) -> bool {
    const auto suffix = std::to_string(writer_index) + "_" + std::to_string(cycle);
    const auto table_name = "table_" + suffix;
    const auto first_index_name = "first_index_" + suffix;
    const auto second_index_name = "second_index_" + suffix;
    const auto table = store.create_table(
        CreateTableRequest{
            .name = table_name,
            .schema = schema,
        }
    );
    if (!table || !table->has_value()) {
        return false;
    }
    const auto first_index = store.create_index(
        CreateIndexRequest{
            .name = first_index_name,
            .table_name = table_name,
            .column_names = {"id"},
            .unique = true,
        }
    );
    const auto second_index = store.create_index(
        CreateIndexRequest{
            .name = second_index_name,
            .table_name = table_name,
            .column_names = {"id"},
        }
    );
    if (!first_index || !first_index->has_value() || !second_index || !second_index->has_value()
        || !store.install_statistics({stress_statistics(table->value().oid())})) {
        return false;
    }
    const auto dropped_index = store.drop_index(first_index_name, false);
    const auto invalidated = store.invalidate_statistics(table->value().oid());
    const auto dropped_table = store.drop_table(table_name, false);
    return dropped_index && dropped_index->has_value() && invalidated && invalidated.value()
           && dropped_table && dropped_table->has_value()
           && dropped_table->value().indexes.size() == 1U;
}

void run_writer(
    CatalogStore& store,
    const Schema& schema,
    StressState& state,
    const std::size_t writer_index,
    const std::size_t cycle_count
) {
    wait_for_start(state);
    for (std::size_t cycle = 0U; cycle < cycle_count; ++cycle) {
        if (!run_ddl_cycle(store, schema, writer_index, cycle)) {
            state.failures.fetch_add(1U, std::memory_order_relaxed);
            break;
        }
    }
    state.active_writers.fetch_sub(1U, std::memory_order_release);
}

TEST(CatalogConcurrencyTest, ConcurrentDdlSnapshotsAndFileReopensPublishOnlyWholeGenerations) {
    auto root = test_support::TestDirectory::create("catalog-concurrent-ddl");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto store_result = CatalogStore::create(path, options());
    ASSERT_TRUE(store_result) << store_result.error().to_string();
    auto& store = store_result.value();
    const auto schema = stress_schema();

    constexpr std::size_t k_writer_count = 4U;
    constexpr std::size_t k_cycles_per_writer = 6U;
    constexpr std::size_t k_reader_count = 2U;
    constexpr std::size_t k_operations_per_cycle = 7U;
    const auto total_cycles = k_writer_count * k_cycles_per_writer;
    StressState state;
    state.active_writers.store(k_writer_count, std::memory_order_relaxed);

    std::vector<std::thread> readers;
    readers.reserve(k_reader_count * 2U);
    for (std::size_t reader_index = 0U; reader_index < k_reader_count; ++reader_index) {
        readers.emplace_back([&] {
            observe_snapshots(store, state);
        });
        readers.emplace_back([&] {
            observe_reopens(path, options(), state);
        });
    }

    std::vector<std::thread> writers;
    writers.reserve(k_writer_count);
    for (std::size_t writer_index = 0U; writer_index < k_writer_count; ++writer_index) {
        writers.emplace_back([&, writer_index] {
            run_writer(store, schema, state, writer_index, k_cycles_per_writer);
        });
    }

    state.start.store(true, std::memory_order_release);
    for (auto& writer : writers) {
        writer.join();
    }
    for (auto& reader : readers) {
        reader.join();
    }

    ASSERT_EQ(state.failures.load(std::memory_order_relaxed), 0U);
    EXPECT_GT(state.snapshot_observations.load(std::memory_order_relaxed), 0U);
    EXPECT_GT(state.reopen_observations.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(store.generation().value(), 1U + total_cycles * k_operations_per_cycle);
    EXPECT_EQ(
        store.id_state().value(),
        (CatalogIdState{
            .next_oid = 1U + total_cycles * 3U,
            .next_relation_id = 1U + total_cycles * 3U,
            .next_index_id = 1U + total_cycles * 2U,
        })
    );
    EXPECT_TRUE(store.list_tables().value().empty());
    EXPECT_TRUE(store.list_indexes().value().empty());
    EXPECT_TRUE(store.list_statistics().value().empty());

    const auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->generation().value(), store.generation().value());
    EXPECT_EQ(reopened->id_state().value(), store.id_state().value());
    const auto captured = reopened->snapshot();
    ASSERT_TRUE(captured);
    EXPECT_TRUE(snapshot_is_self_consistent(captured.value()));
}

}  // namespace
}  // namespace db::catalog
