#include "btree_insert.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/base/lab_missing.hpp"
#include "db/storage/page.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/xid.hpp"

#include "btree_key_internal.hpp"
#include "btree_logging.hpp"
#include "btree_page_builder.hpp"
#include "btree_page_read_view.hpp"

namespace db::access::detail {
namespace {

constexpr std::size_t kBtreeUsableBytes =
    storage::kPageSize - storage::kPageHeaderSize - kBtreePageOpaqueSize;
constexpr std::size_t kRightmostLeafTargetBytes = kBtreeUsableBytes * 90U / 100U;
constexpr std::size_t kRightmostInternalTargetBytes = kBtreeUsableBytes * 70U / 100U;

constexpr base::ExceptionBoundaryPolicy kTidClassifierExceptionPolicy{
    .component = {},
    .allocation_message = "unique-index TID classifier exhausted available memory",
    .exception_prefix = "unique-index TID classifier threw an exception: ",
    .unknown_exception_message = "unique-index TID classifier threw a non-standard exception",
};

constexpr base::ExceptionBoundaryPolicy kConflictWaiterExceptionPolicy{
    .component = {},
    .allocation_message = "unique-index conflict waiter exhausted available memory",
    .exception_prefix = "unique-index conflict waiter threw an exception: ",
    .unknown_exception_message = "unique-index conflict waiter threw a non-standard exception",
};

struct LeafEntry final {
    EncodedBtreeKey key;
    base::Rid heap_rid{base::kInvalidRid};
};

struct LeafState final {
    std::vector<LeafEntry> entries;
    std::optional<EncodedBtreeKey> high_key;
    BtreePageLinks links;
    bool is_root{false};
    bool incomplete_split{false};
};

struct InternalState final {
    std::vector<base::PageId> children;
    std::vector<EncodedBtreeKey> separators;
    std::optional<EncodedBtreeKey> high_key;
    BtreePageLinks links;
    std::uint16_t level{0};
    bool is_root{false};
    bool incomplete_split{false};
};

struct SplitResult final {
    EncodedBtreeKey separator;
    base::PageId left_page_id{base::kInvalidPageId};
    base::PageId right_page_id{base::kInvalidPageId};
    std::uint16_t level{0};
    std::optional<storage::Page> root_left_before;
    std::optional<storage::Page> root_right_before;
};

struct InsertOperation final {
    base::RelationId relation_id{base::kInvalidRelationId};
    const BtreeKeyLayout* key_layout{nullptr};
    storage::BufferPool* pool{nullptr};
    const EncodedBtreeKey* key{nullptr};
    base::Rid heap_rid{base::kInvalidRid};
    BtreeUniqueCheck unique_check;
    BtreeMutationLogPort* mutation_log{nullptr};
    base::Xid writer_xid{base::kInvalidXid};
    transaction::SerializableConflictManager* serializable_conflicts{nullptr};
    bool serializable_writer{false};
    std::optional<base::PageId> checked_previous;
};

struct HeldWritePage final {
    HeldWritePage(const base::PageId page_id_value, storage::PageWriteGuard write_guard) noexcept
        : page_id{page_id_value},
          guard{std::move(write_guard)} {}

    base::PageId page_id{base::kInvalidPageId};
    storage::PageWriteGuard guard;
};

struct WriteLeafTarget final {
    std::unique_ptr<HeldWritePage> page;
    BtreePage view;
    BtreePageReadView read_view;
};

struct RootRead final {
    BtreeRoot root;
    storage::PageReadGuard guard;
    BtreePageReadView page;
};

class PageMutationRollback final {
public:
    PageMutationRollback() = default;
    PageMutationRollback(const PageMutationRollback&) = delete;
    auto operator=(const PageMutationRollback&) -> PageMutationRollback& = delete;
    PageMutationRollback(PageMutationRollback&&) = delete;
    auto operator=(PageMutationRollback&&) -> PageMutationRollback& = delete;

    [[maybe_unused]] void add(storage::Page& page, const storage::Page& before) noexcept {
        for (auto& entry : pages_) {
            if (entry.page == nullptr) {
                entry = Entry{.page = &page, .before = &before};
                return;
            }
        }
        std::unreachable();
    }

