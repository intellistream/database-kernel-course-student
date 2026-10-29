#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/catalog/consistency.hpp"
#include "db/catalog/format.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"
#include "db/catalog/store.hpp"
#include "db/io/durable.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"

namespace db::catalog {
namespace {

constexpr RowSizeLimit kRowLimit{.maximum_encoded_size = 4'036U};

enum class PublicationFailure : std::uint8_t {
    kNone,
    kBeforeRename,
    kAfterRename,
    kRemoveAuthoritative,
    kThrowAllocation,
    kThrowBeforeRename,
    kThrowAfterRename,
};

class ScriptedPublication final : public CatalogPublicationPort {
public:
    ScriptedPublication()
        : calls_(std::make_shared<std::size_t>(0U)) {}

    explicit ScriptedPublication(std::shared_ptr<std::size_t> calls)
        : calls_(std::move(calls)) {}

    auto publish(
        const std::filesystem::path& path,
        const base::Bytes image,
        const std::uint32_t permissions
    ) -> base::Status override {
        ++*calls_;
        const auto failure = next_failure_;
        next_failure_ = PublicationFailure::kNone;
        if (failure == PublicationFailure::kBeforeRename) {
            return injected_failure("before rename");
        }
        if (failure == PublicationFailure::kThrowAllocation) {
            throw std::bad_alloc{};
        }
        if (failure == PublicationFailure::kThrowBeforeRename) {
            throw std::runtime_error{"injected catalog publication exception before rename"};
        }
        if (failure == PublicationFailure::kRemoveAuthoritative) {
            if (auto removed = io::remove_file_durable(path); !removed) {
                return removed;
            }
            return injected_failure("after removing authoritative file");
        }
        auto published = io::atomic_replace(path, image, permissions);
        if (!published) {
            return published;
        }
        if (failure == PublicationFailure::kAfterRename) {
            return injected_failure("after rename");
        }
        if (failure == PublicationFailure::kThrowAfterRename) {
            throw std::runtime_error{"injected catalog publication exception after rename"};
        }
        return {};
    }

    void fail_next(const PublicationFailure failure) noexcept {
        next_failure_ = failure;
    }

    auto calls() const noexcept -> std::size_t {
        return *calls_;
    }

private:
    static auto injected_failure(const std::string& boundary) -> base::Status {
        return base::fail(
            base::Error{
                base::ErrorCode::kIo,
                "injected catalog publication failure " + boundary,
            }
        );
    }

    PublicationFailure next_failure_{PublicationFailure::kNone};
    std::shared_ptr<std::size_t> calls_;
};

class MissingRelationValidation final : public IndexRelationValidationPort {
public:
    auto validate(IndexRelationValidationRequest request) -> base::Status override {
        observed.push_back(request.relation_id);
        return base::fail(base::Error{base::ErrorCode::kNotFound, "relation is missing"});
    }

    std::vector<base::RelationId> observed;
};

auto options() -> CatalogStoreOptions {
    return CatalogStoreOptions{
        .decode = CatalogDecodeOptions{.row_size_limit = kRowLimit},
    };
}

auto one_column_schema(const std::string& name = "id") -> Schema {
    auto schema = Schema::create(
        std::vector<Column>{Column{.name = name, .data_type = types::kInt64Type}},
        kRowLimit
    );
    EXPECT_TRUE(schema) << schema.error().to_string();
    return std::move(schema).value();
}

auto create_table(
    CatalogStore& store,
    std::string name,
    const bool if_not_exists = false,
    const std::uint8_t fillfactor = kDefaultHeapFillfactor
) -> base::Result<std::optional<TableMetadata>> {
    return store.create_table(
        CreateTableRequest{
            .name = std::move(name),
            .schema = one_column_schema(),
            .fillfactor = fillfactor,
            .if_not_exists = if_not_exists,
        }
    );
}

auto create_index(
    CatalogStore& store,
    std::string name,
    std::string table_name,
    const bool unique = false,
    const bool if_not_exists = false
) -> base::Result<std::optional<IndexMetadata>> {
    return store.create_index(
        CreateIndexRequest{
            .name = std::move(name),
            .table_name = std::move(table_name),
            .column_names = {"id"},
            .unique = unique,
            .if_not_exists = if_not_exists,
        }
    );
}

auto statistics(
    const base::Oid table_oid,
    const std::uint64_t live_rows,
    const std::int64_t minimum = 1,
    const std::int64_t maximum = 9
) -> TableStatistics {
    return TableStatistics{
        .table_oid = table_oid,
        .relation_pages = 3U,
        .live_rows = live_rows,
        .dead_rows = 2U,
        .average_row_width = 8U,
        .sample_rows = static_cast<std::uint32_t>(live_rows),
        .analyzed_xid = base::Xid{17U},
        .columns = {ColumnStatistics{
            .data_type = types::kInt64Type,
            .average_width = 8U,
            .sample_distinct = live_rows,
            .minimum = types::Value::integer(minimum),
            .maximum = types::Value::integer(maximum),
        }},
    };
}

TEST(CatalogStoreTest, CreatesListsAndReopensCanonicalMetadata) {
    auto root = test_support::TestDirectory::create("catalog-store-roundtrip");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog/main.cat");

    auto created = CatalogStore::create(path, options());
    ASSERT_TRUE(created) << created.error().to_string();
    EXPECT_EQ(created->generation().value(), 1U);
    EXPECT_EQ(created->id_state().value(), CatalogIdState{});

    auto accounts = create_table(created.value(), "accounts", false, 90U);
    ASSERT_TRUE(accounts);
    ASSERT_TRUE(accounts->has_value());
    EXPECT_EQ(accounts->value().oid(), base::Oid{1U});
    EXPECT_EQ(accounts->value().relation_id(), base::RelationId{1U});
    EXPECT_EQ(accounts->value().fillfactor(), 90U);

    auto events = create_table(created.value(), "events");
    ASSERT_TRUE(events);
    ASSERT_TRUE(events->has_value());
    EXPECT_EQ(events->value().oid(), base::Oid{2U});
    EXPECT_EQ(events->value().relation_id(), base::RelationId{2U});

    auto events_index = create_index(created.value(), "events_id_idx", "events");
    ASSERT_TRUE(events_index);
    ASSERT_TRUE(events_index->has_value());
    EXPECT_EQ(events_index->value().oid(), base::Oid{3U});
    EXPECT_EQ(events_index->value().index_id(), base::IndexId{1U});
    EXPECT_EQ(events_index->value().relation_id(), base::RelationId{3U});
    EXPECT_EQ(events_index->value().table_oid(), base::Oid{2U});
    EXPECT_EQ(
        events_index->value().schema_signature(),
        index_schema_signature(std::vector<types::DataType>{types::kInt64Type}).value()
    );

    auto accounts_index = create_index(created.value(), "accounts_id_key", "accounts", true);
    ASSERT_TRUE(accounts_index);
    ASSERT_TRUE(accounts_index->has_value());
    EXPECT_TRUE(accounts_index->value().unique());
    EXPECT_EQ(created->generation().value(), 5U);
    EXPECT_EQ(
        created->id_state().value(),
        (CatalogIdState{.next_oid = 5U, .next_relation_id = 5U, .next_index_id = 3U})
    );

    const auto tables = created->list_tables();
    ASSERT_TRUE(tables);
    ASSERT_EQ(tables->size(), 2U);
    EXPECT_EQ((*tables)[0].name(), "accounts");
    EXPECT_EQ((*tables)[1].name(), "events");
    const auto indexes = created->list_indexes();
    ASSERT_TRUE(indexes);
    ASSERT_EQ(indexes->size(), 2U);
    EXPECT_EQ((*indexes)[0].name(), "events_id_idx");
    EXPECT_EQ((*indexes)[1].name(), "accounts_id_key");

    auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->generation().value(), 5U);
    EXPECT_EQ(reopened->id_state().value(), created->id_state().value());
    ASSERT_TRUE(reopened->lookup_table("events").value().has_value());
    ASSERT_TRUE(reopened->lookup_table(base::Oid{1U}).value().has_value());
    ASSERT_TRUE(reopened->lookup_index("accounts_id_key").value().has_value());
    const auto owned_indexes = reopened->indexes_for_table(base::Oid{1U});
    ASSERT_TRUE(owned_indexes);
    ASSERT_EQ(owned_indexes->size(), 1U);
    EXPECT_EQ((*owned_indexes)[0].name(), "accounts_id_key");
}

TEST(CatalogStoreTest, SnapshotPinsOneCompleteGenerationAcrossLaterDdl) {
    auto root = test_support::TestDirectory::create("catalog-store-snapshot-generation");
    ASSERT_TRUE(root);
    auto store = CatalogStore::create(root->join("catalog.cat"), options());
    ASSERT_TRUE(store);

    const auto created = create_table(store.value(), "items");
    ASSERT_TRUE(created);
    ASSERT_TRUE(created->has_value());
    const auto table = created->value();
    const auto index = create_index(store.value(), "items_id_idx", "items", true);
    ASSERT_TRUE(index);
    ASSERT_TRUE(index->has_value());
    const auto analyzed = statistics(table.oid(), 2U, 1, 2);
    ASSERT_TRUE(store->install_statistics({analyzed}));

    const auto pinned_result = store->snapshot();
    ASSERT_TRUE(pinned_result);
    const auto& pinned = pinned_result.value();
    EXPECT_EQ(pinned.generation(), 4U);
    EXPECT_EQ(pinned.lookup_table("items"), table);
    EXPECT_EQ(pinned.lookup_table(table.oid()), table);
    EXPECT_EQ(pinned.lookup_index("items_id_idx"), index->value());
    EXPECT_EQ(pinned.indexes_for_table(table.oid()), std::vector{index->value()});
    EXPECT_EQ(pinned.table_statistics(table.oid()), analyzed);

    ASSERT_TRUE(store->drop_table("items", false));
    EXPECT_EQ(store->generation().value(), 5U);
    const auto current_result = store->snapshot();
    ASSERT_TRUE(current_result);
    const auto& current = current_result.value();
    EXPECT_EQ(current.generation(), 5U);
    EXPECT_TRUE(current.list_tables().empty());
    EXPECT_TRUE(current.list_indexes().empty());
    EXPECT_FALSE(current.table_statistics(table.oid()).has_value());

    EXPECT_EQ(pinned.generation(), 4U);
    EXPECT_EQ(pinned.lookup_table("items"), table);
    EXPECT_EQ(pinned.lookup_index("items_id_idx"), index->value());
    EXPECT_EQ(pinned.table_statistics(table.oid()), analyzed);
}

TEST(CatalogStoreTest, RecoveryOidFloorPublishesOnlyStrictPersistentAdvances) {
    auto root = test_support::TestDirectory::create("catalog-store-recovery-oid-floor");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog/main.cat");
    auto store = CatalogStore::create(path, options());
    ASSERT_TRUE(store) << store.error().to_string();

    auto invalid = store->ensure_next_oid_at_least(0U);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);
    auto unchanged = store->ensure_next_oid_at_least(1U);
    ASSERT_TRUE(unchanged) << unchanged.error().to_string();
    EXPECT_FALSE(unchanged.value());
    EXPECT_EQ(store->generation().value(), 1U);

