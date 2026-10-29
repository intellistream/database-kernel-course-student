#include "db/recovery/transaction_wal.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/xid.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

[[maybe_unused]] auto
adapter_error(const base::ErrorCode code, std::string operation, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "recovery.transaction_wal",
        std::move(operation)
    );
}

[[maybe_unused]] auto completion_info([[maybe_unused]] const transaction::CompletionOutcome outcome)
    -> base::Result<std::uint8_t> {
    // LAB-S10.Q06:R1020-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q06:R1020"});
    // LAB-S10.Q06:R1020-END
}

}  // namespace

TransactionCompletionWalAdapter::TransactionCompletionWalAdapter(wal::WalManager& manager) noexcept
    : manager_{&manager} {}

[[maybe_unused]] auto TransactionCompletionWalAdapter::append_completion(
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const transaction::CompletionOutcome outcome
) -> base::Result<base::Lsn> {
    // LAB-S10.Q06:R1021-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q06:R1021"});
    // LAB-S10.Q06:R1021-END
}

[[maybe_unused]] auto TransactionCompletionWalAdapter::flush_through(
    [[maybe_unused]] const base::Lsn end_lsn,
    [[maybe_unused]] const std::chrono::microseconds group_delay
) -> base::Status {
    // LAB-S10.Q06:R1022-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q06:R1022"});
    // LAB-S10.Q06:R1022-END
}

}  // namespace db::recovery