    [[maybe_unused]] void release() noexcept {
        active_ = false;
    }

    ~PageMutationRollback() {
        if (active_) {
            for (const auto entry : pages_) {
                if (entry.page != nullptr) {
                    *entry.page = *entry.before;
                }
            }
        }
    }

private:
    struct Entry final {
        storage::Page* page{nullptr};
        const storage::Page* before{nullptr};
    };

    // Each publication phase tracks at most three pages. Borrowed before-images
    // avoid allocation after any page is mutated.
    std::array<Entry, 3U> pages_{};
    bool active_{true};
};

struct ParentHint final {
    base::PageId page_id{base::kInvalidPageId};
    std::size_t child_index{0U};
};

struct WriteDescent final {
    BtreeRoot observed_root;
    std::vector<ParentHint> parents;
    base::PageId leaf_page_id{base::kInvalidPageId};
    storage::PinGuard leaf_pin;
};

using InsertDescentPass = std::variant<WriteDescent, RootRead>;

enum class ReadDescentKind : std::uint8_t {
    kMoveRight,
    kLeaf,
    kChild,
};

struct ReadDescentStep final {
    ReadDescentKind kind{ReadDescentKind::kLeaf};
    base::PageId next_page_id{base::kInvalidPageId};
    std::size_t child_index{0U};
};

struct SiblingLinkUpdate final {
    std::optional<base::PageId> old_next;
    base::PageId new_previous{base::kInvalidPageId};
};

struct SiblingRewrite final {
    std::unique_ptr<HeldWritePage> page;
    storage::Page before;
};

struct SplitChildRewrite final {
    HeldWritePage* page{nullptr};
    storage::Page before;
};

enum class InsertAttemptKind : std::uint8_t {
    kInserted,
    kWaitFor,
    kCheckPrevious,
    kRestart,
};

struct InsertAttemptOutcome final {
    InsertAttemptKind kind{InsertAttemptKind::kInserted};
    BtreeRoot observed_root;
    base::Xid blocking_xid{base::kInvalidXid};
    base::PageId previous_page_id{base::kInvalidPageId};
};

struct UniqueLeafProbe final {
    std::optional<InsertAttemptOutcome> outcome;
    std::optional<base::Xid> blocking_xid;
    std::vector<std::size_t> dead_entries;
    std::size_t first_data_slot{0U};
    std::size_t logical_insertion_slot{0U};
};

struct LeafInsertPlan final {
    std::optional<InsertAttemptOutcome> outcome;
    std::optional<UniqueLeafProbe> unique_probe;
    std::optional<std::size_t> physical_insertion_slot;
    std::size_t logical_insertion_slot{0U};
};

auto finish_incomplete_split(
    const InsertOperation& operation,
    std::unique_ptr<HeldWritePage> split_left,
    std::vector<ParentHint> parents
) -> base::Result<BtreeRoot>;

[[maybe_unused]] auto insert_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view detail
) -> base::Error {
    return {code, "B-tree insert " + std::string{operation} + ": " + std::string{detail}};
}

[[maybe_unused]] auto
page_address(const base::RelationId relation_id, const base::PageId page_id) noexcept
    -> storage::PageAddress {
    return storage::PageAddress{
        .relation_id = relation_id,
        .page_id = page_id,
    };
}

[[maybe_unused]] auto key_bytes(const EncodedBtreeKey& key) noexcept -> base::Bytes {
    return base::Bytes{key.data(), key.size()};
}

template <typename Value>
[[maybe_unused]] auto move_slice(
    [[maybe_unused]] std::vector<Value>& values,
    [[maybe_unused]] const std::size_t first,
    [[maybe_unused]] const std::size_t last
) -> std::vector<Value> {
    // LAB-S03.Q03:R04-BEGIN
    base::lab::missing_algorithm("S03.Q03:R04");
    // LAB-S03.Q03:R04-END
}

[[maybe_unused]] auto first_data_slot([[maybe_unused]] const BtreePage& page) noexcept
    -> std::size_t {
    // LAB-S03.Q03:R05-BEGIN
    base::lab::missing_algorithm("S03.Q03:R05");
    // LAB-S03.Q03:R05-END
}

[[maybe_unused]] auto first_data_slot([[maybe_unused]] const BtreePageReadView& page) noexcept
    -> std::size_t {
    // LAB-S03.Q03:R06-BEGIN
    base::lab::missing_algorithm("S03.Q03:R06");
    // LAB-S03.Q03:R06-END
}

[[maybe_unused]] auto physical_bytes([[maybe_unused]] const std::size_t key_size) noexcept
    -> std::size_t {
    // LAB-S03.Q03:R07-BEGIN
    base::lab::missing_algorithm("S03.Q03:R07");
    // LAB-S03.Q03:R07-END
}

[[maybe_unused]] auto
high_key_bytes([[maybe_unused]] const std::optional<EncodedBtreeKey>& high_key) noexcept
    -> std::size_t {
    // LAB-S03.Q03:R08-BEGIN
    base::lab::missing_algorithm("S03.Q03:R08");
    // LAB-S03.Q03:R08-END
}

[[maybe_unused]] auto leaf_bytes([[maybe_unused]] const LeafState& state) noexcept -> std::size_t {
    // LAB-S03.Q03:R09-BEGIN
    base::lab::missing_algorithm("S03.Q03:R09");
    // LAB-S03.Q03:R09-END
}

[[maybe_unused]] auto leaf_fits([[maybe_unused]] const LeafState& state) noexcept -> bool {
    // LAB-S03.Q03:R10-BEGIN
    base::lab::missing_algorithm("S03.Q03:R10");
    // LAB-S03.Q03:R10-END
}

[[maybe_unused]] auto read_leaf_state([[maybe_unused]] const BtreePageReadView& page) -> LeafState {
    // LAB-S03.Q03:R11-BEGIN
    base::lab::missing_algorithm("S03.Q03:R11");
    // LAB-S03.Q03:R11-END
}

[[maybe_unused]] auto read_leaf_state(
    [[maybe_unused]] const storage::Page& storage_page,
    [[maybe_unused]] const storage::PageAddress address,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) -> LeafState {
    // LAB-S03.Q03:R12-BEGIN
    base::lab::missing_algorithm("S03.Q03:R12");
    // LAB-S03.Q03:R12-END
}

[[maybe_unused]] auto read_internal_state([[maybe_unused]] const BtreePage& page) -> InternalState {
    // LAB-S03.Q03:R13-BEGIN
    base::lab::missing_algorithm("S03.Q03:R13");
    // LAB-S03.Q03:R13-END
}

[[maybe_unused]] auto read_internal_state(
    [[maybe_unused]] const storage::Page& storage_page,
    [[maybe_unused]] const storage::PageAddress address,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) -> InternalState {
    // LAB-S03.Q03:R14-BEGIN
    base::lab::missing_algorithm("S03.Q03:R14");
    // LAB-S03.Q03:R14-END
}

struct InternalChildSelection final {
    base::PageId page_id{base::kInvalidPageId};
    std::size_t child_index{0U};
};

[[maybe_unused]] auto choose_insert_child(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const EncodedBtreeKey& key,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) noexcept -> InternalChildSelection {
    // LAB-S03.Q02:R01-BEGIN
    base::lab::missing_algorithm("S03.Q02:R01");
    // LAB-S03.Q02:R01-END
}

[[maybe_unused]] auto find_internal_child_index(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const base::PageId child_page_id
) noexcept -> std::optional<std::size_t> {
    // LAB-S03.Q02:R02-BEGIN
    base::lab::missing_algorithm("S03.Q02:R02");
    // LAB-S03.Q02:R02-END
}

[[maybe_unused]] auto resolve_internal_child_index(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const base::PageId child_page_id,
    [[maybe_unused]] const std::optional<std::size_t> hinted_index
) noexcept -> std::optional<std::size_t> {
    // LAB-S03.Q02:R03-BEGIN
    base::lab::missing_algorithm("S03.Q02:R03");
    // LAB-S03.Q02:R03-END
}

[[maybe_unused]] auto choose_leaf_insert_slot(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const EncodedBtreeKey& key,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) noexcept -> std::size_t {
    // LAB-S03.Q02:R04-BEGIN
    base::lab::missing_algorithm("S03.Q02:R04");
    // LAB-S03.Q02:R04-END
}

[[maybe_unused]] auto duplicate_key_error() -> base::Error {
    return base::Error{
        base::ErrorCode::kAlreadyExists,
        "duplicate key violates unique constraint",
    }
        .with_context("access.btree", "check unique key");
}

[[maybe_unused]] auto probe_equal_rid(
    [[maybe_unused]] const base::Rid heap_rid,
    [[maybe_unused]] const BtreeUniqueCheck unique_check
) -> base::Result<IndexTidState> {
    // LAB-S03.Q06:R02-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q06:R02"});
    // LAB-S03.Q06:R02-END
}

[[maybe_unused]] auto leaf_starts_with_equal_key(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const EncodedBtreeKey& key,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) noexcept -> bool {
    // LAB-S03.Q06:R03-BEGIN
    base::lab::missing_algorithm("S03.Q06:R03");
    // LAB-S03.Q06:R03-END
}

[[maybe_unused]] auto probe_leaf_unique(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const EncodedBtreeKey& key,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const BtreeUniqueCheck unique_check
) -> base::Result<UniqueLeafProbe> {
    // LAB-S03.Q06:R04-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q06:R04"});
    // LAB-S03.Q06:R04-END
}

[[maybe_unused]] auto page_move_right_target(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const EncodedBtreeKey& key,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) noexcept -> std::optional<base::PageId> {
    // LAB-S03.Q02:R05-BEGIN
    base::lab::missing_algorithm("S03.Q02:R05");
    // LAB-S03.Q02:R05-END
}

[[maybe_unused]] auto write_leaf_state(
    [[maybe_unused]] storage::Page& storage_page,
    [[maybe_unused]] const storage::PageAddress address,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const LeafState& state
) -> base::Status {
    // LAB-S03.Q03:R15-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R15"});
    // LAB-S03.Q03:R15-END
}

[[maybe_unused]] auto write_internal_state(
    [[maybe_unused]] storage::Page& storage_page,
    [[maybe_unused]] const storage::PageAddress address,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const InternalState& state
) -> base::Status {
    // LAB-S03.Q03:R16-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R16"});
    // LAB-S03.Q03:R16-END
}

[[maybe_unused]] auto acquire_write_page(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const base::PageId page_id
) -> base::Result<std::unique_ptr<HeldWritePage>> {
    // LAB-S03.Q03:R17-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R17"});
    // LAB-S03.Q03:R17-END
}

[[maybe_unused]] auto allocate_write_page(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const base::RelationId relation_id
) -> base::Result<std::unique_ptr<HeldWritePage>> {
    // LAB-S03.Q03:R18-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R18"});
    // LAB-S03.Q03:R18-END
}

[[maybe_unused]] auto read_metapage_root(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) -> base::Result<std::optional<BtreeRoot>> {
    // LAB-S03.Q02:R06-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R06"});
    // LAB-S03.Q02:R06-END
}

[[maybe_unused]] auto create_first_root_if_absent(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] BtreeMutationLogPort& mutation_log,
    [[maybe_unused]] const base::Xid writer_xid
) -> base::Result<std::optional<BtreeRoot>> {
    // LAB-S03.Q03:R19-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R19"});
    // LAB-S03.Q03:R19-END
}

