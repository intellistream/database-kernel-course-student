#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_key.hpp"
#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/page.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "btree_test_log.hpp"

namespace db::access {
namespace {

struct AllocationFailureState final {
    void fail_on(const std::size_t invocation) noexcept {
        fail_on_invocation = invocation;
        observed = 0U;
    }

    std::optional<std::size_t> fail_on_invocation;
    std::size_t observed{0U};
};

class AllocationFailDisk final : public storage::DiskManager {
public:
    AllocationFailDisk(
        std::unique_ptr<storage::FileDiskManager> inner,
        std::shared_ptr<AllocationFailureState> state
    ) noexcept
        : inner_{std::move(inner)},
          state_{std::move(state)} {}

    auto read_page(const storage::PageAddress address) -> base::Result<storage::Page> override {
        return inner_->read_page(address);
    }

    auto write_page(const storage::PageAddress address, const storage::Page& page)
        -> base::Status override {
        return inner_->write_page(address, page);
    }

    auto allocate_page(
        const base::RelationId relation_id,
        const storage::PageKind kind,
        const std::size_t special_size
    ) -> base::Result<storage::PageAddress> override {
        ++state_->observed;
        if (state_->fail_on_invocation.has_value()
            && state_->observed == state_->fail_on_invocation.value()) {
            state_->fail_on_invocation.reset();
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "injected B-tree bulk-build page allocation failure",
                }
            );
        }
        return inner_->allocate_page(relation_id, kind, special_size);
    }

    auto page_count(const base::RelationId relation_id) -> base::Result<std::uint64_t> override {
        return inner_->page_count(relation_id);
    }

    auto sync_relation(const base::RelationId relation_id) -> base::Status override {
        return inner_->sync_relation(relation_id);
    }

    auto sync_all() -> base::Status override {
        return inner_->sync_all();
    }

    auto remove_relation(const base::RelationId relation_id, const bool missing_ok)
        -> base::Status override {
        return inner_->remove_relation(relation_id, missing_ok);
    }

private:
    std::unique_ptr<storage::FileDiskManager> inner_;
    std::shared_ptr<AllocationFailureState> state_;
};

struct BuiltTree final {
    std::unique_ptr<storage::BufferPool> pool;
    BtreeRelation relation;
};

auto make_tree(
    const test_support::TestDirectory& directory,
    const base::RelationId relation_id,
    const bool unique,
    const std::shared_ptr<AllocationFailureState>& failure_state = nullptr,
    const std::vector<types::DataType>& key_types = std::vector<types::DataType>{types::kInt64Type}
) -> base::Result<BuiltTree> {
    auto file_disk = storage::FileDiskManager::open(directory.join("relations"));
    if (!file_disk) {
        return base::fail(std::move(file_disk).error());
    }
    std::unique_ptr<storage::DiskManager> disk;
    if (failure_state == nullptr) {
        disk = std::move(file_disk).value();
    } else {
        disk = std::make_unique<AllocationFailDisk>(std::move(file_disk).value(), failure_state);
    }
    auto pool =
        storage::BufferPool::create(16U, std::move(disk), nullptr, test::btree_durability());
    if (!pool) {
        return base::fail(std::move(pool).error());
    }
    auto layout = BtreeKeyLayout::create(key_types);
    if (!layout) {
        return base::fail(std::move(layout).error());
    }
    auto relation = BtreeRelation::create(
        relation_id,
        *pool.value(),
        layout.value(),
        unique,
        test::btree_log()
    );
    if (!relation) {
        return base::fail(std::move(relation).error());
    }
    return BuiltTree{
        .pool = std::move(pool).value(),
        .relation = std::move(relation).value(),
    };
}

auto make_integer_entries(const BtreeRelation& relation, const std::size_t count)
    -> base::Result<std::vector<BtreeBulkBuildEntry>> {
    std::vector<BtreeBulkBuildEntry> entries;
    entries.reserve(count);
    for (std::size_t offset = 0U; offset < count; ++offset) {
        const auto key = static_cast<std::int64_t>(count - offset - 1U);
        const std::vector values{types::Value::integer(key)};
        auto entry = relation.make_bulk_build_entry(
            values,
            base::Rid{
                base::PageId{static_cast<std::uint32_t>(key)},
                base::SlotId{static_cast<std::uint16_t>(key % 127)},
            }
        );
        if (!entry) {
            return base::fail(std::move(entry).error());
        }
        entries.push_back(std::move(entry).value());
    }
    return entries;
}

