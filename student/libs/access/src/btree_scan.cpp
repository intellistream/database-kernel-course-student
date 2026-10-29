#include "db/access/btree_scan.hpp"

#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/access/btree_page.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/storage/page.hpp"

#include "btree_concurrency.hpp"
#include "btree_key_internal.hpp"
#include "btree_page_read_view.hpp"

namespace db::access {
namespace {

enum class ScanDirection : std::uint8_t {
    kUndecided,
    kForward,
    kBackward,
};

struct EncodedBound final {
    EncodedBtreeKey key;
    std::size_t columns{0U};
    bool has_key{false};
    bool exclusive{false};
    std::uint64_t ordered_payload{0U};
    std::uint8_t tag{0U};
    std::uint8_t fast_payload_width{0U};
};

struct ScanBounds final {
    EncodedBound lower;
    EncodedBound upper;
    bool common_equality{false};

    [[maybe_unused]] auto upper_bound() const noexcept -> const EncodedBound& {
        // LAB-S03.Q04:R01-BEGIN
        base::lab::missing_algorithm("S03.Q04:R01");
        // LAB-S03.Q04:R01-END
    }
};

[[maybe_unused]] auto scan_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view detail
) -> base::Error {
    return {code, "B-tree scan " + std::string{operation} + ": " + std::string{detail}};
}

[[maybe_unused]] auto inactive_scan(const std::string_view operation) -> base::Error {
    return scan_error(base::ErrorCode::kUnavailable, operation, "scan is inactive");
}

[[maybe_unused]] auto
page_address(const base::RelationId relation_id, const base::PageId page_id) noexcept
    -> storage::PageAddress {
    return storage::PageAddress{
        .relation_id = relation_id,
        .page_id = page_id,
    };
}

[[maybe_unused]] auto first_data_slot([[maybe_unused]] const BtreePageReadView& page) noexcept
    -> std::size_t {
    // LAB-S03.Q04:R02-BEGIN
    base::lab::missing_algorithm("S03.Q04:R02");
    // LAB-S03.Q04:R02-END
}

[[maybe_unused]] auto load_ordered_u32(const std::byte* const bytes) noexcept -> std::uint32_t {
    std::uint32_t value = 0U;
    std::memcpy(&value, bytes, sizeof(value));
    if constexpr (std::endian::native == std::endian::little) {
        return __builtin_bswap32(value);
    }
    return value;
}

[[maybe_unused]] auto load_ordered_u64(const std::byte* const bytes) noexcept -> std::uint64_t {
    std::uint64_t value = 0U;
    std::memcpy(&value, bytes, sizeof(value));
    if constexpr (std::endian::native == std::endian::little) {
        return __builtin_bswap64(value);
    }
    return value;
}

[[maybe_unused]] void prepare_fast_bound(
    [[maybe_unused]] const BtreeKeyLayout& layout,
    [[maybe_unused]] EncodedBound& bound
) noexcept {
    // LAB-S03.Q04:R03-BEGIN
    base::lab::missing_algorithm("S03.Q04:R03");
    // LAB-S03.Q04:R03-END
}

[[maybe_unused]] auto encode_bound(
    [[maybe_unused]] const BtreeKeyLayout& layout,
    [[maybe_unused]] const std::span<const types::Value> values,
    [[maybe_unused]] const bool exclusive,
    [[maybe_unused]] const std::string_view name
) -> base::Result<EncodedBound> {
    // LAB-S03.Q04:R04-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R04"});
    // LAB-S03.Q04:R04-END
}

[[maybe_unused]] auto encode_eq_range_bound(
    [[maybe_unused]] const BtreeKeyLayout& layout,
    [[maybe_unused]] const std::span<const types::Value> equality_prefix,
    [[maybe_unused]] const std::span<const types::Value> range_values,
    [[maybe_unused]] const bool exclusive,
    [[maybe_unused]] const std::string_view name
) -> base::Result<EncodedBound> {
    // LAB-S03.Q04:R05-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R05"});
    // LAB-S03.Q04:R05-END
}

[[maybe_unused]] auto equality_bounds(
    [[maybe_unused]] const BtreeKeyLayout& layout,
    [[maybe_unused]] const std::span<const types::Value> values
) -> base::Result<ScanBounds> {
    // LAB-S03.Q04:R06-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R06"});
    // LAB-S03.Q04:R06-END
}

[[maybe_unused]] auto range_bounds(
    [[maybe_unused]] const BtreeKeyLayout& layout,
    [[maybe_unused]] const BtreeScanRange range
) -> base::Result<ScanBounds> {
    // LAB-S03.Q04:R07-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R07"});
    // LAB-S03.Q04:R07-END
}

[[maybe_unused]] auto equality_range_bounds(
    [[maybe_unused]] const BtreeKeyLayout& layout,
    [[maybe_unused]] const std::span<const types::Value> equality_prefix,
    [[maybe_unused]] const BtreeScanRange range
) -> base::Result<ScanBounds> {
    // LAB-S03.Q04:R08-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R08"});
    // LAB-S03.Q04:R08-END
}

[[maybe_unused]] auto compare_stored_to_bound(
    [[maybe_unused]] const BtreeKeyLayout& layout,
    [[maybe_unused]] const base::Bytes stored,
    [[maybe_unused]] const EncodedBound& bound
) noexcept -> std::strong_ordering {
    // LAB-S03.Q04:R09-BEGIN
    base::lab::missing_algorithm("S03.Q04:R09");
    // LAB-S03.Q04:R09-END
}

[[maybe_unused]] auto compare_bound_to_stored(
    [[maybe_unused]] const BtreeKeyLayout& layout,
    [[maybe_unused]] const EncodedBound& bound,
    [[maybe_unused]] const base::Bytes stored
) noexcept -> std::strong_ordering {
    // LAB-S03.Q04:R10-BEGIN
    base::lab::missing_algorithm("S03.Q04:R10");
    // LAB-S03.Q04:R10-END
}

[[maybe_unused]] auto forward_start_slot(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const std::size_t first,
    [[maybe_unused]] const EncodedBound& lower,
    [[maybe_unused]] const BtreeKeyLayout& layout
) noexcept -> std::size_t {
    // LAB-S03.Q02:R15-BEGIN
    base::lab::missing_algorithm("S03.Q02:R15");
    // LAB-S03.Q02:R15-END
}

[[maybe_unused]] auto backward_start_slot(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const std::size_t first,
    [[maybe_unused]] const EncodedBound& upper,
    [[maybe_unused]] const BtreeKeyLayout& layout
) noexcept -> std::size_t {
    // LAB-S03.Q02:R16-BEGIN
    base::lab::missing_algorithm("S03.Q02:R16");
    // LAB-S03.Q02:R16-END
}

[[maybe_unused]] auto choose_child_index(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const std::size_t first,
    [[maybe_unused]] const EncodedBound& positioning_bound,
    [[maybe_unused]] const ScanDirection direction,
    [[maybe_unused]] const BtreeKeyLayout& layout
) noexcept -> std::size_t {
    // LAB-S03.Q02:R17-BEGIN
    base::lab::missing_algorithm("S03.Q02:R17");
    // LAB-S03.Q02:R17-END
}

[[maybe_unused]] auto child_page_at(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const std::size_t first,
    [[maybe_unused]] const std::size_t child_index
) -> base::PageId {
    // LAB-S03.Q02:R18-BEGIN
    base::lab::missing_algorithm("S03.Q02:R18");
    // LAB-S03.Q02:R18-END
}

[[maybe_unused]] auto move_right_for_position(
    [[maybe_unused]] const BtreePageReadView& page,
    [[maybe_unused]] const EncodedBound& positioning_bound,
    [[maybe_unused]] const ScanDirection direction,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) noexcept -> base::PageId {
    // LAB-S03.Q02:R19-BEGIN
    base::lab::missing_algorithm("S03.Q02:R19");
    // LAB-S03.Q02:R19-END
}

struct RootRead final {
    BtreeRoot root;
    storage::PageReadGuard guard;
    BtreePageReadView page;
};

struct ReadDescentStep final {
    std::optional<base::PageId> right_sibling;
    std::optional<base::PageId> child;
    bool is_leaf{false};
};

}  // namespace