[[maybe_unused]] auto acquire_root_read(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const BtreeRoot candidate
) -> base::Result<RootRead> {
    // LAB-S03.Q02:R07-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R07"});
    // LAB-S03.Q02:R07-END
}

[[maybe_unused]] auto acquire_insert_root(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] std::optional<BtreeRoot> hint,
    [[maybe_unused]] BtreeMutationLogPort& mutation_log,
    [[maybe_unused]] const base::Xid writer_xid
) -> base::Result<RootRead> {
    // LAB-S03.Q02:R08-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R08"});
    // LAB-S03.Q02:R08-END
}

[[maybe_unused]] auto help_incomplete_page_and_reacquire_root(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] const base::PageId page_id,
    [[maybe_unused]] const BtreeRoot observed_root,
    [[maybe_unused]] std::vector<ParentHint> parents
) -> base::Result<RootRead> {
    // LAB-S03.Q03:R20-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R20"});
    // LAB-S03.Q03:R20-END
}

[[maybe_unused]] auto inspect_leaf_for_insert(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const EncodedBtreeKey& key
) noexcept -> ReadDescentStep {
    // LAB-S03.Q02:R09-BEGIN
    base::lab::missing_algorithm("S03.Q02:R09");
    // LAB-S03.Q02:R09-END
}

