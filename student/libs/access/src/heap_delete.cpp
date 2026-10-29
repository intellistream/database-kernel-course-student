#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

[[maybe_unused]] auto delete_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view message
) -> base::Error {
    return base::Error{code, std::string{message}}.with_context("access", std::string{operation});
}

[[maybe_unused]] auto
validate_delete(const HeapDeleteRequest request, const HeapMutationContext& context)
    -> base::Status {
    if (!detail::is_valid_heap_rid(request.target)) {
        return base::fail(delete_error(
            base::ErrorCode::kInvalidArgument,
            "delete heap tuple",
            "heap delete target RID must be valid"
        ));
    }
    if (!transaction::is_normal_xid(context.mvcc.own_xid)) {
        return base::fail(delete_error(
            base::ErrorCode::kInvalidArgument,
            "delete heap tuple",
            "heap delete requires a normal transaction identifier"
        ));
    }
    if (context.mvcc.snapshot == nullptr || context.mvcc.commit_log == nullptr) {
        return base::fail(delete_error(
            base::ErrorCode::kInvalidArgument,
            "delete heap tuple",
            "heap delete requires snapshot and commit-log services"
        ));
    }
    return {};
}

[[maybe_unused]] auto
reject_delete(const TupleHeader& header, const transaction::TupleModificationDecision decision)
    -> HeapMutationRejected {
    std::optional<base::Rid> newer_rid;
    const bool carries_successor =
        decision.result == transaction::TupleModificationResult::kUpdated
        || decision.result == transaction::TupleModificationResult::kSelfModified;
    if (carries_successor && header.has_flag(tuple_flags::kUpdated)) {
        newer_rid = header.ctid;
    }
    return HeapMutationRejected{.decision = decision, .newer_rid = newer_rid};
}

[[maybe_unused]] auto
write_tuple_header(HeapPage& page, const base::SlotId slot, const TupleHeader& header)
    -> base::Status {
    auto bytes = page.mutable_tuple_bytes(slot);
    if (!bytes) {
        return base::fail(std::move(bytes).error().with_context("access", "write delete header"));
    }
    return header.encode(bytes->first(kTupleHeaderSize));
}

[[maybe_unused]] void mark_deleted(
    [[maybe_unused]] TupleHeader& header,
    [[maybe_unused]] const base::Xid deleter,
    [[maybe_unused]] const base::Rid self,
    [[maybe_unused]] const std::uint32_t command_id
) noexcept {
    // LAB-S01.Q09:R06-BEGIN
    base::lab::missing_algorithm("S01.Q09:R06");
    // LAB-S01.Q09:R06-END
}

[[maybe_unused]] auto log_delete(
    [[maybe_unused]] HeapMutationLogPort& mutation_log,
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::Page& before,
    [[maybe_unused]] const HeapWalMutation mutation
) -> base::Status {
    // LAB-S01.Q09:R07-BEGIN
    base::lab::missing_algorithm("S01.Q09:R07");
    // LAB-S01.Q09:R07-END
}

class HeapDeleteOperation final {
public:
    HeapDeleteOperation(
        HeapTable& table,
        storage::BufferPool& pool,
        HeapMutationLogPort& mutation_log,
        const HeapDeleteRequest request,
        const HeapMutationContext context
    ) noexcept
        : table_{table},
          pool_{pool},
          mutation_log_{mutation_log},
          request_{request},
          context_{context},
          address_{table.relation_id(), request.target.page_id()} {}

    [[maybe_unused]] auto run() -> base::Result<HeapDeleteOutcome> {
        auto valid = validate_delete(request_, context_);
        if (!valid) {
            return base::fail(std::move(valid).error());
        }
        if (context_.mvcc.serializable_conflicts != nullptr) {
            auto conflict = context_.mvcc.serializable_conflicts->check_write_conflict(
                context_.mvcc.own_xid,
                transaction::SsiReadTarget::for_tuple(table_.relation_id(), request_.target)
            );
            if (!conflict) {
                return base::fail(std::move(conflict).error());
            }
        }
        return delete_under_latch();
    }

private:
    [[maybe_unused]] auto delete_under_latch() -> base::Result<HeapDeleteOutcome> {
        // LAB-S09.Q04:R03-BEGIN
        base::lab::missing_algorithm("S09.Q04:R03");
        // LAB-S09.Q04:R03-END
    }

    [[maybe_unused]] auto apply_delete_mutation(
        [[maybe_unused]] storage::Page& storage_page,
        [[maybe_unused]] HeapPage& page,
        [[maybe_unused]] const base::Rid actual,
        [[maybe_unused]] const TupleHeader& current_header
    ) -> base::Result<HeapDeleteOutcome> {
        // LAB-S01.Q09:R10-BEGIN
        base::lab::missing_algorithm("S01.Q09:R10");
        // LAB-S01.Q09:R10-END
    }

    HeapTable& table_;
    storage::BufferPool& pool_;
    HeapMutationLogPort& mutation_log_;
    HeapDeleteRequest request_;
    HeapMutationContext context_;
    storage::PageAddress address_;
};

}  // namespace

auto HeapTable::delete_tuple(const HeapDeleteRequest request, const HeapMutationContext context)
    -> base::Result<HeapDeleteOutcome> {
    if (!valid()) {
        return base::fail(delete_error(
            base::ErrorCode::kUnavailable,
            "delete heap tuple",
            "heap relation handle is inactive"
        ));
    }
    return HeapDeleteOperation{*this, buffer_pool(), mutation_log(), request, context}.run();
}

}  // namespace db::access