struct BtreeScan::Impl final {
    Impl(
        const base::RelationId relation_id,
        const BtreeKeyLayout& key_layout,
        ScanBounds bounds,
        const BtreeScanContext context,
        std::shared_ptr<BtreeRelation::State> relation_state
    )
        : relation_id_{relation_id},
          key_layout_{&key_layout},
          bounds_{std::move(bounds)},
          relation_state_{std::move(relation_state)},
          root_hint_{relation_state_->root_hint()},
          own_xid_{context.own_xid},
          isolation_{context.isolation},
          serializable_conflicts_{context.serializable_conflicts} {}

    void reset_position() noexcept {
        direction_ = ScanDirection::kUndecided;
        positioned_ = false;
        done_ = false;
        current_leaf_ = base::kInvalidPageId;
        next_leaf_.reset();
        clear_candidates();
        leaf_pages_visited_ = 0U;
        leaf_items_examined_ = 0U;
    }

    [[maybe_unused]] void rebind([[maybe_unused]] ScanBounds bounds) noexcept {
        // LAB-S03.Q04:R11-BEGIN
        base::lab::missing_algorithm("S03.Q04:R11");
        // LAB-S03.Q04:R11-END
    }

    [[maybe_unused]] auto address([[maybe_unused]] const base::PageId page_id) const noexcept
        -> storage::PageAddress {
        // LAB-S03.Q04:R12-BEGIN
        base::lab::missing_algorithm("S03.Q04:R12");
        // LAB-S03.Q04:R12-END
    }