auto collect(BtreeRelation& relation, storage::BufferPool& pool)
    -> base::Result<std::vector<base::Rid>> {
    auto scan = BtreeScan::for_full(relation, {});
    if (!scan) {
        return base::fail(std::move(scan).error());
    }
    std::vector<base::Rid> result;
    for (;;) {
        auto next = scan->next(pool);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next->has_value()) {
            return result;
        }
        result.push_back(next->value());
    }
}

TEST(BtreeBulkBuildTest, BuildsSortedMultilevelTreeBottomUp) {
    auto directory = test_support::TestDirectory::create("btree-bulk-multilevel");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(directory.value(), base::RelationId{650}, false);
    ASSERT_TRUE(tree) << tree.error().to_string();
    constexpr std::size_t entry_count = 7'500U;
    auto entries = make_integer_entries(tree->relation, entry_count);
    ASSERT_TRUE(entries) << entries.error().to_string();

    auto built = tree->relation.bulk_build(*tree->pool, entries.value());
    ASSERT_TRUE(built) << built.error().to_string();
    ASSERT_TRUE(built->root.has_value());
    EXPECT_GT(built->root->level, 0U);
    EXPECT_EQ(built->entries, entry_count);
    EXPECT_GT(built->leaf_pages, 32U);
    EXPECT_GT(built->internal_pages, 0U);

    auto rows = collect(tree->relation, *tree->pool);
    ASSERT_TRUE(rows) << rows.error().to_string();
    ASSERT_EQ(rows->size(), entry_count);
    for (std::size_t index = 0U; index < rows->size(); ++index) {
        EXPECT_EQ(rows->at(index).page_id(), base::PageId{static_cast<std::uint32_t>(index)});
    }

    auto second = tree->relation.bulk_build(*tree->pool, {});
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code(), base::ErrorCode::kAlreadyExists);
}

TEST(BtreeBulkBuildTest, AcceptsAlreadySortedEntriesWithRidTieBreaks) {
    auto directory = test_support::TestDirectory::create("btree-bulk-sorted-input");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(directory.value(), base::RelationId{655}, false);
    ASSERT_TRUE(tree) << tree.error().to_string();

    std::vector<BtreeBulkBuildEntry> entries;
    for (const auto& [value, rid] : std::vector<std::pair<std::int64_t, base::Rid>>{
             {1, base::Rid{base::PageId{1}, base::SlotId{1}}},
             {1, base::Rid{base::PageId{1}, base::SlotId{2}}},
             {2, base::Rid{base::PageId{2}, base::SlotId{1}}},
         }) {
        const std::vector values{types::Value::integer(value)};
        auto entry = tree->relation.make_bulk_build_entry(values, rid);
        ASSERT_TRUE(entry) << entry.error().to_string();
        entries.push_back(std::move(entry).value());
    }

    auto built = tree->relation.bulk_build(*tree->pool, entries);
    ASSERT_TRUE(built) << built.error().to_string();
    auto rows = collect(tree->relation, *tree->pool);
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<base::Rid>{
            base::Rid{base::PageId{1}, base::SlotId{1}},
            base::Rid{base::PageId{1}, base::SlotId{2}},
            base::Rid{base::PageId{2}, base::SlotId{1}},
        })
    );
}

