#include "db/recovery/dispatcher.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/recovery/checkpoint.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/transaction/xid.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"

#include "btree_wal_internal.hpp"
#include "heap_wal_internal.hpp"

namespace db::recovery {
namespace {

[[maybe_unused]] auto
dispatcher_error(const base::ErrorCode code, const std::string_view operation, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "recovery.dispatcher",
        std::string{operation}
    );
}

[[maybe_unused]] auto corrupt(const std::string_view operation, std::string message)
    -> base::Status {
    return base::fail(
        dispatcher_error(base::ErrorCode::kCorruption, operation, std::move(message))
    );
}

[[maybe_unused]] auto invalid_argument(const std::string_view operation, std::string message)
    -> base::Error {
    return dispatcher_error(base::ErrorCode::kInvalidArgument, operation, std::move(message));
}

[[maybe_unused]] auto valid_nonfinal_xid(const base::Xid xid) noexcept -> bool {
    return xid == base::kInvalidXid || transaction::is_normal_xid(xid);
}

[[maybe_unused]] auto validate_lsn_envelope(const wal::DecodedWalRecord& decoded) -> base::Status {
    if (decoded.start_lsn == base::kInvalidLsn || decoded.end_lsn == base::kInvalidLsn
        || decoded.end_lsn.value() <= decoded.start_lsn.value()) {
        return corrupt("validate record envelope", "WAL record has an invalid LSN range");
    }
    return {};
}

[[maybe_unused]] auto validate_page_blocks(const wal::DecodedWalRecord& decoded)
    -> base::Result<std::vector<RecoveryBlockDispatch>> {
    const auto& record = decoded.record;
    if (record.blocks.empty()) {
        return base::fail(corrupt("validate page blocks", "page WAL record has no blocks").error());
    }
    if (record.blocks.size() > wal::kMaximumWalBlockReferences) {
        return base::fail(
            corrupt("validate page blocks", "page WAL record has too many blocks").error()
        );
    }

    std::set<storage::PageAddress> addresses;
    std::vector<RecoveryBlockDispatch> result;
    result.reserve(record.blocks.size());
    for (std::size_t index = 0U; index < record.blocks.size(); ++index) {
        const auto& block = record.blocks[index];
        const storage::PageAddress address{block.relation_id, block.page_id};
        if (!address.valid()) {
            return base::fail(corrupt(
                                  "validate page blocks",
                                  "page WAL block has an invalid relation or page address"
            )
                                  .error());
        }
        if (!block.full_page_image.has_value() && block.delta.empty()) {
            return base::fail(corrupt(
                                  "validate page blocks",
                                  "page WAL block has neither a full image nor a delta"
            )
                                  .error());
        }
        if (!addresses.insert(address).second) {
            return base::fail(
                corrupt("validate page blocks", "page WAL record repeats a block address").error()
            );
        }
        result.push_back(
            RecoveryBlockDispatch{
                .block_index = index,
                .address = address,
                .will_initialize = block.will_initialize,
                .standard_page = block.standard_page,
                .has_full_page_image = block.full_page_image.has_value(),
                .has_delta = !block.delta.empty(),
                .payload = {},
            }
        );
    }
    return result;
}

[[maybe_unused]] auto validate_system_record(const wal::WalRecord& record) -> base::Status {
    if (record.xid != base::kInvalidXid || !record.blocks.empty()) {
        return corrupt("validate system record", "system WAL record has an XID or page blocks");
    }
    switch (record.info) {
        case wal::opcode::system::kCheckpointRedo:
            if (!record.main_payload.empty()) {
                return corrupt(
                    "validate checkpoint redo",
                    "checkpoint redo marker has a main payload"
                );
            }
            return {};
        case wal::opcode::system::kCheckpoint: {
            auto checkpoint = decode_checkpoint_payload(record.main_payload);
            if (!checkpoint) {
                return base::fail(
                    std::move(checkpoint)
                        .error()
                        .with_context("recovery.dispatcher", "validate checkpoint")
                );
            }
            return {};
        }
        default:
            return corrupt("validate system record", "system WAL record has an unknown opcode");
    }
}

[[maybe_unused]] auto validate_transaction_record(const wal::WalRecord& record) -> base::Status {
    if (!record.blocks.empty() || !record.main_payload.empty()) {
        return corrupt(
            "validate transaction record",
            "transaction WAL record has page blocks or a main payload"
        );
    }
    if (!transaction::is_normal_xid(record.xid)) {
        return corrupt("validate transaction record", "transaction WAL record has an invalid XID");
    }
    if (record.info != wal::opcode::transaction::kCommit
        && record.info != wal::opcode::transaction::kAbort) {
        return corrupt(
            "validate transaction record",
            "transaction WAL record has an unknown opcode"
        );
    }
    return {};
}

[[maybe_unused]] auto
validate_heap_record(const wal::WalRecord& record, std::vector<RecoveryBlockDispatch>& blocks)
    -> base::Status {
    if (!record.main_payload.empty() || blocks.size() != 1U) {
        return corrupt("validate heap record", "heap WAL record has an invalid envelope");
    }
    auto operation = detail::heap_wal_operation(record.info);
    if (!operation) {
        return base::fail(
            std::move(operation).error().with_context("recovery.dispatcher", "validate heap opcode")
        );
    }
    const bool is_initialize = operation.value() == access::HeapWalOperation::kInitialize;
    const bool may_initialize = is_initialize
                                || operation.value() == access::HeapWalOperation::kInsert
                                || operation.value() == access::HeapWalOperation::kMultiInsert;
    const auto& block = record.blocks.front();
    if (!blocks.front().standard_page || (is_initialize && !blocks.front().will_initialize)
        || (blocks.front().will_initialize && !may_initialize)) {
        return corrupt("validate heap record", "heap WAL block has an invalid page role");
    }
    if (operation.value() == access::HeapWalOperation::kPrune) {
        if (record.xid != base::kInvalidXid) {
            return corrupt("validate heap record", "heap prune WAL record carries an XID");
        }
    } else if (!valid_nonfinal_xid(record.xid)) {
        return corrupt("validate heap record", "heap WAL record has an invalid XID");
    }
    if (block.delta.empty()) {
        return {};
    }
    auto payload = decode_heap_wal_payload(record.info, block.delta);
    if (!payload) {
        return base::fail(
            std::move(payload).error().with_context("recovery.dispatcher", "validate heap payload")
        );
    }
    if (payload->operation != operation.value()) {
        return corrupt("validate heap record", "heap WAL payload and opcode disagree");
    }
    blocks.front().payload = std::move(payload).value();
    return {};
}

[[maybe_unused]] auto
is_btree_metadata_role([[maybe_unused]] const access::BtreeWalPageRole role) noexcept -> bool {
    // LAB-S10.Q11:R01-BEGIN
    base::lab::missing_algorithm("S10.Q11:R01");
    // LAB-S10.Q11:R01-END
}

[[maybe_unused]] auto
validate_btree_record(const wal::WalRecord& record, std::vector<RecoveryBlockDispatch>& blocks)
    -> base::Status {
    if (!record.main_payload.empty()) {
        return corrupt("validate B-tree record", "B-tree WAL record has a main payload");
    }
    if (!valid_nonfinal_xid(record.xid)) {
        return corrupt("validate B-tree record", "B-tree WAL record has an invalid XID");
    }
    auto operation = detail::btree_wal_operation(record.info);
    if (!operation) {
        return base::fail(
            std::move(operation).error().with_context(
                "recovery.dispatcher",
                "validate B-tree opcode"
            )
        );
    }

    const auto relation_id = blocks.front().address.relation_id;
    for (std::size_t index = 0U; index < blocks.size(); ++index) {
        const auto& summary = blocks[index];
        const auto& block = record.blocks[index];
        auto role = detail::expected_btree_role(operation.value(), index, blocks.size());
        if (!role) {
            return base::fail(
                std::move(role).error().with_context(
                    "recovery.dispatcher",
                    "validate B-tree block role"
                )
            );
        }
        if (summary.address.relation_id != relation_id
            || summary.will_initialize
                   != detail::expected_btree_will_initialize(operation.value(), role.value())
            || summary.standard_page == is_btree_metadata_role(role.value())
            || (summary.address.page_id == base::PageId{0})
                   != is_btree_metadata_role(role.value())) {
            return corrupt("validate B-tree record", "B-tree WAL block has an invalid role");
        }
        if (summary.will_initialize && (!summary.has_full_page_image || summary.has_delta)) {
            return corrupt(
                "validate B-tree record",
                "an initializing B-tree WAL block must contain only a full page image"
            );
        }
        blocks[index].payload = BtreeWalPayload{
            .operation = operation.value(),
            .role = role.value(),
            .patches = {},
        };
        if (!summary.has_delta) {
            continue;
        }
        auto payload = decode_btree_wal_payload(record.info, block.delta);
        if (!payload) {
            return base::fail(
                std::move(payload).error().with_context(
                    "recovery.dispatcher",
                    "validate B-tree payload"
                )
            );
        }
        if (payload->operation != operation.value() || payload->role != role.value()) {
            return corrupt("validate B-tree record", "B-tree WAL payload and block role disagree");
        }
        blocks[index].payload = std::move(payload).value();
    }
    return {};
}

[[maybe_unused]] auto validate_catalog_record(
    const wal::WalRecord& record,
    const std::vector<RecoveryBlockDispatch>& blocks
) -> base::Status {
    if (blocks.empty()) {
        return corrupt("validate catalog record", "catalog WAL record has no page blocks");
    }
    if (!valid_nonfinal_xid(record.xid)) {
        return corrupt("validate catalog record", "catalog WAL record has an invalid XID");
    }
    if (record.info != wal::opcode::catalog::kCreate) {
        return corrupt("validate catalog record", "catalog WAL record has an unknown opcode");
    }
    // Durable-state replay reconciles FCAT's OID frontier and resolves frozen
    // index layouts, but does not invent a catalog WAL payload when no producer
    // exists. The dispatcher keeps this physical relation-install envelope
    // generic for the page cache and relation-discovery composition.
    return {};
}

}  // namespace

[[maybe_unused]] auto
RecoveryDispatcher::dispatch([[maybe_unused]] const wal::DecodedWalRecord& decoded) const
    -> base::Result<RecoveryRecordDispatch> {
    // LAB-S10.Q11:R02-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R02"});
    // LAB-S10.Q11:R02-END
}

[[maybe_unused]] auto RecoveryDispatcher::delta_disposition(
    [[maybe_unused]] const RecoveryRecordDispatch& dispatch,
    [[maybe_unused]] const std::size_t block_index,
    [[maybe_unused]] const base::Lsn page_lsn
) -> base::Result<RecoveryDeltaDisposition> {
    // LAB-S10.Q11:R03-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R03"});
    // LAB-S10.Q11:R03-END
}

}  // namespace db::recovery
