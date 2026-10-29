#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/recovery/btree_startup_redo.hpp"
#include "db/recovery/durable_state_redo.hpp"
#include "db/recovery/recovery_page_cache.hpp"
#include "db/recovery/startup_runner.hpp"
#include "db/recovery/wal_stream.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/durability.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/xid.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x5BE0'CD19'137E'2179ULL;
inline constexpr std::uint32_t kTimeline = 13U;

class EmptyDisk final : public storage::DiskManager {
public:
    auto read_page(storage::PageAddress address) -> base::Result<storage::Page> override {
        static_cast<void>(address);
        return base::fail(base::Error{base::ErrorCode::kNotFound, "recovery test page is absent"});
    }

    auto write_page(storage::PageAddress address, const storage::Page& page)
        -> base::Status override {
        static_cast<void>(address);
        static_cast<void>(page);
        ++page_writes_;
        return {};
    }

    auto
    allocate_page(base::RelationId relation_id, storage::PageKind kind, std::size_t special_size)
        -> base::Result<storage::PageAddress> override {
        static_cast<void>(relation_id);
        static_cast<void>(kind);
        static_cast<void>(special_size);
        return base::fail(
            base::Error{
                base::ErrorCode::kUnavailable,
                "page allocation is unexpected in a non-page recovery test",
            }
        );
    }

    auto page_count(base::RelationId relation_id) -> base::Result<std::uint64_t> override {
        static_cast<void>(relation_id);
        return 0U;
    }

    auto sync_relation(base::RelationId relation_id) -> base::Status override {
        static_cast<void>(relation_id);
        ++relation_syncs_;
        return {};
    }

    auto sync_all() -> base::Status override {
        return {};
    }

    auto remove_relation(base::RelationId relation_id, bool missing_ok) -> base::Status override {
        static_cast<void>(relation_id);
        static_cast<void>(missing_ok);
        return {};
    }

    auto page_writes() const noexcept -> std::size_t {
        return page_writes_;
    }

    auto relation_syncs() const noexcept -> std::size_t {
        return relation_syncs_;
    }

private:
    std::size_t page_writes_{0U};
    std::size_t relation_syncs_{0U};
};

class NoopDurability final : public storage::PageDurabilityPort {
public:
    auto flush_through(base::Lsn target_lsn) -> base::Status override {
        static_cast<void>(target_lsn);
        return {};
    }
};

class RecordingCatalog final : public CatalogRecoveryPort {
public:
    auto ensure_next_object_id(const std::uint32_t next_object_id) -> base::Status override {
        next_object_id_ = next_object_id;
        ++calls_;
        return {};
    }

    auto next_object_id() const noexcept -> std::uint32_t {
        return next_object_id_;
    }

    auto calls() const noexcept -> std::size_t {
        return calls_;
    }

private:
    std::uint32_t next_object_id_{0U};
    std::size_t calls_{0U};
};

auto manager_options(const bool create) -> wal::WalManagerOptions {
    return wal::WalManagerOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .ring_page_count = 4U,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = create,
    };
}

auto reader_options() -> wal::WalReaderOptions {
    return wal::WalReaderOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .retention_lsn = base::kInvalidLsn,
    };
}

auto transaction_record(const std::uint32_t xid) -> wal::WalRecord {
    return wal::WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{xid},
        .resource_manager = wal::WalResourceManager::kTransaction,
        .info = wal::opcode::transaction::kCommit,
        .blocks = {},
        .main_payload = {},
    };
}

auto catalog_record() -> wal::WalRecord {
    return wal::WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::kInvalidXid,
        .resource_manager = wal::WalResourceManager::kCatalog,
        .info = wal::opcode::catalog::kCreate,
        .blocks = {wal::WalBlockReference{
            .relation_id = base::RelationId{77U},
            .page_id = base::PageId{0U},
            .will_initialize = false,
            .standard_page = true,
            .full_page_image = std::nullopt,
            .delta = {std::byte{0x7FU}},
        }},
        .main_payload = {},
    };
}

