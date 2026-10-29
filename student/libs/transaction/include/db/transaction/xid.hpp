#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::transaction {

class ProcArray;
class ProcSlot;

namespace detail {
class XidAssignmentPreparation;
}  // namespace detail

inline constexpr base::Xid kFirstNormalXid{1};
// DB does not reuse a 32-bit XID until tuple freezing and an epoch-aware
// durable horizon exist. This value is a stop boundary, never an assigned XID.
inline constexpr base::Xid kXidWrapBoundary{UINT32_MAX};

enum class XidOrder : std::uint8_t {
    kBefore,
    kEqual,
    kAfter,
};

constexpr auto is_normal_xid(base::Xid xid) noexcept -> bool {
    return xid != base::kInvalidXid && xid != kXidWrapBoundary;
}

// PostgreSQL-style modular ordering. Exactly half the 32-bit domain is
// ambiguous and invalid/sentinel values are rejected.
auto compare_xids(base::Xid left, base::Xid right) -> base::Result<XidOrder>;

class XidGenerator final {
public:
    XidGenerator() = default;
    ~XidGenerator() = default;
    XidGenerator(const XidGenerator&) = delete;
    auto operator=(const XidGenerator&) -> XidGenerator& = delete;
    XidGenerator(XidGenerator&&) = delete;
    auto operator=(XidGenerator&&) -> XidGenerator& = delete;

    // Restores the durable next-XID position. The wrap boundary is accepted
    // as an exhausted state so reopen is deterministic.
    static auto from_next_xid(base::Xid next_xid) -> base::Result<std::unique_ptr<XidGenerator>>;

    auto allocate() -> base::Result<base::Xid>;
    auto next_xid() const -> base::Xid;
    auto remaining_before_wrap() const -> std::uint64_t;

private:
    friend class ProcArray;

    explicit XidGenerator(base::Xid next_xid) noexcept;

    // Extends CLOG, advances next-XID, and publishes the stable ProcSlot while
    // holding the generation lock, matching PostgreSQL's assignment boundary.
    auto allocate_prepared(detail::XidAssignmentPreparation& preparation, ProcSlot& slot)
        -> base::Result<base::Xid>;

    mutable std::mutex generation_mutex_;
    base::Xid next_xid_{kFirstNormalXid};
};

}  // namespace db::transaction
