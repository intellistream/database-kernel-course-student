#pragma once

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::transaction::detail {

// Consumer-owned CLOG preparation port invoked while the XID generator owns
// its serialization lock. A failure leaves next-XID and ProcSlot unchanged.
class XidAssignmentPreparation {
public:
    XidAssignmentPreparation() = default;
    virtual ~XidAssignmentPreparation() = default;
    XidAssignmentPreparation(const XidAssignmentPreparation&) = delete;
    auto operator=(const XidAssignmentPreparation&) -> XidAssignmentPreparation& = delete;
    XidAssignmentPreparation(XidAssignmentPreparation&&) = delete;
    auto operator=(XidAssignmentPreparation&&) -> XidAssignmentPreparation& = delete;

    virtual auto prepare(base::Xid candidate) -> base::Status = 0;
};

}  // namespace db::transaction::detail
