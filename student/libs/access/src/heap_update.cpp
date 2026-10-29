#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "db/access/heap_page.hpp"
#include "db/access/heap_table.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/storage/page.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/xid.hpp"

#include "heap_tuple_state_internal.hpp"

namespace db::access {
namespace {

[[maybe_unused]] auto update_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view message
) -> base::Error {
    return base::Error{code, std::string{message}}.with_context("access", std::string{operation});
}

[[maybe_unused]] auto
validate_update(const HeapUpdateRequest& request, const HeapUpdateContext& context)
    -> base::Status {
    if (!detail::is_valid_heap_rid(request.target)) {
        return base::fail(update_error(
            base::ErrorCode::kInvalidArgument,
            "update heap tuple",
            "heap update target RID must be valid"
        ));
    }
    if (!transaction::is_normal_xid(context.mvcc.own_xid)) {
        return base::fail(update_error(
            base::ErrorCode::kInvalidArgument,
            "update heap tuple",
            "heap update requires a normal transaction identifier"
        ));
    }
    if (context.mvcc.snapshot == nullptr || context.mvcc.commit_log == nullptr) {
        return base::fail(update_error(
            base::ErrorCode::kInvalidArgument,
            "update heap tuple",
            "heap update requires snapshot and commit-log services"
        ));
    }
    constexpr auto k_maximum_payload =
        storage::kMaximumPageFreeSpace - kTupleHeaderSize - kLinePointerSize;
    if (request.payload.size() > k_maximum_payload) {
        return base::fail(update_error(
            base::ErrorCode::kResourceExhausted,
            "update heap tuple",
            "updated heap tuple cannot fit on one page"
        ));
    }
    return {};
}

[[maybe_unused]] auto clamp_free_space(const std::size_t free_space) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(
        std::min<std::size_t>(free_space, storage::kMaximumPageFreeSpace)
    );
}

[[maybe_unused]] auto
write_tuple_header(HeapPage& page, const base::SlotId slot, const TupleHeader& header)
    -> base::Status {
    auto bytes = page.mutable_tuple_bytes(slot);
    if (!bytes) {
        return base::fail(std::move(bytes).error().with_context("access", "write tuple header"));
    }
    return header.encode(bytes->first(kTupleHeaderSize));
}

[[maybe_unused]] void clear_xmax_state(TupleHeader& header) noexcept {
    detail::clear_heap_tuple_xmax_state(header);
}

[[maybe_unused]] void mark_locked(
    TupleHeader& header,
    const base::Xid locker,
    const base::Rid self,
    const std::uint32_t command_id
) noexcept {
    clear_xmax_state(header);
    header.xmax = locker;
    header.ctid = self;
    header.command_id = command_id;
    header.flags |= tuple_flags::kXmaxLockOnly;
}

[[maybe_unused]] void mark_updated(
    [[maybe_unused]] TupleHeader& header,
    [[maybe_unused]] const base::Xid updater,
    [[maybe_unused]] const base::Rid successor,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const bool hot
) noexcept {
    // LAB-S01.Q09:R01-BEGIN
    base::lab::missing_algorithm("S01.Q09:R01");
    // LAB-S01.Q09:R01-END
}

[[maybe_unused]] auto
reject_update(const TupleHeader& header, const transaction::TupleModificationDecision decision)
    -> HeapUpdateRejected {
    std::optional<base::Rid> newer_rid;
    const bool carries_successor =
        decision.result == transaction::TupleModificationResult::kUpdated
        || decision.result == transaction::TupleModificationResult::kSelfModified;
    if (carries_successor && header.has_flag(tuple_flags::kUpdated)) {
        newer_rid = header.ctid;
    }
    return HeapUpdateRejected{.decision = decision, .newer_rid = newer_rid};
}

[[maybe_unused]] auto log_page_mutation(
    [[maybe_unused]] HeapMutationLogPort& mutation_log,
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::Page& before,
    [[maybe_unused]] HeapWalMutation mutation
) -> base::Result<base::Lsn> {
    // LAB-S01.Q09:R02-BEGIN
    base::lab::missing_algorithm("S01.Q09:R02");
    // LAB-S01.Q09:R02-END
}

struct OldVersion final {
    base::Rid rid;
    TupleHeader header;
};

