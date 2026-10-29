#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_maintenance.hpp"
#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/access/dead_tid_store.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/recovery/btree_startup_redo.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/recovery/wal_durability.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

constexpr std::uint64_t kBtreeIntegrationSystemIdentifier = 0x4254'5245'4557'414cULL;

struct RelationImage final {
    base::RelationId relation_id{base::kInvalidRelationId};
    std::vector<storage::Page::Storage> pages;
};

struct ReplayRelation final {
    base::RelationId relation_id{base::kInvalidRelationId};
    std::vector<storage::Page> pages;
};

auto page_key(const storage::PageAddress address) noexcept -> std::uint64_t {
    return (static_cast<std::uint64_t>(address.relation_id.value()) << 32U)
           | address.page_id.value();
}

// Test-only page port. It makes the startup access contract observable without
// preempting the production bounded cache implementation.
class MemoryStartupPageAccess final : public StartupPageAccessPort {
public:
    class Lease final : public StartupPageLease {
    public:
        Lease(
            MemoryStartupPageAccess& owner,
            storage::Page& page,
            const storage::PageAddress address
        ) noexcept
            : owner_{owner},
              page_{page},
              address_{address} {}

        auto page() noexcept -> storage::Page& override {
            return page_;
        }

        auto mark_dirty() -> base::Status override {
            if (!dirty_) {
                owner_.dirty_pages_.push_back(address_);
                dirty_ = true;
            }
            return {};
        }

    private:
        MemoryStartupPageAccess& owner_;
        storage::Page& page_;
        storage::PageAddress address_;
        bool dirty_{false};
    };

    auto acquire(const StartupPageRequest request)
        -> base::Result<std::unique_ptr<StartupPageLease>> override {
        requests_.push_back(request);
        auto found = pages_.find(page_key(request.address));
        if (found == pages_.end() && request.load_mode == StartupPageLoadMode::kReadExisting) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kNotFound,
                    "test startup page access cannot read a missing page",
                }
            );
        }
        if (found == pages_.end()) {
            auto page = storage::Page::create(request.expected_kind, request.address);
            if (!page) {
                return base::fail(std::move(page).error());
            }
            found = pages_.emplace(page_key(request.address), std::move(page).value()).first;
        }
        return std::unique_ptr<StartupPageLease>{
            std::make_unique<Lease>(*this, found->second, request.address),
        };
    }

    auto find(const storage::PageAddress address) const -> const storage::Page* {
        const auto found = pages_.find(page_key(address));
        return found == pages_.end() ? nullptr : &found->second;
    }

    auto requests() const noexcept -> const std::vector<StartupPageRequest>& {
        return requests_;
    }

    auto dirty_pages() const noexcept -> const std::vector<storage::PageAddress>& {
        return dirty_pages_;
    }

private:
    std::map<std::uint64_t, storage::Page> pages_;
    std::vector<StartupPageRequest> requests_;
    std::vector<storage::PageAddress> dirty_pages_;
};

auto manager_options() -> wal::WalManagerOptions {
    return wal::WalManagerOptions{
        .system_identifier = kBtreeIntegrationSystemIdentifier,
        .timeline = 1,
        .ring_page_count = 4,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = true,
        .storage_fault_port = nullptr,
    };
}