    [[maybe_unused]] auto choose_direction([[maybe_unused]] const ScanDirection requested)
        -> base::Status {
        // LAB-S03.Q04:R13-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R13"});
        // LAB-S03.Q04:R13-END
    }

    auto record_page_siread(const base::PageId page_id) const -> base::Status {
        if (isolation_ != transaction::IsolationLevel::kSerializable
            || serializable_conflicts_ == nullptr) {
            return {};
        }
        return serializable_conflicts_->record_read(
            own_xid_,
            transaction::SsiReadRecord{
                .target = transaction::SsiReadTarget::for_page(relation_id_, page_id),
            }
        );
    }

    auto record_relation_siread() const -> base::Status {
        if (isolation_ != transaction::IsolationLevel::kSerializable
            || serializable_conflicts_ == nullptr) {
            return {};
        }
        return serializable_conflicts_->record_read(
            own_xid_,
            transaction::SsiReadRecord{
                .target = transaction::SsiReadTarget::for_relation(relation_id_),
            }
        );
    }

    [[maybe_unused]] auto read_metapage_root([[maybe_unused]] storage::BufferPool& pool) const
        -> base::Result<std::optional<BtreeRoot>> {
        // LAB-S03.Q02:R20-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R20"});
        // LAB-S03.Q02:R20-END
    }

    [[maybe_unused]] auto try_root_hint([[maybe_unused]] storage::BufferPool& pool)
        -> base::Result<std::optional<RootRead>> {
        // LAB-S03.Q02:R21-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R21"});
        // LAB-S03.Q02:R21-END
    }

    [[maybe_unused]] auto read_root([[maybe_unused]] storage::BufferPool& pool)
        -> base::Result<std::optional<RootRead>> {
        // LAB-S03.Q02:R22-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R22"});
        // LAB-S03.Q02:R22-END
    }

    [[maybe_unused]] auto positioning_bound() const noexcept -> const EncodedBound& {
        // LAB-S03.Q02:R23-BEGIN
        base::lab::missing_algorithm("S03.Q02:R23");
        // LAB-S03.Q02:R23-END
    }

    [[maybe_unused]] auto
    inspect_read_descent_page([[maybe_unused]] const BtreePageReadView& page) const noexcept
        -> ReadDescentStep {
        // LAB-S03.Q02:R24-BEGIN
        base::lab::missing_algorithm("S03.Q02:R24");
        // LAB-S03.Q02:R24-END
    }

    [[maybe_unused]] auto replace_descent_page(
        [[maybe_unused]] storage::BufferPool& pool,
        [[maybe_unused]] const base::PageId next_page_id,
        [[maybe_unused]] storage::PageReadGuard& current,
        [[maybe_unused]] BtreePageReadView& current_page
    ) const -> base::Status {
        // LAB-S03.Q02:R25-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R25"});
        // LAB-S03.Q02:R25-END
    }