    auto advanced = store->ensure_next_oid_at_least(73U);
    ASSERT_TRUE(advanced) << advanced.error().to_string();
    EXPECT_TRUE(advanced.value());
    EXPECT_EQ(store->generation().value(), 2U);
    EXPECT_EQ(
        store->id_state().value(),
        (CatalogIdState{.next_oid = 73U, .next_relation_id = 1U, .next_index_id = 1U})
    );

    unchanged = store->ensure_next_oid_at_least(72U);
    ASSERT_TRUE(unchanged) << unchanged.error().to_string();
    EXPECT_FALSE(unchanged.value());
    EXPECT_EQ(store->generation().value(), 2U);

    auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->generation().value(), 2U);
    EXPECT_EQ(reopened->id_state()->next_oid, 73U);
    EXPECT_EQ(reopened->id_state()->next_relation_id, 1U);
    EXPECT_EQ(reopened->id_state()->next_index_id, 1U);
}

TEST(CatalogStoreTest, IfClausesAndValidationArePublicationFreeNoOps) {
    auto root = test_support::TestDirectory::create("catalog-store-if-no-op");
    ASSERT_TRUE(root);
    auto publication = std::make_unique<ScriptedPublication>();
    auto* observed = publication.get();
    auto store = CatalogStore::create(root->join("catalog.cat"), options(), std::move(publication));
    ASSERT_TRUE(store);
    ASSERT_TRUE(create_table(store.value(), "t"));
    ASSERT_TRUE(create_index(store.value(), "t_id_idx", "t"));
    ASSERT_EQ(observed->calls(), 3U);
    const auto generation = store->generation().value();
    const auto state = store->id_state().value();

    auto duplicate_table = create_table(store.value(), "t", true);
    ASSERT_TRUE(duplicate_table);
    EXPECT_FALSE(duplicate_table->has_value());
    EXPECT_EQ(create_table(store.value(), "t").error().code(), base::ErrorCode::kAlreadyExists);
    auto duplicate_index = create_index(store.value(), "t_id_idx", "t", false, true);
    ASSERT_TRUE(duplicate_index);
    EXPECT_FALSE(duplicate_index->has_value());
    auto duplicate_index_missing_owner =
        create_index(store.value(), "t_id_idx", "missing", false, true);
    ASSERT_TRUE(duplicate_index_missing_owner);
    EXPECT_FALSE(duplicate_index_missing_owner->has_value());
    EXPECT_EQ(
        create_index(store.value(), "t_id_idx", "missing").error().code(),
        base::ErrorCode::kAlreadyExists
    );
    EXPECT_EQ(
        create_index(store.value(), "missing_idx", "missing").error().code(),
        base::ErrorCode::kNotFound
    );
    EXPECT_EQ(
        store
            ->create_table(
                CreateTableRequest{
                    .name = "",
                    .schema = one_column_schema(),
                }
            )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        store
            ->create_index(
                CreateIndexRequest{
                    .name = "missing_column_idx",
                    .table_name = "t",
                    .column_names = {"missing"},
                }
            )
            .error()
            .code(),
        base::ErrorCode::kNotFound
    );
    EXPECT_EQ(
        store
            ->create_table(
                CreateTableRequest{
                    .name = "bad_fillfactor",
                    .schema = one_column_schema(),
                    .fillfactor = 9U,
                }
            )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        store
            ->create_index(
                CreateIndexRequest{
                    .name = "bad_index",
                    .table_name = "t",
                    .column_names = {},
                }
            )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );

    auto missing_table = store->drop_table("missing", true);
    ASSERT_TRUE(missing_table);
    EXPECT_FALSE(missing_table->has_value());
    EXPECT_EQ(store->drop_table("missing", false).error().code(), base::ErrorCode::kNotFound);
    auto missing_index = store->drop_index("missing", true);
    ASSERT_TRUE(missing_index);
    EXPECT_FALSE(missing_index->has_value());
    EXPECT_EQ(store->drop_index("missing", false).error().code(), base::ErrorCode::kNotFound);

    EXPECT_EQ(observed->calls(), 3U);
    EXPECT_EQ(store->generation().value(), generation);
    EXPECT_EQ(store->id_state().value(), state);
}