TEST(BtreeBulkBuildTest, StreamsProjectedKeysThroughOneRelationBoundArena) {
    auto directory = test_support::TestDirectory::create("btree-bulk-compact-input");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(directory.value(), base::RelationId{656}, false);
    ASSERT_TRUE(tree) << tree.error().to_string();
    auto buffer = tree->relation.make_bulk_build_buffer();
    ASSERT_TRUE(buffer) << buffer.error().to_string();

    const std::vector<std::size_t> positions{1U};
    for (const auto value : {3, 1, 2}) {
        const std::vector row{types::Value::boolean(true), types::Value::integer(value)};
        ASSERT_TRUE(
            tree->relation.append_projected_canonical_bulk_build_key(buffer.value(), row, positions)
        );
    }
    EXPECT_EQ(buffer->size(), 3U);
    const std::vector rids{
        base::Rid{base::PageId{3}, base::SlotId{1}},
        base::Rid{base::PageId{1}, base::SlotId{1}},
        base::Rid{base::PageId{2}, base::SlotId{1}},
    };
    ASSERT_TRUE(tree->relation.attach_bulk_build_rids(buffer.value(), rids));
    auto built = tree->relation.bulk_build(*tree->pool, std::move(buffer).value());
    ASSERT_TRUE(built) << built.error().to_string();

    auto rows = collect(tree->relation, *tree->pool);
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(rows.value(), (std::vector<base::Rid>{rids[1U], rids[2U], rids[0U]}));
}

TEST(BtreeBulkBuildTest, SortsCharacterKeysByPayloadRatherThanLength) {
    auto directory = test_support::TestDirectory::create("btree-bulk-character-order");
    ASSERT_TRUE(directory) << directory.error().to_string();
    constexpr types::TypeModifier modifier{8};
    auto tree = make_tree(
        directory.value(),
        base::RelationId{653},
        false,
        nullptr,
        std::vector<types::DataType>{types::DataType{types::kCharTypeId, modifier}}
    );
    ASSERT_TRUE(tree) << tree.error().to_string();

    std::vector<BtreeBulkBuildEntry> entries;
    for (const auto& [value, rid] : std::vector<std::pair<std::string, base::Rid>>{
             {"b", base::Rid{base::PageId{3}, base::SlotId{1}}},
             {"aa", base::Rid{base::PageId{2}, base::SlotId{1}}},
             {"a", base::Rid{base::PageId{1}, base::SlotId{1}}},
         }) {
        const std::vector values{types::Value::character(value, modifier)};
        auto entry = tree->relation.make_bulk_build_entry(values, rid);
        ASSERT_TRUE(entry) << entry.error().to_string();
        entries.push_back(std::move(entry).value());
    }
    ASSERT_TRUE(tree->relation.bulk_build(*tree->pool, entries));

    auto rows = collect(tree->relation, *tree->pool);
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<base::Rid>{
            base::Rid{base::PageId{1}, base::SlotId{1}},
            base::Rid{base::PageId{2}, base::SlotId{1}},
            base::Rid{base::PageId{3}, base::SlotId{1}},
        })
    );
}

TEST(BtreeBulkBuildTest, SortsNullableFixedWidthCompositeKeysBytewise) {
    auto directory = test_support::TestDirectory::create("btree-bulk-fixed-width-composite");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(
        directory.value(),
        base::RelationId{654},
        false,
        nullptr,
        std::vector<types::DataType>{types::kInt64Type, types::kBoolType}
    );
    ASSERT_TRUE(tree) << tree.error().to_string();

    const auto null_integer = types::Value::null(types::kInt64Type);
    std::vector<BtreeBulkBuildEntry> entries;
    const auto append_entry = [&entries,
                               &tree](std::vector<types::Value> values, const base::Rid heap_rid) {
        auto entry = tree->relation.make_bulk_build_entry(values, heap_rid);
        ASSERT_TRUE(entry) << entry.error().to_string();
        entries.push_back(std::move(entry).value());
    };
    append_entry(
        {types::Value::integer(1), types::Value::boolean(false)},
        base::Rid{base::PageId{25}, base::SlotId{0}}
    );
    append_entry(
        {types::Value::integer(-2), types::Value::boolean(true)},
        base::Rid{base::PageId{24}, base::SlotId{0}}
    );
    append_entry(
        {null_integer, types::Value::boolean(true)},
        base::Rid{base::PageId{21}, base::SlotId{0}}
    );
    append_entry(
        {types::Value::integer(-2), types::Value::null(types::kBoolType)},
        base::Rid{base::PageId{22}, base::SlotId{0}}
    );
    append_entry(
        {types::Value::integer(-2), types::Value::boolean(false)},
        base::Rid{base::PageId{23}, base::SlotId{0}}
    );

    ASSERT_TRUE(tree->relation.bulk_build(*tree->pool, entries));
    auto rows = collect(tree->relation, *tree->pool);
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<base::Rid>{
            {base::PageId{21}, base::SlotId{0}},
            {base::PageId{22}, base::SlotId{0}},
            {base::PageId{23}, base::SlotId{0}},
            {base::PageId{24}, base::SlotId{0}},
            {base::PageId{25}, base::SlotId{0}},
        })
    );
}