[[maybe_unused]] auto inspect_internal_for_insert(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const EncodedBtreeKey& key
) noexcept -> ReadDescentStep {
    // LAB-S03.Q02:R10-BEGIN
    base::lab::missing_algorithm("S03.Q02:R10");
    // LAB-S03.Q02:R10-END
}

[[maybe_unused]] auto inspect_read_descent_page(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const EncodedBtreeKey& key
) noexcept -> ReadDescentStep {
    // LAB-S03.Q02:R11-BEGIN
    base::lab::missing_algorithm("S03.Q02:R11");
    // LAB-S03.Q02:R11-END
}

[[maybe_unused]] auto descend_insert_pass(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] RootRead root
) -> base::Result<InsertDescentPass> {
    // LAB-S03.Q02:R12-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R12"});
    // LAB-S03.Q02:R12-END
}

[[maybe_unused]] auto descend_for_insert(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] RootRead root
) -> base::Result<WriteDescent> {
    // LAB-S03.Q02:R13-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R13"});
    // LAB-S03.Q02:R13-END
}

[[maybe_unused]] auto byte_distance(
    [[maybe_unused]] const std::size_t left,
    [[maybe_unused]] const std::size_t right
) noexcept -> std::size_t {
    // LAB-S03.Q03:R21-BEGIN
    base::lab::missing_algorithm("S03.Q03:R21");
    // LAB-S03.Q03:R21-END
}

