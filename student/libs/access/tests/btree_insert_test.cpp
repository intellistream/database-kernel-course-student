#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_page.hpp"
#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/test_support/test_directory.hpp"

#include "btree_test_log.hpp"

namespace db::access {
namespace {

class BlockingLeafLog final : public BtreeMutationLogPort {
public:
    auto log(const BtreeWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.pages.empty()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "blocking B-tree log received an invalid mutation",
                }
            );
        }
        if (armed_.load(std::memory_order_acquire)
            && mutation.operation == BtreeWalOperation::kInsertLeaf
            && !claimed_.exchange(true, std::memory_order_acq_rel)) {
            std::unique_lock lock{mutex_};
            blocked_ = true;
            changed_.notify_all();
            changed_.wait(lock, [this] {
                return released_;
            });
        }
        return base::Lsn{next_lsn_.fetch_add(1U, std::memory_order_relaxed)};
    }

    void arm() noexcept {
        armed_.store(true, std::memory_order_release);
    }

    void wait_until_blocked() {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] {
            return blocked_;
        });
    }

    void release() {
        const std::lock_guard lock{mutex_};
        released_ = true;
        changed_.notify_all();
    }

private:
    std::atomic<std::uint64_t> next_lsn_{1U};
    std::atomic<bool> armed_{false};
    std::atomic<bool> claimed_{false};
    std::mutex mutex_;
    std::condition_variable changed_;
    bool blocked_{false};
    bool released_{false};
};

class FailNextLeafLog final : public BtreeMutationLogPort {
public:
    auto log(const BtreeWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.pages.empty()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "failing B-tree log received an invalid mutation",
                }
            );
        }
        if (mutation.operation == BtreeWalOperation::kInsertLeaf
            && fail_leaf_.exchange(false, std::memory_order_acq_rel)) {
            return base::fail(base::Error{base::ErrorCode::kIo, "injected leaf WAL failure"});
        }
        return base::Lsn{next_lsn_.fetch_add(1U, std::memory_order_relaxed)};
    }

    void fail_next_leaf() noexcept {
        fail_leaf_.store(true, std::memory_order_release);
    }

private:
    std::atomic<std::uint64_t> next_lsn_{1U};
    std::atomic<bool> fail_leaf_{false};
};

class FailFirstFinishSplitLog final : public BtreeMutationLogPort {
public:
    auto log(const BtreeWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.pages.empty()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "failing split-finish log received an invalid mutation",
                }
            );
        }
        if (mutation.operation == BtreeWalOperation::kFinishSplit
            && !failed_.exchange(true, std::memory_order_acq_rel)) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "injected split-finish WAL failure",
                }
            );
        }
        return base::Lsn{next_lsn_.fetch_add(1U, std::memory_order_relaxed)};
    }

    auto failed() const noexcept -> bool {
        return failed_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uint64_t> next_lsn_{1U};
    std::atomic<bool> failed_{false};
};

class FailArmedNewRootLog final : public BtreeMutationLogPort {
public:
    auto log(const BtreeWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.pages.empty()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "failing new-root log received an invalid mutation",
                }
            );
        }
        if (mutation.operation == BtreeWalOperation::kNewRoot
            && armed_.load(std::memory_order_acquire)) {
            failures_.fetch_add(1U, std::memory_order_relaxed);
            return base::fail(base::Error{base::ErrorCode::kIo, "injected new-root WAL failure"});
        }
        return base::Lsn{next_lsn_.fetch_add(1U, std::memory_order_relaxed)};
    }

    void arm() noexcept {
        armed_.store(true, std::memory_order_release);
    }

    auto failures() const noexcept -> std::size_t {
        return failures_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> next_lsn_{1U};
    std::atomic<bool> armed_{false};
    std::atomic<std::size_t> failures_{0U};
};

struct ExpectedEntry final {
    std::int64_t key{0};
    base::Rid rid{base::kInvalidRid};
};

struct TreeNodeSummary final {
    BtreePageType type{BtreePageType::kLeaf};
    std::uint16_t level{0};
    bool is_root{false};
    BtreePageLinks links;
    std::vector<base::PageId> children;
};

struct PendingTreeNode final {
    base::PageId page_id{base::kInvalidPageId};
    std::uint16_t level{0};
    bool is_root{false};
};

struct TreeAudit final {
    std::set<base::PageId> leaves;
};

constexpr auto heap_rid(const std::uint32_t page_id, const std::uint16_t slot_id) noexcept
    -> base::Rid {
    return base::Rid{base::PageId{page_id}, base::SlotId{slot_id}};
}

auto make_pool(const test_support::TestDirectory& directory, const std::size_t frames = 8U)
    -> base::Result<std::unique_ptr<storage::BufferPool>> {
    auto disk = storage::FileDiskManager::open(directory.join("relations"));
    if (!disk) {
        return base::fail(std::move(disk).error());
    }
    return storage::BufferPool::create(
        frames,
        std::move(disk).value(),
        nullptr,
        test::btree_durability()
    );
}

auto allocate_btree_page(storage::BufferPool& pool, const base::RelationId relation_id)
    -> base::Result<base::PageId> {
    auto pin = pool.allocate_page(relation_id, storage::PageKind::kBtree);
    if (!pin) {
        return base::fail(std::move(pin).error());
    }
    return pin->address().value().page_id;
}

auto insert_integer(
    const BtreeRelation& relation,
    storage::BufferPool& pool,
    const std::int64_t value,
    const base::Rid rid
) -> base::Status {
    const std::array values{types::Value::integer(value)};
    return relation.insert(pool, std::span<const types::Value>{values}, rid);
}

auto collect_forward(BtreeScan& scan, storage::BufferPool& pool)
    -> base::Result<std::vector<base::Rid>> {
    std::vector<base::Rid> result;
    for (;;) {
        auto next = scan.next(pool);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next->has_value()) {
            return result;
        }
        result.push_back(next->value());
    }
}

auto collect_backward(BtreeScan& scan, storage::BufferPool& pool)
    -> base::Result<std::vector<base::Rid>> {
    std::vector<base::Rid> result;
    for (;;) {
        auto previous = scan.previous(pool);
        if (!previous) {
            return base::fail(std::move(previous).error());
        }
        if (!previous->has_value()) {
            return result;
        }
        result.push_back(previous->value());
    }
}

auto data_start_slot(const BtreePage& page) -> base::Result<std::size_t> {
    if (page.tuple_count() == 0U) {
        return 0U;
    }
    auto first = page.tuple(0U);
    if (!first) {
        return base::fail(std::move(first).error());
    }
    return first->kind == BtreeTupleKind::kHighKey ? 1U : 0U;
}

auto read_tree_node(
    storage::BufferPool& pool,
    const BtreeRelation& relation,
    const base::PageId page_id
) -> base::Result<TreeNodeSummary> {
    const storage::PageAddress address{relation.relation_id(), page_id};
    auto read = pool.read_page(address);
    if (!read) {
        return base::fail(std::move(read).error());
    }
    auto page = BtreePage::open(*read->page(), address, relation.key_layout());
    if (!page) {
        return base::fail(std::move(page).error());
    }
    auto links = page->links();
    if (!links) {
        return base::fail(std::move(links).error());
    }
    TreeNodeSummary summary{
        .type = page->type(),
        .level = page->level(),
        .is_root = page->is_root(),
        .links = links.value(),
        .children = {},
    };
    if (summary.type == BtreePageType::kLeaf) {
        return summary;
    }
    auto first = data_start_slot(page.value());
    if (!first) {
        return base::fail(std::move(first).error());
    }
    for (std::size_t slot = first.value(); slot < page->tuple_count(); slot += 2U) {
        auto child = page->tuple(slot);
        if (!child) {
            return base::fail(std::move(child).error());
        }
        if (child->kind != BtreeTupleKind::kChildPage || !child->child_page_id.has_value()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kCorruption,
                    "B-tree internal node has an invalid child tuple",
                }
            );
        }
        summary.children.push_back(child->child_page_id.value());
    }
    return summary;
}