TEST(CatalogStoreTest, RejectsAConfigurationWhosePublicationLimitIsTooSmall) {
    auto root = test_support::TestDirectory::create("catalog-store-decode-limit");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto observed_calls = std::make_shared<std::size_t>(0U);
    auto publication = std::make_unique<ScriptedPublication>(observed_calls);
    auto limited = options();
    limited.decode.maximum_image_size = kCatalogFormatHeaderSize - 1U;

    auto store = CatalogStore::create(path, limited, std::move(publication));
    ASSERT_FALSE(store);
    EXPECT_EQ(store.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ(*observed_calls, 0U);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(CatalogStoreTest, EnforcesConfiguredLimitsWhenObjectsEnterTheCatalog) {
    auto root = test_support::TestDirectory::create("catalog-store-object-limits");
    ASSERT_TRUE(root);
    auto limited = options();
    limited.decode.maximum_tables = 1U;
    limited.decode.maximum_indexes = 0U;
    limited.decode.maximum_statistics = 0U;
    limited.decode.maximum_columns_per_table = 1U;
    limited.decode.row_size_limit.maximum_encoded_size = sizeof(std::int64_t) + 1U;
    auto store = CatalogStore::create(root->join("catalog.cat"), limited);
    ASSERT_TRUE(store) << store.error().to_string();

    auto first = store->create_table(
        CreateTableRequest{
            .name = "first",
            .schema = one_column_schema(),
        }
    );
    ASSERT_TRUE(first) << first.error().to_string();
    EXPECT_EQ(
        store->create_table(CreateTableRequest{.name = "second", .schema = one_column_schema()})
            .error()
            .code(),
        base::ErrorCode::kResourceExhausted
    );
    EXPECT_EQ(
        store->install_statistics({statistics(first->value().oid(), 1U)}).error().code(),
        base::ErrorCode::kResourceExhausted
    );
    EXPECT_EQ(
        store
            ->create_index(
                CreateIndexRequest{
                    .name = "first_id",
                    .table_name = "first",
                    .column_names = {"id"},
                }
            )
            .error()
            .code(),
        base::ErrorCode::kResourceExhausted
    );

    auto wide_schema = Schema::create(
        std::vector<Column>{
            Column{.name = "a", .data_type = types::kBoolType},
            Column{.name = "b", .data_type = types::kBoolType},
        },
        kRowLimit
    );
    ASSERT_TRUE(wide_schema);
    auto columns_limited = options();
    columns_limited.decode.maximum_columns_per_table = 1U;
    auto columns_store = CatalogStore::create(root->join("columns.cat"), columns_limited);
    ASSERT_TRUE(columns_store);
    EXPECT_EQ(
        columns_store
            ->create_table(
                CreateTableRequest{
                    .name = "wide",
                    .schema = std::move(wide_schema).value(),
                }
            )
            .error()
            .code(),
        base::ErrorCode::kResourceExhausted
    );

    auto row_limited = options();
    row_limited.decode.row_size_limit.maximum_encoded_size = 1U;
    auto row_store = CatalogStore::create(root->join("rows.cat"), row_limited);
    ASSERT_TRUE(row_store);
    EXPECT_EQ(
        row_store
            ->create_table(
                CreateTableRequest{
                    .name = "oversized_row",
                    .schema = one_column_schema(),
                }
            )
            .error()
            .code(),
        base::ErrorCode::kResourceExhausted
    );
}

TEST(CatalogStoreTest, DropIndexAndTableCascadePublishOneGenerationEach) {
    auto root = test_support::TestDirectory::create("catalog-store-drop");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto store = CatalogStore::create(path, options());
    ASSERT_TRUE(store);
    ASSERT_TRUE(create_table(store.value(), "first"));
    ASSERT_TRUE(create_table(store.value(), "second"));
    ASSERT_TRUE(create_index(store.value(), "first_a", "first"));
    ASSERT_TRUE(create_index(store.value(), "first_b", "first", true));
    ASSERT_TRUE(create_index(store.value(), "second_a", "second"));
    const auto frontier_before_drop = store->id_state().value();

    auto dropped_index = store->drop_index("second_a", false);
    ASSERT_TRUE(dropped_index);
    ASSERT_TRUE(dropped_index->has_value());
    EXPECT_EQ(dropped_index->value().table_oid(), base::Oid{2U});
    EXPECT_EQ(store->generation().value(), 7U);

    auto dropped_table = store->drop_table("first", false);
    ASSERT_TRUE(dropped_table);
    ASSERT_TRUE(dropped_table->has_value());
    EXPECT_EQ(dropped_table->value().table.oid(), base::Oid{1U});
    ASSERT_EQ(dropped_table->value().indexes.size(), 2U);
    EXPECT_EQ(dropped_table->value().indexes[0].name(), "first_a");
    EXPECT_EQ(dropped_table->value().indexes[1].name(), "first_b");
    EXPECT_EQ(store->generation().value(), 8U);
    EXPECT_EQ(store->id_state().value(), frontier_before_drop);
    EXPECT_FALSE(store->lookup_table("first").value().has_value());
    EXPECT_TRUE(store->lookup_table("second").value().has_value());
    EXPECT_TRUE(store->list_indexes().value().empty());

    auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->generation().value(), 8U);
    EXPECT_EQ(reopened->id_state().value(), frontier_before_drop);
    EXPECT_EQ(reopened->list_tables().value().size(), 1U);
    EXPECT_TRUE(reopened->list_indexes().value().empty());
}

TEST(CatalogStoreTest, StatisticsBatchRoundTripsAndInvalidatesAtomically) {
    auto root = test_support::TestDirectory::create("catalog-store-statistics");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto publication = std::make_unique<ScriptedPublication>();
    auto* observed = publication.get();
    auto store = CatalogStore::create(path, options(), std::move(publication));
    ASSERT_TRUE(store);
    const auto first = create_table(store.value(), "first");
    const auto second = create_table(store.value(), "second");
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(first->has_value());
    ASSERT_TRUE(second->has_value());

    auto first_statistics = statistics(first->value().oid(), 2U, 1, 2);
    auto second_statistics = statistics(second->value().oid(), 3U, 3, 5);
    ASSERT_TRUE(store->install_statistics({second_statistics, first_statistics}));
    EXPECT_EQ(store->generation().value(), 4U);
    EXPECT_EQ(observed->calls(), 4U);
    const auto listed = store->list_statistics();
    ASSERT_TRUE(listed);
    ASSERT_EQ(listed->size(), 2U);
    EXPECT_EQ((*listed)[0], first_statistics);
    EXPECT_EQ((*listed)[1], second_statistics);

    auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->table_statistics(first->value().oid()).value(), first_statistics);
    EXPECT_EQ(reopened->table_statistics(second->value().oid()).value(), second_statistics);

    const auto generation = store->generation().value();
    auto replacement = statistics(first->value().oid(), 1U, 7, 7);
    auto orphan = statistics(base::Oid{999U}, 1U, 8, 8);
    EXPECT_EQ(
        store->install_statistics({replacement, orphan}).error().code(),
        base::ErrorCode::kNotFound
    );
    EXPECT_EQ(store->generation().value(), generation);
    EXPECT_EQ(store->table_statistics(first->value().oid()).value(), first_statistics);
    EXPECT_EQ(observed->calls(), 4U);
    EXPECT_EQ(
        store->install_statistics({first_statistics, first_statistics}).error().code(),
        base::ErrorCode::kAlreadyExists
    );
    EXPECT_TRUE(store->install_statistics({}));
    EXPECT_EQ(observed->calls(), 4U);

    ASSERT_TRUE(create_index(store.value(), "first_id_idx", "first"));
    EXPECT_EQ(store->table_statistics(first->value().oid()).value(), first_statistics);
    EXPECT_FALSE(store->invalidate_statistics(base::Oid{777U}));
    EXPECT_EQ(
        store->invalidate_statistics(base::Oid{777U}).error().code(),
        base::ErrorCode::kNotFound
    );
}

