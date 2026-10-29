#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"
#include "db/transaction/commit_log.hpp"

namespace db::access {

inline constexpr std::size_t kLinePointerSize = sizeof(std::uint32_t);
inline constexpr std::size_t kTupleHeaderSize = 24;
inline constexpr std::size_t kMaximumHeapRowSize =
    storage::kPageSize - storage::kPageHeaderSize - kLinePointerSize - kTupleHeaderSize;

namespace heap_page_flags {

inline constexpr std::uint16_t kPrunable = 1U << 0U;
inline constexpr std::uint16_t kFull = 1U << 1U;

}  // namespace heap_page_flags

enum class LinePointerState : std::uint8_t {
    kUnused = 0,
    kNormal = 1,
    kRedirect = 2,
    kDead = 3,
};

struct LinePointer final {
    std::uint16_t offset{0};
    std::uint16_t length{0};
    LinePointerState state{LinePointerState::kUnused};

    auto operator==(const LinePointer&) const noexcept -> bool = default;

    static auto unused() noexcept -> LinePointer;
    static auto normal(std::uint16_t offset, std::uint16_t length) noexcept -> LinePointer;
    static auto redirect(base::SlotId target) noexcept -> LinePointer;
    static auto dead() noexcept -> LinePointer;

    auto encode(base::MutableBytes output) const -> base::Status;
    static auto decode(base::Bytes input) -> base::Result<LinePointer>;
};

namespace tuple_flags {

inline constexpr std::uint16_t kXmaxCommitted = 1U << 0U;
inline constexpr std::uint16_t kXmaxAborted = 1U << 1U;
inline constexpr std::uint16_t kUpdated = 1U << 2U;
inline constexpr std::uint16_t kDeleted = 1U << 3U;
inline constexpr std::uint16_t kHotUpdated = 1U << 4U;
inline constexpr std::uint16_t kHeapOnly = 1U << 5U;
inline constexpr std::uint16_t kXmaxLockOnly = 1U << 6U;
inline constexpr std::uint16_t kKnownMask =
    kXmaxCommitted | kXmaxAborted | kUpdated | kDeleted | kHotUpdated | kHeapOnly | kXmaxLockOnly;

}  // namespace tuple_flags

struct TupleHeader final {
    base::Xid xmin{base::kInvalidXid};
    base::Xid xmax{base::kInvalidXid};
    base::Rid ctid{base::kInvalidRid};
    std::uint16_t flags{0};
    std::uint16_t header_size{kTupleHeaderSize};
    std::uint32_t command_id{0};

    auto operator==(const TupleHeader&) const noexcept -> bool = default;

    static auto inserted(base::Xid xmin, base::Rid self, std::uint32_t command_id) noexcept
        -> TupleHeader;
    auto has_flag(std::uint16_t flag) const noexcept -> bool;
    auto encode(base::MutableBytes output) const -> base::Status;
    static auto decode(base::Bytes input) -> base::Result<TupleHeader>;
};

struct HeapTupleView final {
    TupleHeader header;
    base::Bytes payload;
};

struct HeapPruneRedirect final {
    base::SlotId root{base::kInvalidSlotId};
    base::SlotId target{base::kInvalidSlotId};

    auto operator==(const HeapPruneRedirect&) const noexcept -> bool = default;
};

struct HeapPruneResult final {
    std::size_t tuples_pruned{0};
    std::size_t redirects_installed{0};
    std::size_t space_reclaimed{0};
    std::size_t line_pointers_truncated{0};
    std::vector<HeapPruneRedirect> redirects;
    std::vector<base::SlotId> dead;
    std::vector<base::SlotId> unused;

    auto has_storage_changes() const noexcept -> bool;
};

auto encode_heap_tuple(const TupleHeader& header, base::Bytes payload)
    -> base::Result<std::vector<std::byte>>;
auto decode_heap_tuple(base::Bytes tuple) -> base::Result<HeapTupleView>;

// Non-owning heap-page facade. The Page and its address must outlive this
// object. open() checks the generic and heap header only; accessors validate
// the slot or tuple they touch. validate_layout() is the explicit whole-page
// audit for recovery, publication, and consistency-check boundaries.
// Mutating the Page outside this handle invalidates its cached header.
class HeapPage final {
public:
    static auto open(storage::Page& page, storage::PageAddress address) -> base::Result<HeapPage>;

    auto item_count() const -> std::size_t;
    auto free_space() const -> std::size_t;
    auto line_pointer(base::SlotId slot) const -> base::Result<LinePointer>;
    auto tuple_bytes(base::SlotId slot) const -> base::Result<base::Bytes>;
    // Reads and validates the line pointer once. Non-normal slots return an
    // empty optional; normal slots retain the tuple bounds/header checks.
    auto tuple_if_normal(base::SlotId slot) const -> base::Result<std::optional<HeapTupleView>>;
    auto mutable_tuple_bytes(base::SlotId slot) -> base::Result<base::MutableBytes>;
    auto tuple(base::SlotId slot) const -> base::Result<HeapTupleView>;

    auto insert_tuple(const TupleHeader& header, base::Bytes payload) -> base::Result<base::SlotId>;
    auto insert(base::Xid xmin, std::uint32_t command_id, base::Bytes payload)
        -> base::Result<base::Rid>;

    auto mark_dead(base::SlotId slot) -> base::Status;
    auto mark_unused(base::SlotId slot) -> base::Status;
    auto redirect(base::SlotId from, base::SlotId to) -> base::Status;
    auto resolve(base::SlotId slot) const -> base::Result<base::SlotId>;
    // Requires the caller to hold the page's sole-pin cleanup latch.
    auto repair_fragmentation() -> base::Result<std::size_t>;
    // Requires the caller to hold the page's sole-pin cleanup latch and to
    // keep oldest_xmin fixed for the complete pruning pass.
    auto prune(const transaction::CommitLog& commit_log, base::Xid oldest_xmin)
        -> base::Result<HeapPruneResult>;
    auto validate_layout() const -> base::Status;

private:
    class CompactionOperation;
    class PruneOperation;

    HeapPage(storage::Page& page, storage::PageAddress address) noexcept;

    auto page_header() const -> base::Result<storage::PageHeader>;
    auto tuple_bytes(const LinePointer& pointer) const -> base::Result<base::Bytes>;
    auto write_page_header(storage::PageHeader header) -> base::Status;
    auto insert_tuple_at(
        const TupleHeader& header,
        base::Bytes payload,
        std::optional<base::SlotId> reusable
    ) -> base::Result<base::SlotId>;
    auto find_unused_slot() -> base::Result<std::optional<base::SlotId>>;
    auto write_line_pointer(base::SlotId slot, const LinePointer& pointer) -> base::Status;

    storage::Page* page_;
    storage::PageAddress address_;
    storage::PageHeader validated_header_;
    std::size_t unused_scan_begin_{0U};
};

}  // namespace db::access