auto read_records(const std::filesystem::path& wal_directory)
    -> std::vector<wal::DecodedWalRecord> {
    auto reader = wal::WalReader::open(
        wal_directory,
        wal::WalReaderOptions{
            .system_identifier = kBtreeIntegrationSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    EXPECT_TRUE(reader) << reader.error().to_string();
    std::vector<wal::DecodedWalRecord> records;
    for (;;) {
        auto next = reader->read_next();
        EXPECT_TRUE(next) << next.error().to_string();
        if (!next || !next->has_value()) {
            return records;
        }
        records.push_back(std::move(next->value()));
    }
}

auto snapshot_relation(storage::BufferPool& pool, const base::RelationId relation_id)
    -> RelationImage {
    auto count = pool.relation_page_count(relation_id);
    EXPECT_TRUE(count) << count.error().to_string();
    RelationImage image{.relation_id = relation_id, .pages = {}};
    if (!count) {
        return image;
    }
    image.pages.reserve(count.value());
    for (std::uint32_t page_id = 0U; page_id < count.value(); ++page_id) {
        auto read = pool.read_page(storage::PageAddress{relation_id, base::PageId{page_id}});
        EXPECT_TRUE(read) << read.error().to_string();
        if (!read) {
            return image;
        }
        auto normalized = *read->page();
        EXPECT_TRUE(normalized.set_lsn(normalized.lsn()));
        image.pages.push_back(normalized.bytes());
    }
    return image;
}

auto make_replay_relation(const RelationImage& expected) -> ReplayRelation {
    ReplayRelation replayed{.relation_id = expected.relation_id, .pages = {}};
    replayed.pages.reserve(expected.pages.size());
    for (std::uint32_t page_id = 0U; page_id < expected.pages.size(); ++page_id) {
        auto page = storage::Page::create(
            page_id == 0U ? storage::PageKind::kMetadata : storage::PageKind::kBtree,
            storage::PageAddress{expected.relation_id, base::PageId{page_id}}
        );
        EXPECT_TRUE(page) << page.error().to_string();
        if (!page) {
            return replayed;
        }
        replayed.pages.push_back(std::move(page).value());
    }
    return replayed;
}

auto make_replay_relations(const std::vector<RelationImage>& expected)
    -> std::vector<ReplayRelation> {
    std::vector<ReplayRelation> replayed;
    replayed.reserve(expected.size());
    for (const auto& relation : expected) {
        replayed.push_back(make_replay_relation(relation));
    }
    return replayed;
}

auto find_relation(std::vector<ReplayRelation>& relations, const base::RelationId relation_id)
    -> ReplayRelation* {
    for (auto& relation : relations) {
        if (relation.relation_id == relation_id) {
            return &relation;
        }
    }
    return nullptr;
}

void apply_all(
    const std::vector<wal::DecodedWalRecord>& records,
    std::vector<ReplayRelation>& replayed
) {
    for (const auto& record : records) {
        ASSERT_EQ(record.record.resource_manager, wal::WalResourceManager::kBtree);
        for (std::size_t block_index = 0U; block_index < record.record.blocks.size();
             ++block_index) {
            const auto& block = record.record.blocks[block_index];
            auto* relation = find_relation(replayed, block.relation_id);
            ASSERT_NE(relation, nullptr);
            const auto page_id = static_cast<std::size_t>(block.page_id.value());
            ASSERT_LT(page_id, relation->pages.size());
            auto applied = apply_btree_redo(record, block_index, relation->pages[page_id]);
            ASSERT_TRUE(applied) << "record info " << static_cast<unsigned>(record.record.info)
                                 << " block " << block_index << " relation "
                                 << block.relation_id.value() << " page " << block.page_id.value()
                                 << ": " << applied.error().to_string();
        }
    }
}

void expect_exact_pages(
    const std::vector<RelationImage>& expected,
    const std::vector<ReplayRelation>& replayed
) {
    ASSERT_EQ(replayed.size(), expected.size());
    for (std::size_t relation_index = 0U; relation_index < expected.size(); ++relation_index) {
        ASSERT_EQ(replayed[relation_index].relation_id, expected[relation_index].relation_id);
        ASSERT_EQ(replayed[relation_index].pages.size(), expected[relation_index].pages.size());
        for (std::size_t page_id = 0U; page_id < expected[relation_index].pages.size(); ++page_id) {
            EXPECT_EQ(
                replayed[relation_index].pages[page_id].bytes(),
                expected[relation_index].pages[page_id]
            ) << "relation "
              << expected[relation_index].relation_id.value() << " page " << page_id;
        }
    }
}

auto insert_integer(
    const access::BtreeRelation& relation,
    storage::BufferPool& pool,
    const std::int64_t key,
    const base::Rid rid
) -> base::Status {
    const std::array values{types::Value::integer(key)};
    return relation.insert(pool, values, rid);
}

auto wide_key(const std::size_t value) -> std::string {
    return std::string(1U, static_cast<char>('a' + static_cast<char>(value)))
           + std::string(999U, 'x');
}

auto wide_key_before_first(const std::size_t value) -> std::string {
    return std::string{"a"} + std::string(1U, static_cast<char>('0' + static_cast<char>(value)))
           + std::string(998U, 'x');
}

auto find_integer(
    const access::BtreeRelation& relation,
    storage::BufferPool& pool,
    const std::int64_t key
) -> base::Result<access::BtreeScanCandidate> {
    const std::array values{types::Value::integer(key)};
    auto scan = access::BtreeScan::for_eq(relation, values, {});
    if (!scan) {
        return base::fail(std::move(scan).error());
    }
    auto candidate = scan->next_candidate(pool);
    if (!candidate) {
        return base::fail(std::move(candidate).error());
    }
    if (!candidate->has_value()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kNotFound,
                "B-tree integration key was not found",
            }
        );
    }
    return candidate->value();
}