auto rewrite_tree_links(
    storage::BufferPool& pool,
    const BtreeRelation& relation,
    const base::PageId page_id,
    const BtreePageLinks links
) -> base::Status {
    const storage::PageAddress address{relation.relation_id(), page_id};
    auto write = pool.write_page(address);
    if (!write) {
        return base::fail(std::move(write).error());
    }
    auto page = BtreePage::open(*write->page(), address, relation.key_layout());
    if (!page) {
        return base::fail(std::move(page).error());
    }
    return page->set_links(links);
}

auto find_incomplete_split_pages(storage::BufferPool& pool, const BtreeRelation& relation)
    -> base::Result<std::vector<base::PageId>> {
    auto page_count = pool.relation_page_count(relation.relation_id());
    if (!page_count) {
        return base::fail(std::move(page_count).error());
    }
    std::vector<base::PageId> incomplete;
    for (std::uint32_t page_id = 1U; page_id < page_count.value(); ++page_id) {
        const storage::PageAddress address{relation.relation_id(), base::PageId{page_id}};
        auto read = pool.read_page(address);
        if (!read) {
            return base::fail(std::move(read).error());
        }
        auto page = BtreePage::open(*read->page(), address, relation.key_layout());
        if (!page) {
            return base::fail(std::move(page).error());
        }
        if (page->incomplete_split()) {
            incomplete.push_back(address.page_id);
        }
    }
    return incomplete;
}

auto check_expected_tree_node(
    const TreeNodeSummary& node,
    const PendingTreeNode& expected,
    const base::PageId root_page_id
) -> base::Status {
    if (node.level != expected.level || node.is_root != expected.is_root) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "B-tree node level or root flag disagrees with its parent path",
            }
        );
    }
    if (node.is_root && expected.page_id != root_page_id) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "a root flag appears away from the root page",
            }
        );
    }
    return {};
}

auto check_next_sibling(
    storage::BufferPool& pool,
    const BtreeRelation& relation,
    const base::PageId page_id,
    const TreeNodeSummary& node
) -> base::Status {
    if (!node.links.next.has_value()) {
        return {};
    }
    auto next = read_tree_node(pool, relation, node.links.next.value());
    if (!next) {
        return base::fail(std::move(next).error());
    }
    if (next->type != node.type || next->level != node.level
        || next->links.previous != std::optional{page_id}) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "B-tree sibling links are not reciprocal at one level",
            }
        );
    }
    return {};
}

auto collect_tree_audit(storage::BufferPool& pool, const BtreeRelation& relation)
    -> base::Result<TreeAudit> {
    auto root = relation.root(pool);
    if (!root) {
        return base::fail(std::move(root).error());
    }
    if (!root->has_value()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "B-tree structural audit requires a published root",
            }
        );
    }
    std::vector<PendingTreeNode> pending{PendingTreeNode{
        .page_id = root->value().page_id,
        .level = root->value().level,
        .is_root = true,
    }};
    std::set<base::PageId> visited;
    TreeAudit audit;
    std::size_t root_flags = 0U;
    while (!pending.empty()) {
        const auto pending_node = pending.back();
        pending.pop_back();
        if (!visited.insert(pending_node.page_id).second) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kCorruption,
                    "B-tree page is reachable from more than one parent",
                }
            );
        }
        auto node = read_tree_node(pool, relation, pending_node.page_id);
        if (!node) {
            return base::fail(std::move(node).error());
        }
        auto expected = check_expected_tree_node(node.value(), pending_node, root->value().page_id);
        if (!expected) {
            return base::fail(std::move(expected).error());
        }
        if (node->is_root) {
            ++root_flags;
        }
        auto sibling = check_next_sibling(pool, relation, pending_node.page_id, node.value());
        if (!sibling) {
            return base::fail(std::move(sibling).error());
        }
        if (node->type == BtreePageType::kLeaf) {
            if (node->level != 0U) {
                return base::fail(
                    base::Error{
                        base::ErrorCode::kCorruption,
                        "B-tree leaf does not occupy level zero",
                    }
                );
            }
            audit.leaves.insert(pending_node.page_id);
            continue;
        }
        if (node->level == 0U || node->children.empty()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kCorruption,
                    "B-tree internal node has no lower-level child path",
                }
            );
        }
        for (const auto child : node->children) {
            pending.push_back(
                PendingTreeNode{
                    .page_id = child,
                    .level = static_cast<std::uint16_t>(node->level - 1U),
                    .is_root = false,
                }
            );
        }
    }
    if (root_flags != 1U) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "B-tree has an invalid count of root-flagged pages",
            }
        );
    }
    return audit;
}

auto find_leftmost_leaf(
    storage::BufferPool& pool,
    const BtreeRelation& relation,
    const std::set<base::PageId>& leaves
) -> base::Result<base::PageId> {
    std::optional<base::PageId> leftmost;
    for (const auto leaf_page_id : leaves) {
        auto leaf = read_tree_node(pool, relation, leaf_page_id);
        if (!leaf) {
            return base::fail(std::move(leaf).error());
        }
        if (!leaf->links.previous.has_value()) {
            if (leftmost.has_value()) {
                return base::fail(
                    base::Error{
                        base::ErrorCode::kCorruption,
                        "B-tree leaf chain has more than one left edge",
                    }
                );
            }
            leftmost = leaf_page_id;
        }
    }
    if (!leftmost.has_value()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "B-tree leaf chain has no left edge",
            }
        );
    }
    return leftmost.value();
}

auto validate_leaf_chain(
    storage::BufferPool& pool,
    const BtreeRelation& relation,
    const std::set<base::PageId>& leaves,
    base::PageId page_id
) -> base::Status {
    std::set<base::PageId> leaf_chain;
    for (;;) {
        if (!leaf_chain.insert(page_id).second) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kCorruption,
                    "B-tree leaf sibling links form a cycle",
                }
            );
        }
        auto leaf = read_tree_node(pool, relation, page_id);
        if (!leaf) {
            return base::fail(std::move(leaf).error());
        }
        if (leaf->type != BtreePageType::kLeaf) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kCorruption,
                    "B-tree leaf chain points at a non-leaf page",
                }
            );
        }
        if (!leaf->links.next.has_value()) {
            break;
        }
        page_id = leaf->links.next.value();
    }
    if (leaf_chain != leaves) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "B-tree leaf chain does not cover the reachable leaf set",
            }
        );
    }
    return {};
}

auto validate_tree_structure(storage::BufferPool& pool, const BtreeRelation& relation)
    -> base::Status {
    auto audit = collect_tree_audit(pool, relation);
    if (!audit) {
        return base::fail(std::move(audit).error());
    }
    auto leftmost = find_leftmost_leaf(pool, relation, audit->leaves);
    if (!leftmost) {
        return base::fail(std::move(leftmost).error());
    }
    return validate_leaf_chain(pool, relation, audit->leaves, leftmost.value());
}