    [[maybe_unused]] auto move_descent_right(
        [[maybe_unused]] storage::BufferPool& pool,
        [[maybe_unused]] const base::PageId next_page_id,
        [[maybe_unused]] base::PageId& page_id,
        [[maybe_unused]] storage::PageReadGuard& current,
        [[maybe_unused]] BtreePageReadView& current_page
    ) const -> base::Status {
        // LAB-S03.Q02:R26-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R26"});
        // LAB-S03.Q02:R26-END
    }

    [[maybe_unused]] auto move_descent_down(
        [[maybe_unused]] storage::BufferPool& pool,
        [[maybe_unused]] const ReadDescentStep& step,
        [[maybe_unused]] base::PageId& page_id,
        [[maybe_unused]] storage::PageReadGuard& current,
        [[maybe_unused]] BtreePageReadView& current_page
    ) const -> base::Status {
        // LAB-S03.Q02:R27-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R27"});
        // LAB-S03.Q02:R27-END
    }

    [[maybe_unused]] auto descend_to_initial_leaf([[maybe_unused]] storage::BufferPool& pool)
        -> base::Status {
        // LAB-S03.Q02:R28-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R28"});
        // LAB-S03.Q02:R28-END
    }

    [[maybe_unused]] auto before_lower(
        [[maybe_unused]] const base::Bytes stored,
        [[maybe_unused]] const EncodedBound& lower
    ) const noexcept -> bool {
        // LAB-S03.Q04:R14-BEGIN
        base::lab::missing_algorithm("S03.Q04:R14");
        // LAB-S03.Q04:R14-END
    }

    [[maybe_unused]] auto after_upper(
        [[maybe_unused]] const base::Bytes stored,
        [[maybe_unused]] const EncodedBound& upper
    ) const noexcept -> bool {
        // LAB-S03.Q04:R15-BEGIN
        base::lab::missing_algorithm("S03.Q04:R15");
        // LAB-S03.Q04:R15-END
    }

    [[maybe_unused]] auto append_forward_unbounded(
        [[maybe_unused]] const BtreePageReadView& page,
        [[maybe_unused]] const BtreePageLinks links,
        [[maybe_unused]] const std::size_t start
    ) -> base::Status {
        // LAB-S03.Q04:R16-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R16"});
        // LAB-S03.Q04:R16-END
    }

    [[maybe_unused]] auto append_forward_bounded(
        [[maybe_unused]] const BtreePageReadView& page,
        [[maybe_unused]] const BtreePageLinks links,
        [[maybe_unused]] const std::size_t start,
        [[maybe_unused]] const EncodedBound& upper
    ) -> base::Status {
        // LAB-S03.Q04:R17-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R17"});
        // LAB-S03.Q04:R17-END
    }

    [[maybe_unused]] auto append_backward_unbounded(
        [[maybe_unused]] const BtreePageReadView& page,
        [[maybe_unused]] const BtreePageLinks links,
        [[maybe_unused]] const std::size_t first,
        [[maybe_unused]] const std::size_t start
    ) -> base::Status {
        // LAB-S03.Q04:R18-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R18"});
        // LAB-S03.Q04:R18-END
    }

    [[maybe_unused]] auto append_backward_bounded(
        [[maybe_unused]] const BtreePageReadView& page,
        [[maybe_unused]] const BtreePageLinks links,
        [[maybe_unused]] const std::size_t first,
        [[maybe_unused]] const std::size_t start,
        [[maybe_unused]] const EncodedBound& lower
    ) -> base::Status {
        // LAB-S03.Q04:R19-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R19"});
        // LAB-S03.Q04:R19-END
    }

    [[maybe_unused]] auto read_leaf_entry(
        [[maybe_unused]] const BtreePageReadView& page,
        [[maybe_unused]] const std::size_t slot
    ) -> BtreeTupleRef {
        // LAB-S03.Q04:R20-BEGIN
        base::lab::missing_algorithm("S03.Q04:R20");
        // LAB-S03.Q04:R20-END
    }

    [[maybe_unused]] void append_candidate([[maybe_unused]] const BtreeTupleRef tuple) {
        // LAB-S03.Q04:R21-BEGIN
        base::lab::missing_algorithm("S03.Q04:R21");
        // LAB-S03.Q04:R21-END
    }

