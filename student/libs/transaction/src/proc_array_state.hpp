#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "db/base/error.hpp"
#include "db/transaction/proc_slot.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction::detail {

inline constexpr auto kInactiveProcSlotIndex = std::numeric_limits<std::size_t>::max();
inline constexpr std::uint8_t kCompletionGroupResultPending = 0;
inline constexpr std::uint8_t kCompletionGroupResultSuccess = 1;
inline constexpr std::uint8_t kCompletionGroupResultInvariantFailure = 2;

auto proc_array_error(base::ErrorCode code, std::string message, std::string operation)
    -> base::Error;

struct ProcArrayEntry final {
    std::unique_ptr<ProcSlot> slot;
    std::size_t active_index{kInactiveProcSlotIndex};
    std::uint64_t generation{0};
    bool registered{false};
    bool abandoned{false};
};

// Shared ownership lets an idle lease close after the ProcArray facade has gone away.
// Callers must hold mutex exclusively before invoking unregister_slot_locked().
class ProcArrayState final {
public:
    ProcArrayState(std::unique_ptr<XidGenerator> xid_generator, base::Xid completed_xid) noexcept;

    void unregister_slot_locked(std::size_t slot_index);

    mutable std::shared_mutex mutex;
    std::unique_ptr<XidGenerator> generator;
    base::Xid latest_completed_xid;
    std::uint64_t completion_count{1};
    std::vector<ProcArrayEntry> slots;
    std::vector<std::size_t> active_slots;
    std::vector<std::size_t> free_slots;
    // ProcSlot allocations are stable for the lifetime of this state. The
    // intrusive pointer chain lets a detached leader wake followers without
    // touching the relocatable slots directory after releasing mutex.
    std::atomic<ProcSlot*> completion_group_head{nullptr};
    std::atomic<std::uint64_t> completion_group_leaders{0};
    std::atomic<std::uint64_t> completion_group_members{0};
};

}  // namespace db::transaction::detail