TEST(BtreeBulkBuildTest, UniquePreflightRejectsDuplicatesAndAllowsNulls) {
    auto directory = test_support::TestDirectory::create("btree-bulk-unique");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(directory.value(), base::RelationId{651}, true);
    ASSERT_TRUE(tree) << tree.error().to_string();

    const std::vector one{types::Value::integer(1)};
    auto first =
        tree->relation.make_bulk_build_entry(one, base::Rid{base::PageId{2}, base::SlotId{1}});
    auto second =
        tree->relation.make_bulk_build_entry(one, base::Rid{base::PageId{1}, base::SlotId{1}});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    std::vector duplicates{std::move(first).value(), std::move(second).value()};
    auto rejected = tree->relation.bulk_build(*tree->pool, duplicates);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_EQ(tree->pool->relation_page_count(tree->relation.relation_id()).value(), 1U);
    EXPECT_FALSE(tree->relation.root(*tree->pool)->has_value());

    const std::vector null_value{types::Value::null(types::kInt64Type)};
    auto null_second = tree->relation.make_bulk_build_entry(
        null_value,
        base::Rid{base::PageId{2}, base::SlotId{1}}
    );
    const std::vector projected_source{types::Value::integer(9), types::Value::integer(1)};
    const std::vector<std::size_t> projected_positions{1U};
    auto nonnull = tree->relation.make_projected_bulk_build_entry(
        projected_source,
        projected_positions,
        base::Rid{base::PageId{3}, base::SlotId{1}}
    );
    auto null_first = tree->relation.make_bulk_build_entry(
        null_value,
        base::Rid{base::PageId{1}, base::SlotId{1}}
    );
    ASSERT_TRUE(null_second);
    ASSERT_TRUE(nonnull);
    ASSERT_TRUE(null_first);
    std::vector allowed{
        std::move(null_second).value(),
        std::move(nonnull).value(),
        std::move(null_first).value(),
    };
    auto built = tree->relation.bulk_build(*tree->pool, allowed);
    ASSERT_TRUE(built) << built.error().to_string();
    auto rows = collect(tree->relation, *tree->pool);
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<base::Rid>{
            base::Rid{base::PageId{1}, base::SlotId{1}},
            base::Rid{base::PageId{2}, base::SlotId{1}},
            base::Rid{base::PageId{3}, base::SlotId{1}},
        })
    );
}

TEST(BtreeBulkBuildTest, AllocationFailureLeavesRootUnpublishedAndRetryable) {
    auto directory = test_support::TestDirectory::create("btree-bulk-failure");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto failure = std::make_shared<AllocationFailureState>();
    auto tree = make_tree(directory.value(), base::RelationId{652}, false, failure);
    ASSERT_TRUE(tree) << tree.error().to_string();
    auto entries = make_integer_entries(tree->relation, 1'500U);
    ASSERT_TRUE(entries) << entries.error().to_string();

    failure->fail_on(3U);
    auto failed = tree->relation.bulk_build(*tree->pool, entries.value());
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    auto rootless = tree->relation.root(*tree->pool);
    ASSERT_TRUE(rootless) << rootless.error().to_string();
    EXPECT_FALSE(rootless->has_value());
    EXPECT_GT(tree->pool->relation_page_count(tree->relation.relation_id()).value(), 1U);

    auto retry_entries = make_integer_entries(tree->relation, 1'500U);
    ASSERT_TRUE(retry_entries) << retry_entries.error().to_string();
    auto retried = tree->relation.bulk_build(*tree->pool, retry_entries.value());
    ASSERT_TRUE(retried) << retried.error().to_string();
    EXPECT_TRUE(retried->root.has_value());
    auto rows = collect(tree->relation, *tree->pool);
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(rows->size(), 1'500U);
}

}  // namespace
}  // namespace db::access