TEST(CatalogStoreTest, ExplicitAndDropInvalidationPublishExactlyOnce) {
    auto root = test_support::TestDirectory::create("catalog-store-statistics-invalidation");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto store = CatalogStore::create(path, options());
    ASSERT_TRUE(store);
    const auto created = create_table(store.value(), "items");
    ASSERT_TRUE(created);
    ASSERT_TRUE(created->has_value());
    const auto table_oid = created->value().oid();
    ASSERT_TRUE(store->install_statistics({statistics(table_oid, 2U, 1, 2)}));
    EXPECT_EQ(store->generation().value(), 3U);

    const auto invalidated = store->invalidate_statistics(table_oid);
    ASSERT_TRUE(invalidated);
    EXPECT_TRUE(invalidated.value());
    EXPECT_EQ(store->generation().value(), 4U);
    EXPECT_FALSE(store->table_statistics(table_oid).value().has_value());
    EXPECT_FALSE(store->invalidate_statistics(table_oid).value());
    EXPECT_EQ(store->generation().value(), 4U);

    ASSERT_TRUE(store->install_statistics({statistics(table_oid, 2U, 1, 2)}));
    const auto dropped = store->drop_table("items", false);
    ASSERT_TRUE(dropped);
    EXPECT_FALSE(store->table_statistics(table_oid).value().has_value());
    const auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->list_statistics().value().empty());
}

