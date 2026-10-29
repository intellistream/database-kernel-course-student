#include "completion_journal_adapter.hpp"

#include <string>
#include <type_traits>
#include <utility>

#include "db/base/exception_boundary.hpp"
#include "db/base/lab_missing.hpp"

namespace db::transaction::detail {
namespace {

constexpr base::ExceptionBoundaryPolicy kCompletionJournalExceptionPolicy{
    .component = {},
    .allocation_message = "completion journal port exhausted available memory",
    .exception_prefix = "completion journal port threw an exception: ",
    .unknown_exception_message = "completion journal port threw a non-standard exception",
};

template <typename Callback>
[[maybe_unused]] auto
invoke_completion_journal_port(const std::string_view operation, Callback&& callback)
    -> std::invoke_result_t<Callback&&> {
    auto result = base::invoke_exception_boundary(
        kCompletionJournalExceptionPolicy,
        std::forward<Callback>(callback)
    );
    if (!result) {
        return base::fail(
            std::move(result).error().with_context("transaction/manager", std::string{operation})
        );
    }
    return result;
}

}  // namespace

[[maybe_unused]] auto append_completion_journal(
    [[maybe_unused]] CompletionJournalPort& journal,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const CompletionOutcome outcome,
    [[maybe_unused]] const std::string_view operation
) -> base::Result<base::Lsn> {
    // LAB-S08.Q06:R14-BEGIN
    base::lab::missing_algorithm("S08.Q06:R14");
    // LAB-S08.Q06:R14-END
}

[[maybe_unused]] auto flush_completion_journal(
    [[maybe_unused]] CompletionJournalPort& journal,
    [[maybe_unused]] const base::Lsn end_lsn,
    [[maybe_unused]] const std::chrono::microseconds group_delay,
    [[maybe_unused]] const std::string_view operation
) -> base::Status {
    // LAB-S08.Q06:R15-BEGIN
    base::lab::missing_algorithm("S08.Q06:R15");
    // LAB-S08.Q06:R15-END
}

}  // namespace db::transaction::detail