[[maybe_unused]] auto resolve_old_version(HeapPage& page, const base::Rid requested)
    -> base::Result<OldVersion> {
    auto resolved = page.resolve(requested.slot_id());
    if (!resolved) {
        return base::fail(
            std::move(resolved).error().with_context("access", "resolve update target")
        );
    }
    auto tuple = page.tuple(resolved.value());
    if (!tuple) {
        return base::fail(std::move(tuple).error().with_context("access", "read update target"));
    }
    return OldVersion{
        .rid = base::Rid{requested.page_id(), resolved.value()},
        .header = tuple->header,
    };
}

struct HotMutation final {
    HeapUpdateSuccess success;
    std::uint16_t free_space;
};

struct ColdPreparation final {
    OldVersion old_version;
};

using Preparation = std::variant<HotMutation, HeapUpdateRejected, ColdPreparation>;

class HeapUpdateOperation final {
public:
    HeapUpdateOperation(
        HeapTable& table,
        storage::BufferPool& pool,
        HeapMutationLogPort& mutation_log,
        HeapUpdateRequest request,
        HeapUpdateContext context
    ) noexcept
        : table_{table},
          pool_{pool},
          mutation_log_{mutation_log},
          request_{request},
          context_{context},
          old_address_{table.relation_id(), request.target.page_id()} {}

    [[maybe_unused]] auto run() -> base::Result<HeapUpdateOutcome> {
        // LAB-S01.Q09:R03-BEGIN
        base::lab::missing_algorithm("S01.Q09:R03");
        // LAB-S01.Q09:R03-END
    }

private:
    [[maybe_unused]] auto prepare_old_version() -> base::Result<Preparation> {
        // LAB-S09.Q04:R01-BEGIN
        base::lab::missing_algorithm("S09.Q04:R01");
        // LAB-S09.Q04:R01-END
    }

    [[maybe_unused]] auto try_hot_update(
        [[maybe_unused]] storage::Page& storage_page,
        [[maybe_unused]] HeapPage& page,
        [[maybe_unused]] const OldVersion& old
    ) -> base::Result<std::optional<HotMutation>> {
        // LAB-S01.Q09:R04-BEGIN
        base::lab::missing_algorithm("S01.Q09:R04");
        // LAB-S01.Q09:R04-END
    }

    [[maybe_unused]] auto install_temporary_lock(
        [[maybe_unused]] storage::Page& storage_page,
        [[maybe_unused]] HeapPage& page,
        [[maybe_unused]] const OldVersion& old
    ) -> base::Status {
        // LAB-S01.Q09:R05-BEGIN
        base::lab::missing_algorithm("S01.Q09:R05");
        // LAB-S01.Q09:R05-END
    }

    [[maybe_unused]] auto finalize_cold_update(
        [[maybe_unused]] const OldVersion& old,
        [[maybe_unused]] const base::Rid successor
    ) -> base::Status {
        // LAB-S09.Q04:R02-BEGIN
        base::lab::missing_algorithm("S09.Q04:R02");
        // LAB-S09.Q04:R02-END
    }

    [[maybe_unused]] auto apply_cold_update_mutation(
        [[maybe_unused]] storage::Page& storage_page,
        [[maybe_unused]] HeapPage& page,
        [[maybe_unused]] const TupleHeader& current_header,
        [[maybe_unused]] const OldVersion& old,
        [[maybe_unused]] const base::Rid successor
    ) -> base::Status {
        // LAB-S01.Q09:R09-BEGIN
        base::lab::missing_algorithm("S01.Q09:R09");
        // LAB-S01.Q09:R09-END
    }

    HeapTable& table_;
    storage::BufferPool& pool_;
    HeapMutationLogPort& mutation_log_;
    HeapUpdateRequest request_;
    HeapUpdateContext context_;
    storage::PageAddress old_address_;
};

}  // namespace

auto HeapTable::update(const HeapUpdateRequest request, const HeapUpdateContext context)
    -> base::Result<HeapUpdateOutcome> {
    if (!valid()) {
        return base::fail(update_error(
            base::ErrorCode::kUnavailable,
            "update heap tuple",
            "heap relation handle is inactive"
        ));
    }
    return HeapUpdateOperation{*this, buffer_pool(), mutation_log(), request, context}.run();
}

}  // namespace db::access