[[maybe_unused]] auto find_leaf_split(
    [[maybe_unused]] const LeafState& state,
    [[maybe_unused]] const bool prefer_left_fill
) -> base::Result<std::size_t> {
    // LAB-S03.Q03:R22-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R22"});
    // LAB-S03.Q03:R22-END
}

[[maybe_unused]] auto find_internal_split([[maybe_unused]] const InternalState& state)
    -> base::Result<std::size_t> {
    // LAB-S03.Q03:R23-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R23"});
    // LAB-S03.Q03:R23-END
}

[[maybe_unused]] auto update_next_previous_link(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const SiblingLinkUpdate& update
) -> base::Result<std::optional<SiblingRewrite>> {
    // LAB-S03.Q03:R24-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R24"});
    // LAB-S03.Q03:R24-END
}

[[maybe_unused]] auto log_split_pages(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] HeldWritePage& left_page,
    [[maybe_unused]] const storage::Page& left_before,
    [[maybe_unused]] HeldWritePage& right_page,
    [[maybe_unused]] const storage::Page& right_before,
    [[maybe_unused]] const std::optional<SiblingRewrite>& neighbor,
    [[maybe_unused]] const std::optional<SplitChildRewrite>& split_child = std::nullopt
) -> base::Status {
    // LAB-S10.Q06:R01-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q06:R01"});
    // LAB-S10.Q06:R01-END
}

[[maybe_unused]] auto split_leaf(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] HeldWritePage& left_page,
    [[maybe_unused]] LeafState state,
    [[maybe_unused]] const bool prefer_left_fill
) -> base::Result<SplitResult> {
    // LAB-S03.Q03:R25-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R25"});
    // LAB-S03.Q03:R25-END
}

[[maybe_unused]] auto split_internal(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] HeldWritePage& left_page,
    [[maybe_unused]] InternalState state,
    [[maybe_unused]] HeldWritePage& split_child
) -> base::Result<SplitResult> {
    // LAB-S03.Q03:R26-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R26"});
    // LAB-S03.Q03:R26-END
}

[[maybe_unused]] auto install_new_root_pages(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] const BtreeRoot old_root,
    [[maybe_unused]] const SplitResult& split,
    [[maybe_unused]] HeldWritePage& new_root,
    [[maybe_unused]] storage::PageWriteGuard& meta_write,
    [[maybe_unused]] HeldWritePage* const split_child
) -> base::Result<BtreeRoot> {
    // LAB-S03.Q03:R27-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R27"});
    // LAB-S03.Q03:R27-END
}