TEST(BtreeWalIntegrationTest, ReplaysEveryMutationFamilyToExactFinalPages) {
    auto root = test_support::TestDirectory::create("btree-wal-integration");
    ASSERT_TRUE(root) << root.error().to_string();
    auto manager = wal::WalManager::open(root->join("wal"), manager_options());
    ASSERT_TRUE(manager) << manager.error().to_string();
    auto disk = storage::FileDiskManager::open(root->join("relations"));
    ASSERT_TRUE(disk) << disk.error().to_string();
    auto pool = storage::BufferPool::create(
        8U,
        std::move(disk).value(),
        nullptr,
        std::make_unique<WalPageDurabilityAdapter>(manager.value())
    );
    ASSERT_TRUE(pool) << pool.error().to_string();
    auto layout = access::BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    BtreeMutationWalAdapter adapter{manager.value()};

    constexpr auto inserted_relation_id = base::RelationId{711};
    auto inserted =
        access::BtreeRelation::create(inserted_relation_id, **pool, layout.value(), false, adapter);
    ASSERT_TRUE(inserted) << inserted.error().to_string();
    // Keep enough tuples to force a non-root leaf split after the initial root
    // split even when the compact page layout raises leaf capacity.
    constexpr std::int64_t inserted_key_count = 700;
    for (std::int64_t key = 0; key < inserted_key_count; ++key) {
        ASSERT_TRUE(insert_integer(
            inserted.value(),
            **pool,
            key,
            base::Rid{base::PageId{static_cast<std::uint32_t>(key + 1'000)}, base::SlotId{1}}
        ));
    }

    auto cleanup_candidate = find_integer(inserted.value(), **pool, 50);
    ASSERT_TRUE(cleanup_candidate) << cleanup_candidate.error().to_string();
    const std::array cleanup_key{types::Value::integer(50)};
    auto cleanup = access::BtreeCleanupBatch::for_equal(inserted.value(), cleanup_key);
    ASSERT_TRUE(cleanup) << cleanup.error().to_string();
    ASSERT_TRUE(cleanup->push_dead(cleanup_candidate.value()));
    auto cleaned = cleanup->flush(**pool);
    ASSERT_TRUE(cleaned) << cleaned.error().to_string();
    ASSERT_EQ(cleaned->rids_removed, 1U);

    auto vacuum_candidate = find_integer(inserted.value(), **pool, 51);
    ASSERT_TRUE(vacuum_candidate) << vacuum_candidate.error().to_string();
    access::DeadTidStore dead_tids;
    ASSERT_TRUE(dead_tids.insert(vacuum_candidate->rid));
    auto vacuumed = inserted->bulk_delete(**pool, dead_tids);
    ASSERT_TRUE(vacuumed) << vacuumed.error().to_string();
    ASSERT_EQ(vacuumed->rids_removed, 1U);

    constexpr auto cascading_relation_id = base::RelationId{713};
    constexpr types::TypeModifier cascading_modifier{1'000};
    auto cascading_layout =
        access::BtreeKeyLayout::create({types::DataType{types::kCharTypeId, cascading_modifier}});
    ASSERT_TRUE(cascading_layout) << cascading_layout.error().to_string();
    auto cascading = access::BtreeRelation::create(
        cascading_relation_id,
        **pool,
        cascading_layout.value(),
        false,
        adapter
    );
    ASSERT_TRUE(cascading) << cascading.error().to_string();
    for (std::size_t value = 0U; value < 18U; ++value) {
        const std::array values{
            types::Value::character(wide_key(value), cascading_modifier),
        };
        ASSERT_TRUE(cascading->insert(
            **pool,
            values,
            base::Rid{base::PageId{static_cast<std::uint32_t>(3'000U + value)}, base::SlotId{1}}
        ));
    }
    for (std::size_t value = 0U; value < 10U; ++value) {
        const std::array values{
            types::Value::character(wide_key_before_first(value), cascading_modifier),
        };
        ASSERT_TRUE(cascading->insert(
            **pool,
            values,
            base::Rid{base::PageId{static_cast<std::uint32_t>(3'100U + value)}, base::SlotId{1}}
        ));
    }
    auto cascading_root = cascading->root(**pool);
    ASSERT_TRUE(cascading_root) << cascading_root.error().to_string();
    ASSERT_TRUE(cascading_root->has_value());
    ASSERT_GE(cascading_root->value().level, 2U);

    constexpr auto built_relation_id = base::RelationId{712};
    auto built =
        access::BtreeRelation::create(built_relation_id, **pool, layout.value(), false, adapter);
    ASSERT_TRUE(built) << built.error().to_string();
    std::vector<access::BtreeBulkBuildEntry> entries;
    entries.reserve(250U);
    for (std::int64_t key = 1'000; key < 1'250; ++key) {
        const std::array values{types::Value::integer(key)};
        auto entry = built->make_bulk_build_entry(
            values,
            base::Rid{base::PageId{static_cast<std::uint32_t>(key + 1'000)}, base::SlotId{2}}
        );
        ASSERT_TRUE(entry) << entry.error().to_string();
        entries.push_back(std::move(entry).value());
    }
    auto built_stats = built->bulk_build(**pool, entries);
    ASSERT_TRUE(built_stats) << built_stats.error().to_string();
    ASSERT_GT(built_stats->leaf_pages, 1U);

    const std::vector<RelationImage> expected{
        snapshot_relation(**pool, inserted_relation_id),
        snapshot_relation(**pool, cascading_relation_id),
        snapshot_relation(**pool, built_relation_id),
    };
    const auto end_lsn = manager->end_lsn();
    ASSERT_NE(end_lsn, base::kInvalidLsn);
    ASSERT_TRUE((*pool)->shutdown());
    ASSERT_TRUE(manager->flush_through(end_lsn));
    ASSERT_TRUE(manager->shutdown());

    const auto records = read_records(root->join("wal"));
    ASSERT_FALSE(records.empty());
    std::set<std::uint8_t> operations;
    std::size_t initialized_build_pages = 0U;
    std::size_t cascading_root_records = 0U;
    std::size_t split_finish_records = 0U;
    for (const auto& record : records) {
        operations.insert(record.record.info);
        for (const auto& block : record.record.blocks) {
            if (!block.will_initialize) {
                continue;
            }
            EXPECT_TRUE(block.full_page_image.has_value());
            EXPECT_TRUE(block.delta.empty());
            if (record.record.info == wal::opcode::btree::kBuildPage) {
                ++initialized_build_pages;
            }
        }
        if (record.record.info == wal::opcode::btree::kNewRoot
            && record.record.blocks.size() == 5U) {
            EXPECT_FALSE(record.record.blocks[0U].will_initialize);
            EXPECT_TRUE(record.record.blocks[1U].will_initialize);
            EXPECT_TRUE(record.record.blocks[2U].will_initialize);
            EXPECT_FALSE(record.record.blocks[3U].will_initialize);
            EXPECT_FALSE(record.record.blocks[4U].will_initialize);
            ++cascading_root_records;
        }
        if (record.record.info == wal::opcode::btree::kSplitFinish) {
            ASSERT_TRUE(record.record.blocks.size() == 3U || record.record.blocks.size() == 4U);
            EXPECT_TRUE(record.record.blocks[1U].will_initialize);
            EXPECT_FALSE(record.record.blocks.back().will_initialize);
            ++split_finish_records;
        }
    }
    EXPECT_GT(initialized_build_pages, 0U);
    EXPECT_GT(cascading_root_records, 0U);
    EXPECT_GT(split_finish_records, 0U);
    const std::set<std::uint8_t> expected_operations{
        wal::opcode::btree::kMetadata,
        wal::opcode::btree::kInsertLeaf,
        wal::opcode::btree::kSplit,
        wal::opcode::btree::kDelete,
        wal::opcode::btree::kNewRoot,
        wal::opcode::btree::kVacuum,
        wal::opcode::btree::kBuildPage,
        wal::opcode::btree::kFinishSplit,
        wal::opcode::btree::kSplitFinish,
    };
    EXPECT_EQ(operations, expected_operations);

    auto replayed = make_replay_relations(expected);
    apply_all(records, replayed);
    expect_exact_pages(expected, replayed);

    apply_all(records, replayed);
    expect_exact_pages(expected, replayed);

    MemoryStartupPageAccess startup_pages;
    std::size_t expected_blocks = 0U;
    for (const auto& record : records) {
        expected_blocks += record.record.blocks.size();
        auto applied = replay_btree_startup_redo(record, startup_pages);
        ASSERT_TRUE(applied) << applied.error().to_string();
        EXPECT_EQ(applied->applied_blocks, record.record.blocks.size());
        EXPECT_EQ(applied->skipped_blocks, 0U);
    }
    ASSERT_EQ(startup_pages.requests().size(), expected_blocks);
    ASSERT_EQ(startup_pages.dirty_pages().size(), expected_blocks);
    std::size_t request_index = 0U;
    for (const auto& record : records) {
        for (const auto& block : record.record.blocks) {
            const auto& request = startup_pages.requests()[request_index++];
            const storage::PageAddress expected_address{block.relation_id, block.page_id};
            EXPECT_EQ(request.address, expected_address);
            EXPECT_EQ(
                request.expected_kind,
                block.standard_page ? storage::PageKind::kBtree : storage::PageKind::kMetadata
            );
            EXPECT_EQ(
                request.load_mode,
                block.full_page_image.has_value() || block.will_initialize
                    ? StartupPageLoadMode::kSkipExisting
                    : StartupPageLoadMode::kReadExisting
            );
        }
    }
    for (const auto& relation : expected) {
        for (std::size_t page_id = 0U; page_id < relation.pages.size(); ++page_id) {
            const auto* page = startup_pages.find(
                storage::PageAddress{
                    relation.relation_id,
                    base::PageId{static_cast<std::uint32_t>(page_id)},
                }
            );
            ASSERT_NE(page, nullptr);
            EXPECT_EQ(page->bytes(), relation.pages[page_id]);
        }
    }
}

TEST(BtreeStartupRedoTest, RejectsANonBtreeRouteBeforePageAcquisition) {
    const wal::DecodedWalRecord record{
        .start_lsn = base::Lsn{1'000U},
        .end_lsn = base::Lsn{1'040U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{11},
            .resource_manager = wal::WalResourceManager::kTransaction,
            .info = wal::opcode::transaction::kCommit,
            .blocks = {},
            .main_payload = {},
        },
    };

    MemoryStartupPageAccess pages;
    auto rejected = replay_btree_startup_redo(record, pages);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_TRUE(pages.requests().empty());
}

}  // namespace
}  // namespace db::recovery
