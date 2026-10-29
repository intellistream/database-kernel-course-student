#include "ssi_commit_order.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "ssi_lifecycle.hpp"

namespace db::transaction::detail {
namespace {

[[maybe_unused]] auto
commit_order_error(base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "transaction/ssi",
        std::move(operation)
    );
}

[[maybe_unused]] auto serialization_error(base::Xid xid, std::string detail) -> base::Error {
    return commit_order_error(
        base::ErrorCode::kSerializationFailure,
        "serializable transaction " + std::to_string(xid.value()) + " " + std::move(detail),
        "precommit"
    );
}

}  // namespace

[[maybe_unused]] auto advance_ssi_commit_sequence([[maybe_unused]] std::uint64_t& last_sequence)
    -> base::Result<std::uint64_t> {
    // LAB-S09.Q06:R26-BEGIN
    base::lab::missing_algorithm("S09.Q06:R26");
    // LAB-S09.Q06:R26-END
}

[[maybe_unused]] auto prepare_ssi_transaction(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] ActiveSnapshotHorizon& active_snapshots,
    [[maybe_unused]] std::uint64_t& last_commit_sequence,
    [[maybe_unused]] const base::Xid xid
) -> base::Status {
    // LAB-S09.Q06:R27-BEGIN
    base::lab::missing_algorithm("S09.Q06:R27");
    // LAB-S09.Q06:R27-END
}
}  // namespace db::transaction::detail
