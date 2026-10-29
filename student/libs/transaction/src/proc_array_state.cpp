#include "proc_array_state.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::transaction::detail {

auto proc_array_error(base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "transaction/proc-array",
        std::move(operation)
    );
}

ProcArrayState::ProcArrayState(
    std::unique_ptr<XidGenerator> xid_generator,
    base::Xid completed_xid
) noexcept
    : generator{std::move(xid_generator)},
      latest_completed_xid{completed_xid} {}

[[maybe_unused]] void
ProcArrayState::unregister_slot_locked([[maybe_unused]] std::size_t slot_index) {
    // LAB-S08.Q02:R20-BEGIN
    base::lab::missing_algorithm("S08.Q02:R20");
    // LAB-S08.Q02:R20-END
}

}  // namespace db::transaction::detail