TEST(CatalogStoreTest, FailedStatisticsPublicationDoesNotPartiallyReplaceState) {
    auto root = test_support::TestDirectory::create("catalog-store-statistics-failure");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto publication = std::make_unique<ScriptedPublication>();
    auto* observed = publication.get();
    auto store = CatalogStore::create(path, options(), std::move(publication));
    ASSERT_TRUE(store);
    const auto created = create_table(store.value(), "items");
    ASSERT_TRUE(created);
    ASSERT_TRUE(created->has_value());
    const auto original = statistics(created->value().oid(), 2U, 1, 2);
    ASSERT_TRUE(store->install_statistics({original}));

    observed->fail_next(PublicationFailure::kBeforeRename);
    const auto failed = store->install_statistics({statistics(created->value().oid(), 1U, 9, 9)});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(store->table_statistics(created->value().oid()).value(), original);
    const auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->table_statistics(created->value().oid()).value(), original);
}

TEST(CatalogStoreTest, ExposesPhysicalAuditAndStableRebuildError) {
    auto root = test_support::TestDirectory::create("catalog-store-index-audit");
    ASSERT_TRUE(root);
    auto store = CatalogStore::create(root->join("catalog.cat"), options());
    ASSERT_TRUE(store);
    ASSERT_TRUE(create_table(store.value(), "items"));
    const auto created = create_index(store.value(), "items_id_idx", "items");
    ASSERT_TRUE(created);
    ASSERT_TRUE(created->has_value());

    MissingRelationValidation validation;
    const auto diagnostics = store->audit_indexes(validation);
    ASSERT_TRUE(diagnostics) << diagnostics.error().to_string();
    ASSERT_EQ(diagnostics->size(), 1U);
    EXPECT_EQ((*diagnostics)[0].reason, IndexRebuildReason::kMissingRelation);
    EXPECT_EQ(validation.observed, (std::vector<base::RelationId>{base::RelationId{2U}}));

    const auto required = store->require_index_consistency(validation);
    ASSERT_FALSE(required);
    EXPECT_EQ(required.error().code(), base::ErrorCode::kRebuildRequired);
}