    void clear_candidates() noexcept {
        first_candidate_.reset();
        additional_candidates_.clear();
        candidate_index_ = 0U;
    }

    auto candidate_count() const noexcept -> std::size_t {
        return (first_candidate_.has_value() ? 1U : 0U) + additional_candidates_.size();
    }

    auto candidate_at(const std::size_t index) const noexcept -> BtreeScanCandidate {
        return index == 0U ? *first_candidate_ : additional_candidates_[index - 1U];
    }

    [[maybe_unused]] auto load_leaf_contents(
        [[maybe_unused]] const base::PageId page_id,
        [[maybe_unused]] const bool initial,
        [[maybe_unused]] const BtreePageReadView& page
    ) -> base::Status {
        // LAB-S03.Q04:R22-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R22"});
        // LAB-S03.Q04:R22-END
    }

    [[maybe_unused]] auto load_leaf(
        [[maybe_unused]] storage::BufferPool& pool,
        [[maybe_unused]] const base::PageId page_id,
        [[maybe_unused]] const bool initial
    ) -> base::Status {
        // LAB-S03.Q04:R23-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R23"});
        // LAB-S03.Q04:R23-END
    }

    [[maybe_unused]] auto load_backward_leaf(
        [[maybe_unused]] storage::BufferPool& pool,
        [[maybe_unused]] base::PageId page_id,
        [[maybe_unused]] const base::PageId origin_page_id
    ) -> base::Status {
        // LAB-S03.Q04:R24-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R24"});
        // LAB-S03.Q04:R24-END
    }

    [[maybe_unused]] auto cache_leaf(
        [[maybe_unused]] const BtreePageReadView& page,
        [[maybe_unused]] const BtreePageLinks links,
        [[maybe_unused]] const std::size_t first,
        [[maybe_unused]] const bool initial
    ) -> base::Status {
        // LAB-S03.Q04:R25-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R25"});
        // LAB-S03.Q04:R25-END
    }

    [[maybe_unused]] auto load_initial_leaf([[maybe_unused]] storage::BufferPool& pool)
        -> base::Status {
        // LAB-S03.Q04:R26-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R26"});
        // LAB-S03.Q04:R26-END
    }

    [[maybe_unused]] auto step_leaf([[maybe_unused]] storage::BufferPool& pool) -> base::Status {
        // LAB-S03.Q04:R27-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R27"});
        // LAB-S03.Q04:R27-END
    }

    [[maybe_unused]] auto advance(
        [[maybe_unused]] storage::BufferPool& pool,
        [[maybe_unused]] const ScanDirection requested
    ) -> base::Result<std::optional<BtreeScanCandidate>> {
        // LAB-S03.Q04:R28-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R28"});
        // LAB-S03.Q04:R28-END
    }

private:
    friend class BtreeScan;

    base::RelationId relation_id_;
    const BtreeKeyLayout* key_layout_;
    ScanBounds bounds_;
    std::shared_ptr<BtreeRelation::State> relation_state_;
    std::optional<BtreeRoot> root_hint_;
    ScanDirection direction_{ScanDirection::kUndecided};
    bool positioned_{false};
    bool done_{false};
    base::PageId current_leaf_{base::kInvalidPageId};
    std::optional<base::PageId> next_leaf_;
    std::optional<BtreeScanCandidate> first_candidate_;
    std::vector<BtreeScanCandidate> additional_candidates_;
    std::size_t candidate_index_{0U};
    std::size_t leaf_pages_visited_{0U};
    std::size_t leaf_items_examined_{0U};
    std::size_t root_hint_uses_{0U};
    base::Xid own_xid_{base::kInvalidXid};
    transaction::IsolationLevel isolation_{transaction::IsolationLevel::kReadCommitted};
    transaction::SerializableConflictManager* serializable_conflicts_{nullptr};
};