auto open_validated_stream(const std::filesystem::path& directory)
    -> base::Result<RecoveryWalStream> {
    auto manager = wal::WalManager::open(directory, manager_options(false));
    if (!manager) {
        return base::fail(std::move(manager).error());
    }
    const auto validation = manager->startup_validation();
    auto stream = RecoveryWalStream::open(
        RecoveryWalStreamOptions{
            .directory = directory,
            .reader_options = reader_options(),
            .redo_lsn = base::kInvalidLsn,
            .trusted_startup_validation = validation,
        }
    );
    auto stopped = manager->shutdown();
    if (!stopped) {
        return base::fail(std::move(stopped).error());
    }
    return stream;
}

auto make_replayer(transaction::CommitLog& commit_log, CatalogRecoveryPort& catalog)
    -> base::Result<DurableStateReplayer> {
    return DurableStateReplayer::create(
        DurableStateReplaySeed{
            .next_xid = transaction::kFirstNormalXid,
            .latest_completed_xid = base::kInvalidXid,
            .next_object_id = 1U,
        },
        commit_log,
        catalog
    );
}

TEST(StartupRecoveryRunnerTest, ReplaysDurableStateAndFinalizesAnEmptyPageCache) {
    auto root = test_support::TestDirectory::create("startup-recovery-runner-transaction");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto manager = wal::WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager) << manager.error().to_string();
        auto inserted = manager->insert(transaction_record(23U));
        ASSERT_TRUE(inserted) << inserted.error().to_string();
        ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    auto stream = open_validated_stream(directory);
    ASSERT_TRUE(stream) << stream.error().to_string();
    EmptyDisk disk;
    NoopDurability durability;
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 2U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    transaction::CommitLog commit_log;
    RecordingCatalog catalog;
    auto replayer = make_replayer(commit_log, catalog);
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    auto report = run_startup_recovery(
        StartupRecoveryRunContext{
            .wal_stream = stream.value(),
            .page_cache = *cache.value(),
            .durable_state = replayer.value(),
        }
    );
    ASSERT_TRUE(report) << report.error().to_string();
    EXPECT_EQ(report->records_replayed, 1U);
    EXPECT_EQ(report->transaction_records_replayed, 1U);
    EXPECT_EQ(report->heap_records_replayed, 0U);
    EXPECT_EQ(report->btree_records_replayed, 0U);
    EXPECT_EQ(report->durable_state.transaction_records_replayed, 1U);
    EXPECT_EQ(report->durable_state.next_xid, base::Xid{24U});
    EXPECT_EQ(report->last_lsn, stream->logical_end_lsn());
    EXPECT_TRUE(cache.value()->is_finalized());
    EXPECT_TRUE(commit_log.is_committed(base::Xid{23U}));
    EXPECT_EQ(catalog.next_object_id(), 1U);
    EXPECT_EQ(catalog.calls(), 1U);
    EXPECT_EQ(disk.page_writes(), 0U);
    EXPECT_EQ(disk.relation_syncs(), 0U);
}

TEST(StartupRecoveryRunnerTest, RejectsDeferredCatalogRouteAndAbandonsOnlyTheCache) {
    auto root = test_support::TestDirectory::create("startup-recovery-runner-catalog");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto manager = wal::WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager) << manager.error().to_string();
        auto inserted = manager->insert(catalog_record());
        ASSERT_TRUE(inserted) << inserted.error().to_string();
        ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    auto stream = open_validated_stream(directory);
    ASSERT_TRUE(stream) << stream.error().to_string();
    EmptyDisk disk;
    NoopDurability durability;
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 2U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    transaction::CommitLog commit_log;
    RecordingCatalog catalog;
    auto replayer = make_replayer(commit_log, catalog);
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    auto rejected = run_startup_recovery(
        StartupRecoveryRunContext{
            .wal_stream = stream.value(),
            .page_cache = *cache.value(),
            .durable_state = replayer.value(),
        }
    );
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_TRUE(cache.value()->is_abandoned());
    EXPECT_FALSE(cache.value()->is_finalized());
    EXPECT_EQ(cache.value()->cached_page_count(), 0U);
    EXPECT_EQ(disk.page_writes(), 0U);
    EXPECT_EQ(disk.relation_syncs(), 0U);
}

}  // namespace
}  // namespace db::recovery