TEST(CatalogStoreTest, FailureBeforeRenameKeepsMemoryFileAndIdsRetryable) {
    auto root = test_support::TestDirectory::create("catalog-store-before-rename");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto publication = std::make_unique<ScriptedPublication>();
    auto* observed = publication.get();
    auto store = CatalogStore::create(path, options(), std::move(publication));
    ASSERT_TRUE(store);

    observed->fail_next(PublicationFailure::kBeforeRename);
    auto failed = create_table(store.value(), "retry_me");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(store->generation().value(), 1U);
    EXPECT_EQ(store->id_state().value(), CatalogIdState{});
    EXPECT_FALSE(store->lookup_table("retry_me").value().has_value());
    auto old_reopen = CatalogStore::open(path, options());
    ASSERT_TRUE(old_reopen);
    EXPECT_TRUE(old_reopen->list_tables().value().empty());

    auto retried = create_table(store.value(), "retry_me");
    ASSERT_TRUE(retried);
    ASSERT_TRUE(retried->has_value());
    EXPECT_EQ(retried->value().oid(), base::Oid{1U});
    EXPECT_EQ(retried->value().relation_id(), base::RelationId{1U});
}

TEST(CatalogStoreTest, FailureAfterRenameReconcilesPublishedGeneration) {
    auto root = test_support::TestDirectory::create("catalog-store-after-rename");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto publication = std::make_unique<ScriptedPublication>();
    auto* observed = publication.get();
    auto store = CatalogStore::create(path, options(), std::move(publication));
    ASSERT_TRUE(store);

    observed->fail_next(PublicationFailure::kAfterRename);
    auto failed = create_table(store.value(), "published");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(store->generation().value(), 2U);
    EXPECT_TRUE(store->lookup_table("published").value().has_value());
    EXPECT_EQ(store->id_state()->next_oid, 2U);

    auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->generation().value(), 2U);
    EXPECT_TRUE(reopened->lookup_table("published").value().has_value());
    EXPECT_EQ(
        create_table(store.value(), "published").error().code(),
        base::ErrorCode::kAlreadyExists
    );
}

