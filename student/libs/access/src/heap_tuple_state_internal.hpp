#include "db/base/lab_missing.hpp"
#pragma once

#include <cstdint>

#include "db/access/heap_page.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/visibility.hpp"

namespace db::access::detail {

inline constexpr std::uint16_t kHeapTupleXmaxStateMask =
    tuple_flags::kXmaxCommitted | tuple_flags::kXmaxAborted | tuple_flags::kUpdated
    | tuple_flags::kDeleted | tuple_flags::kHotUpdated | tuple_flags::kXmaxLockOnly;

inline auto is_valid_heap_rid(const base::Rid rid) noexcept -> bool {
    return rid.page_id() != base::kInvalidPageId && rid.slot_id() != base::kInvalidSlotId;
}

[[maybe_unused]] inline auto
heap_tuple_version_state([[maybe_unused]] const TupleHeader& header) noexcept
    -> transaction::TupleVersionState {
    // LAB-S09.Q04:R04-BEGIN
    base::lab::missing_algorithm("S09.Q04:R04");
    // LAB-S09.Q04:R04-END
}

[[maybe_unused]] inline void
clear_heap_tuple_xmax_state([[maybe_unused]] TupleHeader& header) noexcept {
    // LAB-S01.Q09:R08-BEGIN
    base::lab::missing_algorithm("S01.Q09:R08");
    // LAB-S01.Q09:R08-END
}

}  // namespace db::access::detail
