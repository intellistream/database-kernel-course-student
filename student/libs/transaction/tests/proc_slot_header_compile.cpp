#include "db/transaction/proc_slot.hpp"

namespace {

[[maybe_unused]] void compile_proc_slot_header() {
    auto slot = db::transaction::ProcSlot::create(db::base::SlotId{0});
    (void)slot;
}

}  // namespace