TEST(CatalogStoreTest, PublicationExceptionsAreStructuredAndReconciled) {
    auto root = test_support::TestDirectory::create("catalog-store-publication-exception");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto publication = std::make_unique<ScriptedPublication>();
    auto* observed = publication.get();
    auto store = CatalogStore::create(path, options(), std::move(publication));
    ASSERT_TRUE(store);

    observed->fail_next(PublicationFailure::kThrowBeforeRename);
    auto before = create_table(store.value(), "retry_me");
    ASSERT_FALSE(before);
    EXPECT_EQ(before.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(before.error().to_string().find("exception before rename"), std::string::npos);
    EXPECT_EQ(store->generation().value(), 1U);
    EXPECT_FALSE(store->lookup_table("retry_me").value().has_value());

    auto retried = create_table(store.value(), "retry_me");
    ASSERT_TRUE(retried);
    ASSERT_TRUE(retried->has_value());
    EXPECT_EQ(retried->value().oid(), base::Oid{1U});

    observed->fail_next(PublicationFailure::kThrowAfterRename);
    auto after = create_table(store.value(), "published");
    ASSERT_FALSE(after);
    EXPECT_EQ(after.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(after.error().to_string().find("exception after rename"), std::string::npos);
    EXPECT_EQ(store->generation().value(), 3U);
    EXPECT_TRUE(store->lookup_table("published").value().has_value());

    auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->generation().value(), 3U);
    EXPECT_TRUE(reopened->lookup_table("published").value().has_value());
}

TEST(CatalogStoreTest, CreateConvertsPublicationAllocationException) {
    auto root = test_support::TestDirectory::create("catalog-store-create-exception");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto publication = std::make_unique<ScriptedPublication>();
    publication->fail_next(PublicationFailure::kThrowAllocation);

    auto created = CatalogStore::create(path, options(), std::move(publication));
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(CatalogStoreTest, UnreadableAuthoritativeStateRequiresReopen) {
    auto root = test_support::TestDirectory::create("catalog-store-uncertain");
    ASSERT_TRUE(root);
    auto publication = std::make_unique<ScriptedPublication>();
    auto* observed = publication.get();
    auto store = CatalogStore::create(root->join("catalog.cat"), options(), std::move(publication));
    ASSERT_TRUE(store);

    observed->fail_next(PublicationFailure::kRemoveAuthoritative);
    auto failed = create_table(store.value(), "uncertain");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(store->generation().error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(store->list_tables().error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(create_table(store.value(), "blocked").error().code(), base::ErrorCode::kUnavailable);
}

TEST(CatalogStoreTest, CreateRejectsExistingAndMovedFromStoreIsUnavailable) {
    auto root = test_support::TestDirectory::create("catalog-store-lifecycle");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto first = CatalogStore::create(path, options());
    ASSERT_TRUE(first);
    EXPECT_EQ(
        CatalogStore::create(path, options()).error().code(),
        base::ErrorCode::kAlreadyExists
    );

    auto moved = std::move(first).value();
    EXPECT_TRUE(moved.valid());
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies the documented moved-from facade.
    EXPECT_FALSE(first->valid());
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies the documented moved-from facade.
    EXPECT_EQ(first->generation().error().code(), base::ErrorCode::kUnavailable);
    // NOLINTNEXTLINE(bugprone-use-after-move) - every moved-from read surface must reject access.
    EXPECT_EQ(first->snapshot().error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(moved.generation().value(), 1U);
    EXPECT_EQ(
        CatalogStore::create({}, options()).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(CatalogStore::open({}, options()).error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(CatalogStoreTest, ConcurrentDistinctCreatesSerializeIdsAndPublications) {
    auto root = test_support::TestDirectory::create("catalog-store-concurrent-create");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    auto store = CatalogStore::create(path, options());
    ASSERT_TRUE(store);

    constexpr std::size_t k_threads = 8U;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0U};
    std::vector<std::thread> threads;
    threads.reserve(k_threads);
    for (std::size_t index = 0; index < k_threads; ++index) {
        threads.emplace_back([&, index] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            auto created = store->create_table(
                CreateTableRequest{
                    .name = "table_" + std::to_string(index),
                    .schema = one_column_schema(),
                }
            );
            if (!created || !created->has_value()) {
                failures.fetch_add(1U, std::memory_order_relaxed);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(store->generation().value(), k_threads + 1U);
    const auto tables = store->list_tables();
    ASSERT_TRUE(tables);
    ASSERT_EQ(tables->size(), k_threads);
    for (std::size_t index = 0; index < tables->size(); ++index) {
        EXPECT_EQ((*tables)[index].oid().value(), index + 1U);
        EXPECT_EQ((*tables)[index].relation_id().value(), index + 1U);
    }

    const auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->generation().value(), k_threads + 1U);
    EXPECT_EQ(reopened->list_tables().value().size(), k_threads);
}

}  // namespace
}  // namespace db::catalog