[[maybe_unused]] auto publish_new_root(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] const BtreeRoot old_root,
    [[maybe_unused]] HeldWritePage& old_root_page,
    [[maybe_unused]] const SplitResult& split,
    [[maybe_unused]] HeldWritePage* const split_child
) -> base::Result<BtreeRoot> {
    // LAB-S03.Q03:R28-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R28"});
    // LAB-S03.Q03:R28-END
}

struct LocatedSplitParent final {
    BtreeRoot root;
    ParentHint parent;
};

[[maybe_unused]] auto acquire_parent_search_root([[maybe_unused]] const InsertOperation& operation)
    -> base::Result<RootRead> {
    // LAB-S03.Q03:R29-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R29"});
    // LAB-S03.Q03:R29-END
}

[[maybe_unused]] auto locate_split_parent(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] const SplitResult& pending
) -> base::Result<LocatedSplitParent> {
    // LAB-S03.Q03:R30-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R30"});
    // LAB-S03.Q03:R30-END
}

struct WritableSplitParent final {
    std::unique_ptr<HeldWritePage> page;
    std::size_t child_index{0U};
};

[[maybe_unused]] auto try_acquire_split_parent(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] ParentHint candidate,
    [[maybe_unused]] const SplitResult& pending,
    [[maybe_unused]] std::vector<ParentHint>& ancestors
) -> base::Result<std::optional<WritableSplitParent>> {
    // LAB-S03.Q03:R31-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R31"});
    // LAB-S03.Q03:R31-END
}

[[maybe_unused]] auto publish_pending_root(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] HeldWritePage& split_left,
    [[maybe_unused]] const SplitResult& pending,
    [[maybe_unused]] HeldWritePage* const split_child = nullptr
) -> base::Result<BtreeRoot> {
    // LAB-S03.Q03:R32-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R32"});
    // LAB-S03.Q03:R32-END
}

struct AcquiredSplitParent final {
    BtreeRoot root;
    std::unique_ptr<HeldWritePage> page;
    std::size_t child_index{0U};
};

[[maybe_unused]] auto acquire_propagation_parent(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] std::vector<ParentHint>& parents,
    [[maybe_unused]] const SplitResult& pending,
    [[maybe_unused]] const BtreeRoot known_root
) -> base::Result<AcquiredSplitParent> {
    // LAB-S03.Q03:R33-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R33"});
    // LAB-S03.Q03:R33-END
}

[[maybe_unused]] auto insert_split_into_parent(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] HeldWritePage& parent,
    [[maybe_unused]] HeldWritePage& split_child,
    [[maybe_unused]] const SplitResult& pending,
    [[maybe_unused]] const std::size_t child_index
) -> base::Result<std::optional<SplitResult>> {
    // LAB-S03.Q03:R34-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R34"});
    // LAB-S03.Q03:R34-END
}

[[maybe_unused]] auto pending_split_from_page([[maybe_unused]] const HeldWritePage& split_left)
    -> SplitResult {
    // LAB-S03.Q03:R35-BEGIN
    base::lab::missing_algorithm("S03.Q03:R35");
    // LAB-S03.Q03:R35-END
}

[[maybe_unused]] auto matches_pending_split(
    [[maybe_unused]] const HeldWritePage& split_left,
    [[maybe_unused]] const SplitResult& pending
) noexcept -> bool {
    // LAB-S03.Q03:R36-BEGIN
    base::lab::missing_algorithm("S03.Q03:R36");
    // LAB-S03.Q03:R36-END
}

[[maybe_unused]] auto propagate_split(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] std::vector<ParentHint>& parents,
    [[maybe_unused]] BtreeRoot known_root,
    [[maybe_unused]] std::unique_ptr<HeldWritePage> split_left,
    [[maybe_unused]] SplitResult pending
) -> base::Result<BtreeRoot> {
    // LAB-S03.Q03:R37-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R37"});
    // LAB-S03.Q03:R37-END
}

[[maybe_unused]] auto finish_incomplete_split(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] std::unique_ptr<HeldWritePage> split_left,
    [[maybe_unused]] std::vector<ParentHint> parents
) -> base::Result<BtreeRoot> {
    // LAB-S03.Q03:R38-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R38"});
    // LAB-S03.Q03:R38-END
}