void assert_tree_structure(storage::BufferPool& pool, const BtreeRelation& relation) {
    auto checked = validate_tree_structure(pool, relation);
    ASSERT_TRUE(checked) << checked.error().to_string();
}

auto sorted_rids(std::vector<ExpectedEntry> entries) -> std::vector<base::Rid> {
    std::stable_sort(
        entries.begin(),
        entries.end(),
        [](const ExpectedEntry& left, const ExpectedEntry& right) {
            return left.key < right.key;
        }
    );
    std::vector<base::Rid> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        result.push_back(entry.rid);
    }
    return result;
}

auto wide_key(const std::size_t value) -> std::string {
    return std::string(1U, static_cast<char>('a' + static_cast<char>(value)))
           + std::string(999U, 'x');
}

auto wide_key_before_first(const std::size_t value) -> std::string {
    return std::string{"a"} + std::string(1U, static_cast<char>('0' + static_cast<char>(value)))
           + std::string(998U, 'x');
}

void wait_for_concurrent_start(std::atomic<std::size_t>& ready, std::atomic<bool>& start) noexcept {
    ready.fetch_add(1U, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void release_concurrent_workers(
    const std::size_t expected_workers,
    const std::atomic<std::size_t>& ready,
    std::atomic<bool>& start
) noexcept {
    while (ready.load(std::memory_order_acquire) != expected_workers) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
}

void record_concurrent_failure(std::atomic<std::size_t>& failures) noexcept {
    failures.fetch_add(1U, std::memory_order_relaxed);
}

TEST(BtreeInsertTest, CreatesRootOnFirstInsertAndMaintainsDuplicateKeyOrder) {
    auto directory = test_support::TestDirectory::create("btree-insert-first-root");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{611};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    const std::array entries{
        ExpectedEntry{.key = 4, .rid = heap_rid(104U, 4U)},
        ExpectedEntry{.key = 1, .rid = heap_rid(101U, 1U)},
        ExpectedEntry{.key = 4, .rid = heap_rid(204U, 4U)},
        ExpectedEntry{.key = 3, .rid = heap_rid(103U, 3U)},
    };
    for (const auto& entry : entries) {
        ASSERT_TRUE(insert_integer(relation.value(), *pool, entry.key, entry.rid));
    }

    auto root = relation->root(*pool);
    ASSERT_TRUE(root) << root.error().to_string();
    ASSERT_TRUE(root->has_value());
    EXPECT_EQ(root->value().level, 0U);
    const storage::PageAddress address{relation_id, root->value().page_id};
    auto read = pool->read_page(address);
    ASSERT_TRUE(read) << read.error().to_string();
    auto leaf = BtreePage::open(*read->page(), address, relation->key_layout());
    ASSERT_TRUE(leaf) << leaf.error().to_string();
    EXPECT_TRUE(leaf->is_root());
    EXPECT_EQ(leaf->type(), BtreePageType::kLeaf);
    EXPECT_EQ(leaf->tuple_count(), entries.size());

    auto scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto observed = collect_forward(scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    EXPECT_EQ(observed.value(), sorted_rids({entries.begin(), entries.end()}));
}

TEST(BtreeInsertTest, InPlaceLeafInsertRollsBackTheExactPageWhenWalFails) {
    auto directory = test_support::TestDirectory::create("btree-insert-wal-rollback");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    FailNextLeafLog log;
    const auto relation_id = base::RelationId{621};
    auto relation = BtreeRelation::create(relation_id, *pool, layout.value(), false, log);
    ASSERT_TRUE(relation) << relation.error().to_string();
    ASSERT_TRUE(insert_integer(relation.value(), *pool, 1, heap_rid(101U, 1U)));

    auto root = relation->root(*pool);
    ASSERT_TRUE(root) << root.error().to_string();
    ASSERT_TRUE(root->has_value());
    const storage::PageAddress root_address{relation_id, root->value().page_id};
    storage::Page::Storage before{};
    {
        auto read = pool->read_page(root_address);
        ASSERT_TRUE(read) << read.error().to_string();
        before = read->page()->bytes();
    }

    log.fail_next_leaf();
    auto failed = insert_integer(relation.value(), *pool, 2, heap_rid(102U, 2U));
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    {
        auto read = pool->read_page(root_address);
        ASSERT_TRUE(read) << read.error().to_string();
        EXPECT_EQ(read->page()->bytes(), before);
    }

    auto scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto observed = collect_forward(scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    EXPECT_EQ(observed.value(), (std::vector<base::Rid>{heap_rid(101U, 1U)}));

    ASSERT_TRUE(insert_integer(relation.value(), *pool, 2, heap_rid(102U, 2U)));
}

TEST(BtreeConcurrencyTest, FailedRootPublicationRollsBackWhilePageLatchesRemainHeld) {
    auto directory = test_support::TestDirectory::create("btree-root-publish-rollback");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 256U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    FailArmedNewRootLog log;
    auto relation = BtreeRelation::create(base::RelationId{624}, *pool, layout.value(), false, log);
    ASSERT_TRUE(relation) << relation.error().to_string();

    std::vector<base::Rid> expected;
    ASSERT_TRUE(insert_integer(relation.value(), *pool, 0, heap_rid(30'000U, 1U)));
    expected.push_back(heap_rid(30'000U, 1U));
    log.arm();

    std::int64_t failing_key = -1;
    for (std::int64_t key = 1; key < 1'000; ++key) {
        const auto rid = heap_rid(static_cast<std::uint32_t>(30'000 + key), 1U);
        auto inserted = insert_integer(relation.value(), *pool, key, rid);
        if (!inserted) {
            EXPECT_EQ(inserted.error().code(), base::ErrorCode::kIo);
            failing_key = key;
            break;
        }
        expected.push_back(rid);
    }
    ASSERT_NE(failing_key, -1);
    auto root_before = relation->root(*pool);
    ASSERT_TRUE(root_before) << root_before.error().to_string();

    std::atomic<bool> keep_reading{true};
    std::atomic<bool> reader_failed{false};
    std::jthread reader{[&] {
        while (keep_reading.load(std::memory_order_acquire)) {
            if (!relation->root(*pool)) {
                reader_failed.store(true, std::memory_order_release);
                return;
            }
        }
    }};
    bool retries_failed_as_expected = true;
    for (std::size_t attempt = 0U; attempt < 64U; ++attempt) {
        auto failed = insert_integer(
            relation.value(),
            *pool,
            failing_key,
            heap_rid(static_cast<std::uint32_t>(30'000 + failing_key), 1U)
        );
        if (failed || failed.error().code() != base::ErrorCode::kIo) {
            retries_failed_as_expected = false;
            break;
        }
    }
    keep_reading.store(false, std::memory_order_release);
    reader.join();
    ASSERT_TRUE(retries_failed_as_expected);
    EXPECT_FALSE(reader_failed.load(std::memory_order_acquire));
    EXPECT_EQ(log.failures(), 65U);

    auto root_after = relation->root(*pool);
    ASSERT_TRUE(root_after) << root_after.error().to_string();
    EXPECT_EQ(root_after.value(), root_before.value());
    auto scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto observed = collect_forward(scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    EXPECT_EQ(observed.value(), expected);
    assert_tree_structure(*pool, relation.value());
}

TEST(BtreeInsertTest, WriterFinishesAPersistedIncompleteSplit) {
    auto directory = test_support::TestDirectory::create("btree-incomplete-split-help");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 64U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    FailFirstFinishSplitLog log;
    const auto relation_id = base::RelationId{622};
    auto relation = BtreeRelation::create(relation_id, *pool, layout.value(), false, log);
    ASSERT_TRUE(relation) << relation.error().to_string();

    std::size_t inserted_count = 0U;
    for (; inserted_count < 700U; ++inserted_count) {
        auto inserted = insert_integer(
            relation.value(),
            *pool,
            static_cast<std::int64_t>(inserted_count),
            heap_rid(static_cast<std::uint32_t>(10'000U + inserted_count), 1U)
        );
        if (!inserted) {
            EXPECT_EQ(inserted.error().code(), base::ErrorCode::kIo);
            break;
        }
    }
    ASSERT_TRUE(log.failed());
    ASSERT_LT(inserted_count, 700U);

    auto incomplete = find_incomplete_split_pages(*pool, relation.value());
    ASSERT_TRUE(incomplete) << incomplete.error().to_string();
    ASSERT_EQ(incomplete->size(), 1U);
    ASSERT_TRUE(pool->flush_all());
    ASSERT_TRUE(pool->sync_relation(relation_id));
    pool.reset();

    auto reopened_pool = make_pool(directory.value(), 64U);
    ASSERT_TRUE(reopened_pool) << reopened_pool.error().to_string();
    pool = std::move(reopened_pool).value();
    auto reopened = BtreeRelation::open(relation_id, *pool, layout.value(), false, log);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto persisted = find_incomplete_split_pages(*pool, reopened.value());
    ASSERT_TRUE(persisted) << persisted.error().to_string();
    EXPECT_EQ(persisted.value(), incomplete.value());

    const auto helped_key = inserted_count + 1U;
    ASSERT_TRUE(insert_integer(
        reopened.value(),
        *pool,
        static_cast<std::int64_t>(helped_key),
        heap_rid(static_cast<std::uint32_t>(10'000U + helped_key), 1U)
    ));
    auto completed = find_incomplete_split_pages(*pool, reopened.value());
    ASSERT_TRUE(completed) << completed.error().to_string();
    EXPECT_TRUE(completed->empty());

    std::vector<base::Rid> expected;
    expected.reserve(helped_key + 1U);
    for (std::size_t key = 0U; key <= inserted_count; ++key) {
        expected.push_back(heap_rid(static_cast<std::uint32_t>(10'000U + key), 1U));
    }
    expected.push_back(heap_rid(static_cast<std::uint32_t>(10'000U + helped_key), 1U));
    auto scan = BtreeScan::for_full(reopened.value(), {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto observed = collect_forward(scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    EXPECT_EQ(observed.value(), expected);
    auto audit = collect_tree_audit(*pool, reopened.value());
    ASSERT_TRUE(audit) << audit.error().to_string();
    EXPECT_GE(audit->leaves.size(), 3U);
}

TEST(BtreeInsertTest, StaleLeafFastRootUsesFakeStackAcrossPersistedDuplicateBoundaries) {
    auto directory = test_support::TestDirectory::create("btree-duplicate-fake-stack");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 16U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    constexpr auto relation_id = base::RelationId{623};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    auto stale_root = relation->create_empty_root(*pool);
    ASSERT_TRUE(stale_root) << stale_root.error().to_string();
    const auto split_left_id = stale_root->page_id;
    auto split_right_id = allocate_btree_page(*pool, relation_id);
    auto far_right_leaf_id = allocate_btree_page(*pool, relation_id);
    auto left_parent_id = allocate_btree_page(*pool, relation_id);
    auto right_parent_id = allocate_btree_page(*pool, relation_id);
    auto root_id = allocate_btree_page(*pool, relation_id);
    ASSERT_TRUE(
        split_right_id && far_right_leaf_id && left_parent_id && right_parent_id && root_id
    );

    const std::array boundary_value{types::Value::integer(42)};
    const std::array left_value{types::Value::integer(41)};
    const std::array right_value{types::Value::integer(43)};
    auto boundary = layout->encode(boundary_value);
    auto left_key = layout->encode(left_value);
    auto right_key = layout->encode(right_value);
    ASSERT_TRUE(boundary && left_key && right_key);

    {
        const storage::PageAddress address{relation_id, split_left_id};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write) << write.error().to_string();
        auto leaf = BtreePage::initialize(
            *write->page(),
            address,
            layout.value(),
            BtreePageType::kLeaf,
            0U
        );
        ASSERT_TRUE(leaf) << leaf.error().to_string();
        ASSERT_TRUE(leaf->append_high_key(boundary.value()));
        ASSERT_TRUE(leaf->append_leaf_entry(left_key.value(), heap_rid(20'001U, 1U)));
        ASSERT_TRUE(leaf->set_links({.previous = std::nullopt, .next = split_right_id.value()}));
        ASSERT_TRUE(leaf->set_incomplete_split(true));
    }
    std::vector<base::Rid> expected{
        heap_rid(20'001U, 1U),
        heap_rid(20'002U, 1U),
        heap_rid(20'003U, 1U),
    };
    {
        const storage::PageAddress address{relation_id, split_right_id.value()};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write) << write.error().to_string();
        auto leaf = BtreePage::initialize(
            *write->page(),
            address,
            layout.value(),
            BtreePageType::kLeaf,
            0U
        );
        ASSERT_TRUE(leaf) << leaf.error().to_string();
        ASSERT_TRUE(leaf->append_high_key(right_key.value()));
        for (std::uint32_t duplicate = 0U; duplicate < 8U; ++duplicate) {
            const auto rid = heap_rid(21'000U + duplicate, 1U);
            ASSERT_TRUE(leaf->append_leaf_entry(boundary.value(), rid));
            expected.push_back(rid);
        }
        ASSERT_TRUE(
            leaf->set_links({.previous = split_left_id, .next = far_right_leaf_id.value()})
        );
    }
    {
        const storage::PageAddress address{relation_id, far_right_leaf_id.value()};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write) << write.error().to_string();
        auto leaf = BtreePage::initialize(
            *write->page(),
            address,
            layout.value(),
            BtreePageType::kLeaf,
            0U
        );
        ASSERT_TRUE(leaf) << leaf.error().to_string();
        ASSERT_TRUE(leaf->append_leaf_entry(right_key.value(), heap_rid(22'000U, 1U)));
        expected.push_back(heap_rid(22'000U, 1U));
        ASSERT_TRUE(leaf->set_links({.previous = split_right_id.value(), .next = std::nullopt}));
    }
    {
        const storage::PageAddress address{relation_id, left_parent_id.value()};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write) << write.error().to_string();
        auto parent = BtreePage::initialize(
            *write->page(),
            address,
            layout.value(),
            BtreePageType::kInternal,
            1U
        );
        ASSERT_TRUE(parent) << parent.error().to_string();
        ASSERT_TRUE(parent->append_high_key(boundary.value()));
        ASSERT_TRUE(parent->append_first_child(split_left_id));
        ASSERT_TRUE(parent->set_links({.previous = std::nullopt, .next = right_parent_id.value()}));
    }
    {
        const storage::PageAddress address{relation_id, right_parent_id.value()};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write) << write.error().to_string();
        auto parent = BtreePage::initialize(
            *write->page(),
            address,
            layout.value(),
            BtreePageType::kInternal,
            1U
        );
        ASSERT_TRUE(parent) << parent.error().to_string();
        ASSERT_TRUE(parent->append_first_child(far_right_leaf_id.value()));
        ASSERT_TRUE(parent->set_links({.previous = left_parent_id.value(), .next = std::nullopt}));
    }
    {
        const storage::PageAddress address{relation_id, root_id.value()};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write) << write.error().to_string();
        auto root = BtreePage::initialize(
            *write->page(),
            address,
            layout.value(),
            BtreePageType::kInternal,
            2U,
            true
        );
        ASSERT_TRUE(root) << root.error().to_string();
        ASSERT_TRUE(root->append_first_child(left_parent_id.value()));
        ASSERT_TRUE(root->append_separator_and_child(boundary.value(), right_parent_id.value()));
    }
    {
        const storage::PageAddress address{relation_id, base::PageId{0}};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write) << write.error().to_string();
        auto metapage = BtreeMetaPage::from_latched_page(*write->page(), address, layout.value());
        ASSERT_TRUE(metapage.set_root(root_id.value(), 2U));
    }

    ASSERT_TRUE(pool->flush_all());
    ASSERT_TRUE(pool->sync_relation(relation_id));
    pool.reset();
    auto reopened_pool = make_pool(directory.value(), 16U);
    ASSERT_TRUE(reopened_pool) << reopened_pool.error().to_string();
    pool = std::move(reopened_pool).value();
    storage::Page::Storage right_parent_before{};
    {
        auto read = pool->read_page({relation_id, right_parent_id.value()});
        ASSERT_TRUE(read) << read.error().to_string();
        right_parent_before = read->page()->bytes();
    }
    ASSERT_TRUE(insert_integer(relation.value(), *pool, 41, heap_rid(20'002U, 1U)));
    ASSERT_TRUE(insert_integer(relation.value(), *pool, 41, heap_rid(20'003U, 1U)));

    {
        auto read = pool->read_page({relation_id, left_parent_id.value()});
        ASSERT_TRUE(read) << read.error().to_string();
        const auto parent = BtreePage::from_latched_page(*read->page());
        ASSERT_EQ(parent.tuple_count(), 4U);
        EXPECT_EQ(parent.tuple_ref(1U).target_page_id, split_left_id);
        EXPECT_EQ(parent.tuple_ref(3U).target_page_id, split_right_id.value());
    }
    {
        auto read = pool->read_page({relation_id, split_left_id});
        ASSERT_TRUE(read) << read.error().to_string();
        EXPECT_FALSE(BtreePage::from_latched_page(*read->page()).incomplete_split());
    }
    {
        auto read = pool->read_page({relation_id, right_parent_id.value()});
        ASSERT_TRUE(read) << read.error().to_string();
        EXPECT_EQ(read->page()->bytes(), right_parent_before);
    }

    auto scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto observed = collect_forward(scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    EXPECT_EQ(observed.value(), expected);
    assert_tree_structure(*pool, relation.value());
}

TEST(BtreeInsertTest, SplitsLeafPublishesRootAndPersistsOrderedScan) {
    auto directory = test_support::TestDirectory::create("btree-insert-leaf-split");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{612};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    std::vector<ExpectedEntry> expected;
    for (std::int64_t value = 0; value < 180; ++value) {
        const auto rid = heap_rid(static_cast<std::uint32_t>(1'000 + value), 1U);
        ASSERT_TRUE(insert_integer(relation.value(), *pool, value, rid));
        expected.push_back(ExpectedEntry{.key = value, .rid = rid});
    }
    const std::array duplicate_rids{heap_rid(2'000U, 1U), heap_rid(2'001U, 1U)};
    for (const auto rid : duplicate_rids) {
        ASSERT_TRUE(insert_integer(relation.value(), *pool, 42, rid));
        expected.push_back(ExpectedEntry{.key = 42, .rid = rid});
    }

    auto root = relation->root(*pool);
    ASSERT_TRUE(root) << root.error().to_string();
    ASSERT_TRUE(root->has_value());
    EXPECT_EQ(root->value().level, 1U);
    {
        const storage::PageAddress root_address{relation_id, root->value().page_id};
        auto root_read = pool->read_page(root_address);
        ASSERT_TRUE(root_read) << root_read.error().to_string();
        auto root_page = BtreePage::open(*root_read->page(), root_address, relation->key_layout());
        ASSERT_TRUE(root_page) << root_page.error().to_string();
        ASSERT_TRUE(root_page->is_root());
        ASSERT_EQ(root_page->type(), BtreePageType::kInternal);
        ASSERT_EQ(root_page->level(), 1U);
        ASSERT_EQ(root_page->tuple_count(), 3U);
        auto left_child = root_page->tuple(0U);
        auto right_child = root_page->tuple(2U);
        ASSERT_TRUE(left_child) << left_child.error().to_string();
        ASSERT_TRUE(right_child) << right_child.error().to_string();
        ASSERT_TRUE(left_child->child_page_id.has_value());
        ASSERT_TRUE(right_child->child_page_id.has_value());
        const storage::PageAddress left_address{relation_id, left_child->child_page_id.value()};
        const storage::PageAddress right_address{relation_id, right_child->child_page_id.value()};
        auto left_read = pool->read_page(left_address);
        auto right_read = pool->read_page(right_address);
        ASSERT_TRUE(left_read) << left_read.error().to_string();
        ASSERT_TRUE(right_read) << right_read.error().to_string();
        auto left = BtreePage::open(*left_read->page(), left_address, relation->key_layout());
        auto right = BtreePage::open(*right_read->page(), right_address, relation->key_layout());
        ASSERT_TRUE(left) << left.error().to_string();
        ASSERT_TRUE(right) << right.error().to_string();
        auto left_links = left->links();
        auto right_links = right->links();
        ASSERT_TRUE(left_links) << left_links.error().to_string();
        ASSERT_TRUE(right_links) << right_links.error().to_string();
        EXPECT_EQ(left_links->next, std::optional{right_address.page_id});
        EXPECT_EQ(right_links->previous, std::optional{left_address.page_id});
    }

    const auto sorted = sorted_rids(expected);
    auto forward_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(forward_scan) << forward_scan.error().to_string();
    auto forward = collect_forward(forward_scan.value(), *pool);
    ASSERT_TRUE(forward) << forward.error().to_string();
    EXPECT_EQ(forward.value(), sorted);
    EXPECT_EQ(forward_scan->leaf_pages_visited(), 2U);

    auto backward_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(backward_scan) << backward_scan.error().to_string();
    auto backward = collect_backward(backward_scan.value(), *pool);
    ASSERT_TRUE(backward) << backward.error().to_string();
    auto reversed = sorted;
    std::reverse(reversed.begin(), reversed.end());
    EXPECT_EQ(backward.value(), reversed);

    ASSERT_TRUE(pool->flush_all());
    ASSERT_TRUE(pool->sync_relation(relation_id));
    pool.reset();
    auto reopened_pool_result = make_pool(directory.value());
    ASSERT_TRUE(reopened_pool_result) << reopened_pool_result.error().to_string();
    auto reopened_pool = std::move(reopened_pool_result).value();
    auto reopened =
        BtreeRelation::open(relation_id, *reopened_pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto reopened_scan = BtreeScan::for_full(reopened.value(), {});
    ASSERT_TRUE(reopened_scan) << reopened_scan.error().to_string();
    auto reopened_rids = collect_forward(reopened_scan.value(), *reopened_pool);
    ASSERT_TRUE(reopened_rids) << reopened_rids.error().to_string();
    EXPECT_EQ(reopened_rids.value(), sorted);
}

TEST(BtreeInsertTest, SplitsInternalPagesAndPropagatesANewRoot) {
    auto directory = test_support::TestDirectory::create("btree-insert-internal-split");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    constexpr types::TypeModifier modifier{1'000};
    const types::DataType key_type{types::kCharTypeId, modifier};
    auto layout = BtreeKeyLayout::create({key_type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{613};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    std::vector<base::Rid> expected;
    for (std::size_t value = 0U; value < 18U; ++value) {
        const std::array values{types::Value::character(wide_key(value), modifier)};
        const auto rid = heap_rid(static_cast<std::uint32_t>(3'000U + value), 1U);
        ASSERT_TRUE(relation->insert(*pool, std::span<const types::Value>{values}, rid));
        expected.push_back(rid);
    }
    for (std::size_t value = 0U; value < 10U; ++value) {
        const std::array values{types::Value::character(wide_key_before_first(value), modifier)};
        const auto rid = heap_rid(static_cast<std::uint32_t>(3'100U + value), 1U);
        ASSERT_TRUE(relation->insert(*pool, std::span<const types::Value>{values}, rid));
        expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(value), rid);
    }

    auto root = relation->root(*pool);
    ASSERT_TRUE(root) << root.error().to_string();
    ASSERT_TRUE(root->has_value());
    EXPECT_GE(root->value().level, 2U);
    const storage::PageAddress root_address{relation_id, root->value().page_id};
    auto root_read = pool->read_page(root_address);
    ASSERT_TRUE(root_read) << root_read.error().to_string();
    auto root_page = BtreePage::open(*root_read->page(), root_address, relation->key_layout());
    ASSERT_TRUE(root_page) << root_page.error().to_string();
    EXPECT_TRUE(root_page->is_root());
    EXPECT_EQ(root_page->type(), BtreePageType::kInternal);
    EXPECT_EQ(root_page->level(), root->value().level);
    assert_tree_structure(*pool, relation.value());

    auto forward_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(forward_scan) << forward_scan.error().to_string();
    auto observed = collect_forward(forward_scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    EXPECT_EQ(observed.value(), expected);
    EXPECT_GT(forward_scan->leaf_pages_visited(), 4U);

    for (std::size_t value = 0U; value < 18U; ++value) {
        const std::array values{types::Value::character(wide_key(value), modifier)};
        auto equality =
            BtreeScan::for_eq(relation.value(), std::span<const types::Value>{values}, {});
        ASSERT_TRUE(equality) << equality.error().to_string();
        auto matches = collect_forward(equality.value(), *pool);
        ASSERT_TRUE(matches) << matches.error().to_string();
        EXPECT_EQ(matches.value(), (std::vector<base::Rid>{expected[value + 10U]}));
    }
}

TEST(BtreeInsertTest, RejectsASingleEntryThatCannotFitWithoutCorruptingTheRoot) {
    auto directory = test_support::TestDirectory::create("btree-insert-oversized-entry");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    constexpr types::TypeModifier modifier{4'048};
    const types::DataType key_type{types::kCharTypeId, modifier};
    auto layout = BtreeKeyLayout::create({key_type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{614};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();
    const std::array values{types::Value::character(std::string(4'048U, 'x'), modifier)};

    auto inserted = relation->insert(
        *pool,
        std::span<const types::Value>{values.data(), values.size()},
        heap_rid(4'000U, 1U)
    );
    ASSERT_FALSE(inserted);
    EXPECT_EQ(inserted.error().code(), base::ErrorCode::kResourceExhausted);
    auto root = relation->root(*pool);
    ASSERT_TRUE(root) << root.error().to_string();
    ASSERT_TRUE(root->has_value());
    EXPECT_EQ(root->value().level, 0U);
    EXPECT_EQ(pool->relation_page_count(relation_id).value(), 2U);
    auto scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto observed = collect_forward(scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    EXPECT_TRUE(observed->empty());
}

TEST(BtreeConcurrencyTest, OpenedRelationUsesItsFastRootHint) {
    auto directory = test_support::TestDirectory::create("btree-root-hint-open");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 64U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{615};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();
    for (std::int64_t value = 0; value < 180; ++value) {
        ASSERT_TRUE(insert_integer(
            relation.value(),
            *pool,
            value,
            heap_rid(static_cast<std::uint32_t>(7'000 + value), 1U)
        ));
    }

    auto opened = BtreeRelation::open(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(opened) << opened.error().to_string();
    const std::array values{types::Value::integer(137)};
    auto scan = BtreeScan::for_eq(opened.value(), std::span<const types::Value>{values}, {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto matches = collect_forward(scan.value(), *pool);
    ASSERT_TRUE(matches) << matches.error().to_string();
    ASSERT_EQ(matches->size(), 1U);
    EXPECT_EQ(matches->front(), heap_rid(7'137U, 1U));
    EXPECT_EQ(scan->root_hint_uses(), 1U);
}

TEST(BtreeConcurrencyTest, UsesAFormerRootAsFastRootAfterRootSplit) {
    auto directory = test_support::TestDirectory::create("btree-root-hint-stale");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 64U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{616};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();
    ASSERT_TRUE(insert_integer(relation.value(), *pool, 0, heap_rid(8'000U, 1U)));

    const std::array old_values{types::Value::integer(0)};
    auto stale_scan =
        BtreeScan::for_eq(relation.value(), std::span<const types::Value>{old_values}, {});
    ASSERT_TRUE(stale_scan) << stale_scan.error().to_string();
    auto stale_backward = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(stale_backward) << stale_backward.error().to_string();
    for (std::int64_t value = 1; value < 180; ++value) {
        ASSERT_TRUE(insert_integer(
            relation.value(),
            *pool,
            value,
            heap_rid(static_cast<std::uint32_t>(8'000 + value), 1U)
        ));
    }

    auto old_matches = collect_forward(stale_scan.value(), *pool);
    ASSERT_TRUE(old_matches) << old_matches.error().to_string();
    EXPECT_EQ(old_matches.value(), (std::vector<base::Rid>{heap_rid(8'000U, 1U)}));
    EXPECT_EQ(stale_scan->root_hint_uses(), 1U);

    auto backward_matches = collect_backward(stale_backward.value(), *pool);
    ASSERT_TRUE(backward_matches) << backward_matches.error().to_string();
    std::vector<base::Rid> expected_backward;
    expected_backward.reserve(180U);
    for (std::uint32_t value = 180U; value > 0U; --value) {
        expected_backward.push_back(heap_rid(8'000U + value - 1U, 1U));
    }
    EXPECT_EQ(backward_matches.value(), expected_backward);
    EXPECT_EQ(stale_backward->root_hint_uses(), 1U);

    const std::array fresh_values{types::Value::integer(179)};
    auto fresh_scan =
        BtreeScan::for_eq(relation.value(), std::span<const types::Value>{fresh_values}, {});
    ASSERT_TRUE(fresh_scan) << fresh_scan.error().to_string();
    auto fresh_matches = collect_forward(fresh_scan.value(), *pool);
    ASSERT_TRUE(fresh_matches) << fresh_matches.error().to_string();
    EXPECT_EQ(fresh_matches.value(), (std::vector<base::Rid>{heap_rid(8'179U, 1U)}));
    EXPECT_EQ(fresh_scan->root_hint_uses(), 1U);
}

TEST(BtreeConcurrencyTest, BridgesBackwardFromACopiedSiblingEdge) {
    auto directory = test_support::TestDirectory::create("btree-stale-sibling-bridge");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 64U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{620};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    constexpr std::size_t k_entries = 500U;
    std::vector<base::Rid> expected;
    expected.reserve(k_entries);
    for (std::size_t index = 0U; index < k_entries; ++index) {
        const auto rid = heap_rid(static_cast<std::uint32_t>(9'000U + index), 1U);
        ASSERT_TRUE(insert_integer(relation.value(), *pool, static_cast<std::int64_t>(index), rid));
        expected.push_back(rid);
    }
    auto audit = collect_tree_audit(*pool, relation.value());
    ASSERT_TRUE(audit) << audit.error().to_string();
    auto left = find_leftmost_leaf(*pool, relation.value(), audit->leaves);
    ASSERT_TRUE(left) << left.error().to_string();
    auto left_node = read_tree_node(*pool, relation.value(), left.value());
    ASSERT_TRUE(left_node) << left_node.error().to_string();
    ASSERT_TRUE(left_node->links.next.has_value());
    const auto bridge_page_id = left_node->links.next.value();
    auto bridge_node = read_tree_node(*pool, relation.value(), bridge_page_id);
    ASSERT_TRUE(bridge_node) << bridge_node.error().to_string();
    ASSERT_TRUE(bridge_node->links.next.has_value());
    const auto target_page_id = bridge_node->links.next.value();
    auto target_node = read_tree_node(*pool, relation.value(), target_page_id);
    ASSERT_TRUE(target_node) << target_node.error().to_string();

    // Model a reader that copied left.next before a split, then releases its
    // latch after the splitter has installed bridge.previous = left and
    // target.previous = bridge. The durable chain remains left -> bridge ->
    // target, but this reader's stale edge still targets the old right leaf.
    auto forward_links = left_node->links;
    forward_links.next = target_page_id;
    ASSERT_TRUE(rewrite_tree_links(*pool, relation.value(), left.value(), forward_links));
    auto forward = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(forward) << forward.error().to_string();
    auto forward_rows = collect_forward(forward.value(), *pool);
    ASSERT_TRUE(forward_rows) << forward_rows.error().to_string();
    EXPECT_LT(forward_rows->size(), expected.size());

    ASSERT_TRUE(rewrite_tree_links(*pool, relation.value(), left.value(), left_node->links));
    auto backward_links = target_node->links;
    backward_links.previous = left.value();
    ASSERT_TRUE(rewrite_tree_links(*pool, relation.value(), target_page_id, backward_links));
    auto backward = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(backward) << backward.error().to_string();
    auto backward_rows = collect_backward(backward.value(), *pool);
    ASSERT_TRUE(backward_rows) << backward_rows.error().to_string();
    std::ranges::reverse(expected);
    EXPECT_EQ(backward_rows.value(), expected);
}

TEST(BtreeConcurrencyTest, BackwardBridgeWalkCoversALeftSplitAfterReaderUnlatches) {
    auto directory = test_support::TestDirectory::create("btree-backward-live-bridge");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 64U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{623};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    std::vector<base::Rid> expected;
    expected.reserve(680U);
    for (std::int64_t key = 1'000; key < 1'180; ++key) {
        const auto rid = heap_rid(static_cast<std::uint32_t>(12'000 + key), 1U);
        ASSERT_TRUE(insert_integer(relation.value(), *pool, key, rid));
        expected.push_back(rid);
    }
    auto audit = collect_tree_audit(*pool, relation.value());
    ASSERT_TRUE(audit) << audit.error().to_string();
    auto left_page_id = find_leftmost_leaf(*pool, relation.value(), audit->leaves);
    ASSERT_TRUE(left_page_id) << left_page_id.error().to_string();
    auto left_before = read_tree_node(*pool, relation.value(), left_page_id.value());
    ASSERT_TRUE(left_before) << left_before.error().to_string();
    ASSERT_TRUE(left_before->links.next.has_value());
    const auto origin_page_id = left_before->links.next.value();
    auto origin = read_tree_node(*pool, relation.value(), origin_page_id);
    ASSERT_TRUE(origin) << origin.error().to_string();
    ASSERT_FALSE(origin->links.next.has_value());

    auto backward = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(backward) << backward.error().to_string();
    auto first = backward->previous(*pool);
    ASSERT_TRUE(first) << first.error().to_string();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ(first->value(), heap_rid(13'179U, 1U));

    std::vector<base::Rid> low_entries;
    low_entries.reserve(500U);
    for (std::int64_t key = 0; key < 500; ++key) {
        const auto rid = heap_rid(static_cast<std::uint32_t>(14'000 + key), 1U);
        ASSERT_TRUE(insert_integer(relation.value(), *pool, key, rid));
        low_entries.push_back(rid);
    }
    expected.insert(expected.begin(), low_entries.begin(), low_entries.end());
    auto left_after = read_tree_node(*pool, relation.value(), left_page_id.value());
    ASSERT_TRUE(left_after) << left_after.error().to_string();
    ASSERT_TRUE(left_after->links.next.has_value());
    EXPECT_NE(left_after->links.next.value(), origin_page_id);

    std::vector<base::Rid> observed{first->value()};
    for (;;) {
        auto previous = backward->previous(*pool);
        ASSERT_TRUE(previous) << previous.error().to_string();
        if (!previous->has_value()) {
            break;
        }
        observed.push_back(previous->value());
    }
    std::ranges::reverse(expected);
    EXPECT_EQ(observed, expected);
}

TEST(BtreeConcurrencyTest, CrabsConcurrentDisjointWritersThroughRootSplits) {
    auto directory = test_support::TestDirectory::create("btree-concurrent-root-split");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 128U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{617};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    constexpr std::size_t k_writers = 4U;
    constexpr std::size_t k_keys_per_writer = 180U;
    std::atomic<std::size_t> ready{0U};
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0U};
    std::vector<std::thread> workers;
    workers.reserve(k_writers);
    for (std::size_t writer = 0U; writer < k_writers; ++writer) {
        workers.emplace_back([&, writer] {
            wait_for_concurrent_start(ready, start);
            for (std::size_t offset = 0U; offset < k_keys_per_writer; ++offset) {
                const auto key = static_cast<std::int64_t>(writer * k_keys_per_writer + offset);
                const auto rid = heap_rid(
                    static_cast<std::uint32_t>(writer + 1U),
                    static_cast<std::uint16_t>(offset + 1U)
                );
                if (!insert_integer(relation.value(), *pool, key, rid)) {
                    record_concurrent_failure(failures);
                    return;
                }
            }
        });
    }
    release_concurrent_workers(k_writers, ready, start);
    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0U);
    assert_tree_structure(*pool, relation.value());
    auto root = relation->root(*pool);
    ASSERT_TRUE(root) << root.error().to_string();
    ASSERT_TRUE(root->has_value());
    EXPECT_GE(root->value().level, 1U);
    auto full_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(full_scan) << full_scan.error().to_string();
    auto observed = collect_forward(full_scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    std::vector<base::Rid> expected;
    expected.reserve(k_writers * k_keys_per_writer);
    for (std::size_t writer = 0U; writer < k_writers; ++writer) {
        for (std::size_t offset = 0U; offset < k_keys_per_writer; ++offset) {
            expected.push_back(heap_rid(
                static_cast<std::uint32_t>(writer + 1U),
                static_cast<std::uint16_t>(offset + 1U)
            ));
        }
    }
    EXPECT_EQ(observed.value(), expected);

    auto shutdown = pool->shutdown();
    EXPECT_TRUE(shutdown) << shutdown.error().to_string();
}

TEST(BtreeConcurrencyTest, AllowsDisjointLeafWritersToProgressWhileOneLeafIsLatched) {
    auto directory = test_support::TestDirectory::create("btree-concurrent-disjoint-progress");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 128U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    BlockingLeafLog log;
    auto relation = BtreeRelation::create(base::RelationId{620}, *pool, layout.value(), false, log);
    ASSERT_TRUE(relation) << relation.error().to_string();
    for (std::int64_t key = 0; key < 2'000; ++key) {
        ASSERT_TRUE(insert_integer(
            relation.value(),
            *pool,
            key,
            heap_rid(static_cast<std::uint32_t>(key + 1), 1U)
        ));
    }

    log.arm();
    base::Status left_status;
    std::promise<base::Status> right_promise;
    auto right_future = right_promise.get_future();
    std::thread left_writer([&] {
        left_status = insert_integer(relation.value(), *pool, -1, heap_rid(10'001U, 1U));
    });
    log.wait_until_blocked();
    std::thread right_writer([&] {
        right_promise.set_value(
            insert_integer(relation.value(), *pool, 10'000, heap_rid(10'002U, 1U))
        );
    });

    const bool progressed =
        right_future.wait_for(std::chrono::seconds{10}) == std::future_status::ready;
    log.release();
    left_writer.join();
    right_writer.join();
    auto right_status = right_future.get();

    EXPECT_TRUE(progressed) << "a disjoint leaf writer waited behind another leaf write latch";
    EXPECT_TRUE(left_status) << left_status.error().to_string();
    EXPECT_TRUE(right_status) << right_status.error().to_string();
    assert_tree_structure(*pool, relation.value());
}

TEST(BtreeConcurrencyTest, SerializesOverlappingKeysAtTheirRightmostLeafRun) {
    auto directory = test_support::TestDirectory::create("btree-concurrent-overlap");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 128U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{618};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    constexpr std::size_t k_writers = 4U;
    constexpr std::size_t k_keys = 110U;
    std::atomic<std::size_t> ready{0U};
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0U};
    std::vector<std::thread> workers;
    workers.reserve(k_writers);
    for (std::size_t writer = 0U; writer < k_writers; ++writer) {
        workers.emplace_back([&, writer] {
            wait_for_concurrent_start(ready, start);
            for (std::size_t key = 0U; key < k_keys; ++key) {
                const auto rid = heap_rid(
                    static_cast<std::uint32_t>(100U + writer),
                    static_cast<std::uint16_t>(key + 1U)
                );
                if (!insert_integer(relation.value(), *pool, static_cast<std::int64_t>(key), rid)) {
                    record_concurrent_failure(failures);
                    return;
                }
            }
        });
    }
    release_concurrent_workers(k_writers, ready, start);
    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0U);
    assert_tree_structure(*pool, relation.value());
    auto full_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(full_scan) << full_scan.error().to_string();
    auto observed = collect_forward(full_scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    std::vector<base::Rid> expected;
    expected.reserve(k_writers * k_keys);
    for (std::size_t writer = 0U; writer < k_writers; ++writer) {
        for (std::size_t key = 0U; key < k_keys; ++key) {
            expected.push_back(heap_rid(
                static_cast<std::uint32_t>(100U + writer),
                static_cast<std::uint16_t>(key + 1U)
            ));
        }
    }
    auto sorted_observed = observed.value();
    std::sort(sorted_observed.begin(), sorted_observed.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(sorted_observed, expected);
}

TEST(BtreeConcurrencyTest, CouplesReadersToPagesWhileWritersSplitInterleavedKeys) {
    auto directory = test_support::TestDirectory::create("btree-concurrent-read-write");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value(), 128U);
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{619};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    constexpr std::size_t k_stable_keys = 180U;
    for (std::size_t index = 0U; index < k_stable_keys; ++index) {
        const auto key = static_cast<std::int64_t>(2U * index);
        ASSERT_TRUE(insert_integer(
            relation.value(),
            *pool,
            key,
            heap_rid(static_cast<std::uint32_t>(index + 1U), 1U)
        ));
    }

    constexpr std::size_t k_readers = 3U;
    constexpr std::size_t k_writers = 2U;
    constexpr std::size_t k_odd_keys_per_writer = k_stable_keys / k_writers;
    constexpr std::size_t k_full_scans_per_reader = 80U;
    std::atomic<std::size_t> ready{0U};
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0U};
    std::vector<std::thread> workers;
    workers.reserve(k_readers + k_writers);
    for (std::size_t reader = 0U; reader < k_readers; ++reader) {
        workers.emplace_back([&] {
            wait_for_concurrent_start(ready, start);
            for (std::size_t iteration = 0U; iteration < k_full_scans_per_reader; ++iteration) {
                auto scan = BtreeScan::for_full(relation.value(), {});
                if (!scan) {
                    record_concurrent_failure(failures);
                    return;
                }
                auto matches = collect_forward(scan.value(), *pool);
                if (!matches || matches->empty() || scan->leaf_pages_visited() < 2U) {
                    record_concurrent_failure(failures);
                    return;
                }
            }
        });
    }
    for (std::size_t writer = 0U; writer < k_writers; ++writer) {
        workers.emplace_back([&, writer] {
            wait_for_concurrent_start(ready, start);
            for (std::size_t offset = 0U; offset < k_odd_keys_per_writer; ++offset) {
                const auto index = writer * k_odd_keys_per_writer + offset;
                const auto key = static_cast<std::int64_t>(2U * index + 1U);
                if (!insert_integer(
                        relation.value(),
                        *pool,
                        key,
                        heap_rid(static_cast<std::uint32_t>(500U + index), 1U)
                    )) {
                    record_concurrent_failure(failures);
                    return;
                }
            }
        });
    }
    release_concurrent_workers(k_readers + k_writers, ready, start);
    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0U);
    assert_tree_structure(*pool, relation.value());
    auto full_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(full_scan) << full_scan.error().to_string();
    auto observed = collect_forward(full_scan.value(), *pool);
    ASSERT_TRUE(observed) << observed.error().to_string();
    std::vector<base::Rid> expected;
    expected.reserve(2U * k_stable_keys);
    for (std::size_t index = 0U; index < k_stable_keys; ++index) {
        expected.push_back(heap_rid(static_cast<std::uint32_t>(index + 1U), 1U));
        expected.push_back(heap_rid(static_cast<std::uint32_t>(500U + index), 1U));
    }
    auto sorted_observed = observed.value();
    std::sort(sorted_observed.begin(), sorted_observed.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(sorted_observed, expected);
}

}  // namespace
}  // namespace db::access