BtreeScan::BtreeScan(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

BtreeScan::BtreeScan(BtreeScan&& other) noexcept = default;
auto BtreeScan::operator=(BtreeScan&& other) noexcept -> BtreeScan& = default;
BtreeScan::~BtreeScan() = default;

[[maybe_unused]] auto BtreeScan::for_eq(
    [[maybe_unused]] const BtreeRelation& tree,
    [[maybe_unused]] const std::span<const types::Value> values,
    [[maybe_unused]] const BtreeScanContext context
) -> base::Result<BtreeScan> {
    // LAB-S03.Q04:R29-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R29"});
    // LAB-S03.Q04:R29-END
}

[[maybe_unused]] auto BtreeScan::for_range(
    [[maybe_unused]] const BtreeRelation& tree,
    [[maybe_unused]] const BtreeScanRange range,
    [[maybe_unused]] const BtreeScanContext context
) -> base::Result<BtreeScan> {
    // LAB-S03.Q04:R30-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R30"});
    // LAB-S03.Q04:R30-END
}

[[maybe_unused]] auto BtreeScan::for_eq_range(
    [[maybe_unused]] const BtreeRelation& tree,
    [[maybe_unused]] const std::span<const types::Value> equality_prefix,
    [[maybe_unused]] const BtreeScanRange range,
    [[maybe_unused]] const BtreeScanContext context
) -> base::Result<BtreeScan> {
    // LAB-S03.Q04:R31-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R31"});
    // LAB-S03.Q04:R31-END
}

[[maybe_unused]] auto BtreeScan::for_full(
    [[maybe_unused]] const BtreeRelation& tree,
    [[maybe_unused]] const BtreeScanContext context
) -> base::Result<BtreeScan> {
    // LAB-S03.Q04:R32-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R32"});
    // LAB-S03.Q04:R32-END
}

auto BtreeScan::valid() const noexcept -> bool {
    return implementation_ != nullptr;
}

[[maybe_unused]] auto BtreeScan::next_candidate([[maybe_unused]] storage::BufferPool& pool)
    -> base::Result<std::optional<BtreeScanCandidate>> {
    // LAB-S03.Q04:R33-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R33"});
    // LAB-S03.Q04:R33-END
}

[[maybe_unused]] auto BtreeScan::next([[maybe_unused]] storage::BufferPool& pool)
    -> base::Result<std::optional<base::Rid>> {
    // LAB-S03.Q04:R34-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R34"});
    // LAB-S03.Q04:R34-END
}

[[maybe_unused]] auto BtreeScan::previous_candidate([[maybe_unused]] storage::BufferPool& pool)
    -> base::Result<std::optional<BtreeScanCandidate>> {
    // LAB-S03.Q04:R35-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R35"});
    // LAB-S03.Q04:R35-END
}

[[maybe_unused]] auto BtreeScan::previous([[maybe_unused]] storage::BufferPool& pool)
    -> base::Result<std::optional<base::Rid>> {
    // LAB-S03.Q04:R36-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R36"});
    // LAB-S03.Q04:R36-END
}

[[maybe_unused]] auto
BtreeScan::rebind_eq([[maybe_unused]] const std::span<const types::Value> values) -> base::Status {
    // LAB-S03.Q04:R37-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R37"});
    // LAB-S03.Q04:R37-END
}

[[maybe_unused]] auto BtreeScan::rebind_range([[maybe_unused]] const BtreeScanRange range)
    -> base::Status {
    // LAB-S03.Q04:R38-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R38"});
    // LAB-S03.Q04:R38-END
}

[[maybe_unused]] auto BtreeScan::rebind_eq_range(
    [[maybe_unused]] const std::span<const types::Value> equality_prefix,
    [[maybe_unused]] const BtreeScanRange range
) -> base::Status {
    // LAB-S03.Q04:R39-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q04:R39"});
    // LAB-S03.Q04:R39-END
}

[[maybe_unused]] void BtreeScan::rescan() noexcept {
    // LAB-S03.Q04:R40-BEGIN
    base::lab::missing_algorithm("S03.Q04:R40");
    // LAB-S03.Q04:R40-END
}

auto BtreeScan::leaf_pages_visited() const noexcept -> std::size_t {
    return valid() ? implementation_->leaf_pages_visited_ : 0U;
}

auto BtreeScan::leaf_items_examined() const noexcept -> std::size_t {
    return valid() ? implementation_->leaf_items_examined_ : 0U;
}

auto BtreeScan::root_hint_uses() const noexcept -> std::size_t {
    return valid() ? implementation_->root_hint_uses_ : 0U;
}

}  // namespace db::access