[[maybe_unused]] void remove_dead_entries(
    [[maybe_unused]] LeafState& state,
    [[maybe_unused]] const std::vector<std::size_t>& dead_entries
) {
    // LAB-S03.Q03:R39-BEGIN
    base::lab::missing_algorithm("S03.Q03:R39");
    // LAB-S03.Q03:R39-END
}

struct PreviousLeafProbe final {
    bool finished{false};
    std::optional<base::Xid> blocking_xid;
};

[[maybe_unused]] auto probe_previous_leaf(
    [[maybe_unused]] const LeafState& leaf,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const EncodedBtreeKey& key,
    [[maybe_unused]] const BtreeUniqueCheck unique_check
) -> base::Result<PreviousLeafProbe> {
    // LAB-S03.Q06:R05-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q06:R05"});
    // LAB-S03.Q06:R05-END
}

[[maybe_unused]] auto check_previous_unique(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const BtreeKeyLayout& key_layout,
    [[maybe_unused]] const EncodedBtreeKey& key,
    [[maybe_unused]] base::PageId page_id,
    [[maybe_unused]] const BtreeUniqueCheck unique_check
) -> base::Result<std::optional<base::Xid>> {
    // LAB-S03.Q06:R06-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q06:R06"});
    // LAB-S03.Q06:R06-END
}

[[maybe_unused]] auto prepare_unique_leaf(
    [[maybe_unused]] const BtreePageReadView& leaf,
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] const BtreeRoot observed_root
) -> base::Result<UniqueLeafProbe> {
    // LAB-S03.Q06:R07-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q06:R07"});
    // LAB-S03.Q06:R07-END
}

[[maybe_unused]] auto prepare_leaf_insert_plan(
    [[maybe_unused]] const BtreePageReadView& leaf,
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] const BtreeRoot observed_root
) -> base::Result<LeafInsertPlan> {
    // LAB-S03.Q03:R40-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R40"});
    // LAB-S03.Q03:R40-END
}

[[maybe_unused]] auto log_leaf_insert(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] HeldWritePage& leaf_page,
    [[maybe_unused]] const storage::PageAddress leaf_address,
    [[maybe_unused]] const storage::Page& leaf_before,
    [[maybe_unused]] const std::span<const BtreePageChangeRange> changed_ranges = {}
) -> base::Status {
    // LAB-S10.Q06:R02-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q06:R02"});
    // LAB-S10.Q06:R02-END
}

[[maybe_unused]] auto acquire_write_leaf_target(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] WriteDescent& descent
) -> base::Result<WriteLeafTarget> {
    // LAB-S03.Q03:R41-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R41"});
    // LAB-S03.Q03:R41-END
}

[[maybe_unused]] auto try_in_place_leaf_insert(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] WriteLeafTarget& target,
    [[maybe_unused]] const storage::PageAddress leaf_address,
    [[maybe_unused]] const storage::Page& leaf_before,
    [[maybe_unused]] const BtreeRoot observed_root,
    [[maybe_unused]] const std::optional<std::size_t> insertion_slot
) -> base::Result<std::optional<InsertAttemptOutcome>> {
    // LAB-S03.Q03:R42-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R42"});
    // LAB-S03.Q03:R42-END
}

[[maybe_unused]] auto insert_into_leaf(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] WriteDescent descent
) -> base::Result<InsertAttemptOutcome> {
    // LAB-S03.Q03:R43-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R43"});
    // LAB-S03.Q03:R43-END
}

[[maybe_unused]] auto try_insert_btree_entry(
    [[maybe_unused]] const InsertOperation& operation,
    [[maybe_unused]] std::optional<BtreeRoot> root_hint
) -> base::Result<InsertAttemptOutcome> {
    // LAB-S03.Q03:R44-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R44"});
    // LAB-S03.Q03:R44-END
}

}  // namespace

[[maybe_unused]] auto insert_btree_entry([[maybe_unused]] BtreeInsertRequest request)
    -> base::Result<BtreeRoot> {
    // LAB-S03.Q07:R01-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q07:R01"});
    // LAB-S03.Q07:R01-END
}

}  // namespace db::access::detail
