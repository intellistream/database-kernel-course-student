#include <cstddef>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/recovery/wal_durability.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/page.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x4455'5241'4249'4C31ULL;

auto wal_options(const bool create) -> wal::WalManagerOptions {
    return wal::WalManagerOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = 1U,
        .ring_page_count = 4U,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = create,
    };
}

auto record(const std::uint32_t xid) -> wal::WalRecord {
    return wal::WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{xid},
        .resource_manager = wal::WalResourceManager::kTransaction,
        .info = 0U,
        .blocks = {},
        .main_payload = {static_cast<std::byte>(xid & 0xFFU)},
    };
}

TEST(WalPageDurabilityAdapterTest, RetainsManagerAndAcceptsHistoricalFlushedLsn) {
    auto root = test_support::TestDirectory::create("recovery-wal-durability-reopen");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    wal::WalInsertResult first{};
    {
        auto opened = wal::WalManager::open(directory, wal_options(true));
        ASSERT_TRUE(opened);
        auto first_inserted = opened->insert(record(1U));
        ASSERT_TRUE(first_inserted);
        first = first_inserted.value();
        auto second = opened->insert(record(2U));
        ASSERT_TRUE(second);
        ASSERT_TRUE(opened->flush_through(second->end_lsn));
        ASSERT_TRUE(opened->shutdown());
    }

    auto reopened = wal::WalManager::open(directory, wal_options(false));
    ASSERT_TRUE(reopened);
    auto manager = std::make_shared<wal::WalManager>(std::move(reopened).value());
    std::weak_ptr<wal::WalManager> lifetime = manager;
    WalPageDurabilityAdapter adapter{manager};
    const auto syncs_before = manager->successful_sync_count();
    manager.reset();
    EXPECT_FALSE(lifetime.expired());
    ASSERT_TRUE(adapter.flush_through(first.end_lsn));
    auto retained = lifetime.lock();
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained->successful_sync_count(), syncs_before);
}

TEST(WalPageDurabilityAdapterTest, BufferPoolFlushesWalBeforeWritingDataPage) {
    auto root = test_support::TestDirectory::create("recovery-wal-durability-buffer-pool");
    ASSERT_TRUE(root);
    auto opened = wal::WalManager::open(root->join("wal"), wal_options(true));
    ASSERT_TRUE(opened);
    auto manager = std::make_shared<wal::WalManager>(std::move(opened).value());
    const auto inserted = manager->insert(record(1U));
    ASSERT_TRUE(inserted);

    auto disk = storage::FileDiskManager::open(root->join("relations"));
    ASSERT_TRUE(disk);
    auto pool = storage::BufferPool::create(
        2U,
        std::move(disk).value(),
        nullptr,
        std::make_unique<WalPageDurabilityAdapter>(manager)
    );
    ASSERT_TRUE(pool);
    storage::PageAddress address{base::kInvalidRelationId, base::kInvalidPageId};
    {
        auto allocated =
            pool.value()->allocate_page(base::RelationId{19U}, storage::PageKind::kHeap);
        ASSERT_TRUE(allocated);
        ASSERT_TRUE(allocated->address().has_value());
        address = *allocated->address();
    }
    {
        auto page = pool.value()->write_page(address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->page()->set_lsn(inserted->end_lsn));
    }
    ASSERT_TRUE(pool.value()->flush_page(address));
    EXPECT_GE(manager->flushed_lsn().value(), inserted->end_lsn.value());
    ASSERT_TRUE(pool.value()->shutdown());
}

}  // namespace
}  // namespace db::recovery
