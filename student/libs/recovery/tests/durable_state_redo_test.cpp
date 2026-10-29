#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/recovery/checkpoint.hpp"
#include "db/recovery/durable_state_redo.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/xid.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

constexpr std::uint64_t kDurableStateSystemIdentifier = 0x4455'5241'424C'4531ULL;

class RecordingCatalogRecoveryPort final : public CatalogRecoveryPort {
public:
    auto ensure_next_object_id(const std::uint32_t next_object_id) -> base::Status override {
        observed_object_ids.push_back(next_object_id);
        if (fail_next) {
            fail_next = false;
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "injected catalog recovery publication failure",
                }
            );
        }
        if (throw_allocation_next) {
            throw_allocation_next = false;
            throw std::bad_alloc{};
        }
        if (throw_standard_next) {
            throw_standard_next = false;
            throw std::runtime_error{"catalog recovery adapter failed"};
        }
        return {};
    }

    std::vector<std::uint32_t> observed_object_ids;
    bool fail_next{false};
    bool throw_allocation_next{false};
    bool throw_standard_next{false};
};

auto decoded(const std::uint64_t start_lsn, wal::WalRecord record) -> wal::DecodedWalRecord {
    return wal::DecodedWalRecord{
        .start_lsn = base::Lsn{start_lsn},
        .end_lsn = base::Lsn{start_lsn + 1U},
        .record = std::move(record),
    };
}

auto transaction_completion(
    const std::uint64_t start_lsn,
    const base::Xid xid,
    const transaction::TransactionStatus status
) -> wal::DecodedWalRecord {
    return decoded(
        start_lsn,
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = xid,
            .resource_manager = wal::WalResourceManager::kTransaction,
            .info = status == transaction::TransactionStatus::kCommitted
                        ? wal::opcode::transaction::kCommit
                        : wal::opcode::transaction::kAbort,
            .blocks = {},
            .main_payload = {},
        }
    );
}

auto checkpoint(const std::uint64_t start_lsn, const CheckpointPayload& payload)
    -> std::optional<wal::DecodedWalRecord> {
    auto encoded = encode_checkpoint_payload(payload);
    EXPECT_TRUE(encoded) << encoded.error().to_string();
    if (!encoded) {
        return std::nullopt;
    }
    return decoded(
        start_lsn,
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::kInvalidXid,
            .resource_manager = wal::WalResourceManager::kSystem,
            .info = wal::opcode::system::kCheckpoint,
            .blocks = {},
            .main_payload = std::move(encoded).value(),
        }
    );
}

auto open_durable_clog(const test_support::TestDirectory& root) -> transaction::CommitLog {
    auto commit_log = transaction::CommitLog::open(
        root.join("xact"),
        transaction::CommitLogOpenOptions{
            .system_identifier = kDurableStateSystemIdentifier,
            .create = true,
        }
    );
    EXPECT_TRUE(commit_log) << commit_log.error().to_string();
    return std::move(commit_log).value();
}

void expect_inert(DurableStateReplayer& replayer, const wal::DecodedWalRecord& record) {
    // Deliberately inspects the documented inert state after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(replayer.valid());
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    const auto replayed = replayer.replay(record);
    ASSERT_FALSE(replayed);
    EXPECT_EQ(replayed.error().code(), base::ErrorCode::kUnavailable);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    const auto finalized = replayer.finalize();
    ASSERT_FALSE(finalized);
    EXPECT_EQ(finalized.error().code(), base::ErrorCode::kUnavailable);
}

TEST(DurableStateReplayerTest, ReplaysCompletionRecordsAndFinalizesDurableHorizons) {
    auto root = test_support::TestDirectory::create("durable-state-replay");
    ASSERT_TRUE(root) << root.error().to_string();
    auto commit_log = open_durable_clog(root.value());
    RecordingCatalogRecoveryPort catalog;

    auto replayer = DurableStateReplayer::create(
        DurableStateReplaySeed{
            .next_xid = transaction::kFirstNormalXid,
            .latest_completed_xid = base::kInvalidXid,
            .next_object_id = 1U,
        },
        commit_log,
        catalog
    );
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    const auto redo_marker = decoded(
        10U,
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::kInvalidXid,
            .resource_manager = wal::WalResourceManager::kSystem,
            .info = wal::opcode::system::kCheckpointRedo,
            .blocks = {},
            .main_payload = {},
        }
    );
    auto replayed = replayer->replay(redo_marker);
    ASSERT_TRUE(replayed) << replayed.error().to_string();
    EXPECT_EQ(replayed.value(), DurableStateReplayDisposition::kIgnored);

    replayed = replayer->replay(
        transaction_completion(20U, base::Xid{4U}, transaction::TransactionStatus::kCommitted)
    );
    ASSERT_TRUE(replayed) << replayed.error().to_string();
    EXPECT_EQ(replayed.value(), DurableStateReplayDisposition::kApplied);
    replayed = replayer->replay(
        transaction_completion(30U, base::Xid{7U}, transaction::TransactionStatus::kAborted)
    );
    ASSERT_TRUE(replayed) << replayed.error().to_string();
    EXPECT_EQ(replayed.value(), DurableStateReplayDisposition::kApplied);

    const auto final_checkpoint = checkpoint(
        40U,
        CheckpointPayload{
            .next_xid = base::Xid{9U},
            .latest_completed_xid = base::Xid{7U},
            .next_object_id = 44U,
        }
    );
    ASSERT_TRUE(final_checkpoint.has_value());
    replayed = replayer->replay(final_checkpoint.value());
    ASSERT_TRUE(replayed) << replayed.error().to_string();
    EXPECT_EQ(replayed.value(), DurableStateReplayDisposition::kApplied);

    auto finalized = replayer->finalize();
    ASSERT_TRUE(finalized) << finalized.error().to_string();
    EXPECT_EQ(
        finalized.value(),
        (DurableStateReplayResult{
            .next_xid = base::Xid{9U},
            .latest_completed_xid = base::Xid{8U},
            .next_object_id = 44U,
            .last_lsn = base::Lsn{41U},
            .transaction_records_replayed = 2U,
        })
    );
    EXPECT_TRUE(commit_log.is_committed(base::Xid{4U}));
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{7U}));
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{1U}));
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{8U}));
    EXPECT_TRUE(commit_log.is_in_progress(base::Xid{9U}));
    EXPECT_EQ(commit_log.next_xid(), base::Xid{9U});
    EXPECT_EQ(commit_log.durable_next_xid(), base::Xid{9U});
    EXPECT_EQ(catalog.observed_object_ids, std::vector<std::uint32_t>{44U});

    auto finalized_again = replayer->finalize();
    ASSERT_TRUE(finalized_again) << finalized_again.error().to_string();
    EXPECT_EQ(finalized_again.value(), finalized.value());
    EXPECT_EQ(catalog.observed_object_ids, std::vector<std::uint32_t>{44U});

    auto after_finalize = replayer->replay(
        transaction_completion(50U, base::Xid{9U}, transaction::TransactionStatus::kCommitted)
    );
    ASSERT_FALSE(after_finalize);
    EXPECT_EQ(after_finalize.error().code(), base::ErrorCode::kUnavailable);
}

TEST(DurableStateReplayerTest, MovingTransfersTheOnlyActiveServiceBindingsAndReplayCursor) {
    auto root = test_support::TestDirectory::create("durable-state-replay-moved-from");
    ASSERT_TRUE(root) << root.error().to_string();
    auto commit_log = open_durable_clog(root.value());
    RecordingCatalogRecoveryPort catalog;

    auto created = DurableStateReplayer::create(DurableStateReplaySeed{}, commit_log, catalog);
    ASSERT_TRUE(created) << created.error().to_string();
    DurableStateReplayer replayer{std::move(created).value()};
    ASSERT_TRUE(replayer.valid());
    const auto committed =
        transaction_completion(10U, base::Xid{4U}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(replayer.replay(committed));

    DurableStateReplayer moved{std::move(replayer)};
    const auto rejected_record =
        transaction_completion(20U, base::Xid{5U}, transaction::TransactionStatus::kAborted);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert(replayer, rejected_record);
    ASSERT_TRUE(moved.valid());

    auto assignment_target =
        DurableStateReplayer::create(DurableStateReplaySeed{}, commit_log, catalog);
    ASSERT_TRUE(assignment_target) << assignment_target.error().to_string();
    assignment_target.value() = std::move(moved);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert(moved, rejected_record);
    ASSERT_TRUE(assignment_target->valid());

    auto finalized = assignment_target->finalize();
    ASSERT_TRUE(finalized) << finalized.error().to_string();
    EXPECT_EQ(finalized->last_lsn, base::Lsn{11U});
    EXPECT_EQ(finalized->transaction_records_replayed, 1U);
    EXPECT_TRUE(commit_log.is_committed(base::Xid{4U}));
    EXPECT_TRUE(commit_log.is_in_progress(base::Xid{5U}));
    EXPECT_EQ(catalog.observed_object_ids, std::vector<std::uint32_t>{1U});
}

TEST(DurableStateReplayerTest, RejectsCheckpointWhoseDeclaredFinalXidIsStillInProgress) {
    transaction::CommitLog commit_log;
    RecordingCatalogRecoveryPort catalog;
    auto replayer = DurableStateReplayer::create(DurableStateReplaySeed{}, commit_log, catalog);
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    const auto malformed_horizon = checkpoint(
        10U,
        CheckpointPayload{
            .next_xid = base::Xid{4U},
            .latest_completed_xid = base::Xid{3U},
            .next_object_id = 9U,
        }
    );
    ASSERT_TRUE(malformed_horizon.has_value());
    auto rejected = replayer->replay(malformed_horizon.value());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(commit_log.next_xid(), transaction::kFirstNormalXid);
    EXPECT_TRUE(catalog.observed_object_ids.empty());
}

TEST(DurableStateReplayerTest, RejectsConflictingCompletionWithoutCorruptingPriorReplay) {
    transaction::CommitLog commit_log;
    RecordingCatalogRecoveryPort catalog;
    auto replayer = DurableStateReplayer::create(DurableStateReplaySeed{}, commit_log, catalog);
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    auto committed = replayer->replay(
        transaction_completion(10U, base::Xid{2U}, transaction::TransactionStatus::kCommitted)
    );
    ASSERT_TRUE(committed) << committed.error().to_string();
    auto contradictory = replayer->replay(
        transaction_completion(20U, base::Xid{2U}, transaction::TransactionStatus::kAborted)
    );
    ASSERT_FALSE(contradictory);
    EXPECT_EQ(contradictory.error().code(), base::ErrorCode::kInvariantViolation);
    EXPECT_TRUE(commit_log.is_committed(base::Xid{2U}));

    auto finalized = replayer->finalize();
    ASSERT_TRUE(finalized) << finalized.error().to_string();
    EXPECT_EQ(finalized->next_xid, base::Xid{3U});
    EXPECT_EQ(finalized->latest_completed_xid, base::Xid{2U});
    EXPECT_EQ(catalog.observed_object_ids, std::vector<std::uint32_t>{1U});
}

TEST(DurableStateReplayerTest, RejectsOutOfOrderRecordBeforeMutatingClog) {
    transaction::CommitLog commit_log;
    RecordingCatalogRecoveryPort catalog;
    auto replayer = DurableStateReplayer::create(DurableStateReplaySeed{}, commit_log, catalog);
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    auto first = replayer->replay(
        transaction_completion(20U, base::Xid{4U}, transaction::TransactionStatus::kCommitted)
    );
    ASSERT_TRUE(first) << first.error().to_string();
    auto out_of_order = replayer->replay(
        transaction_completion(10U, base::Xid{5U}, transaction::TransactionStatus::kCommitted)
    );
    ASSERT_FALSE(out_of_order);
    EXPECT_EQ(out_of_order.error().code(), base::ErrorCode::kCorruption);
    EXPECT_TRUE(commit_log.is_committed(base::Xid{4U}));
    EXPECT_TRUE(commit_log.is_in_progress(base::Xid{5U}));
}

TEST(DurableStateReplayerTest, ValidatesSeedHorizonAgainstExistingClog) {
    transaction::CommitLog commit_log;
    RecordingCatalogRecoveryPort catalog;
    auto rejected = DurableStateReplayer::create(
        DurableStateReplaySeed{
            .next_xid = base::Xid{4U},
            .latest_completed_xid = base::Xid{3U},
            .next_object_id = 1U,
        },
        commit_log,
        catalog
    );
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);

    ASSERT_TRUE(commit_log.set_status(base::Xid{3U}, transaction::TransactionStatus::kCommitted));
    auto accepted = DurableStateReplayer::create(
        DurableStateReplaySeed{
            .next_xid = base::Xid{4U},
            .latest_completed_xid = base::Xid{3U},
            .next_object_id = 1U,
        },
        commit_log,
        catalog
    );
    ASSERT_TRUE(accepted) << accepted.error().to_string();
}

TEST(DurableStateReplayerTest, PreservesAnAlreadyAllocatedClogHorizon) {
    transaction::CommitLog commit_log;
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{6U}));
    RecordingCatalogRecoveryPort catalog;
    auto replayer = DurableStateReplayer::create(
        DurableStateReplaySeed{
            .next_xid = base::Xid{3U},
            .latest_completed_xid = base::kInvalidXid,
            .next_object_id = 1U,
        },
        commit_log,
        catalog
    );
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    auto finalized = replayer->finalize();
    ASSERT_TRUE(finalized) << finalized.error().to_string();
    EXPECT_EQ(finalized->next_xid, base::Xid{7U});
    EXPECT_EQ(finalized->latest_completed_xid, base::Xid{6U});
    for (std::uint32_t xid = 1U; xid < 7U; ++xid) {
        EXPECT_TRUE(commit_log.is_aborted(base::Xid{xid}));
    }
}

TEST(DurableStateReplayerTest, AbortsAnUnfinishedPhysicalRecordBeforeReusingItsXid) {
    transaction::CommitLog commit_log;
    RecordingCatalogRecoveryPort catalog;
    auto replayer = DurableStateReplayer::create(DurableStateReplaySeed{}, commit_log, catalog);
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    auto physical_record = replayer->replay(decoded(
        10U,
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{8U},
            .resource_manager = wal::WalResourceManager::kCatalog,
            .info = wal::opcode::catalog::kCreate,
            .blocks = {wal::WalBlockReference{
                .relation_id = base::RelationId{71U},
                .page_id = base::PageId{0U},
                .will_initialize = false,
                .standard_page = true,
                .full_page_image = std::nullopt,
                .delta = {std::byte{0xA5U}},
            }},
            .main_payload = {},
        }
    ));
    ASSERT_TRUE(physical_record) << physical_record.error().to_string();
    EXPECT_EQ(physical_record.value(), DurableStateReplayDisposition::kIgnored);

    auto finalized = replayer->finalize();
    ASSERT_TRUE(finalized) << finalized.error().to_string();
    EXPECT_EQ(finalized->next_xid, base::Xid{9U});
    EXPECT_EQ(finalized->latest_completed_xid, base::Xid{8U});
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{8U}));
}

TEST(DurableStateReplayerTest, RetriesCatalogReconciliationAfterDurableClogCompletion) {
    transaction::CommitLog commit_log;
    RecordingCatalogRecoveryPort catalog;
    catalog.fail_next = true;
    auto replayer = DurableStateReplayer::create(
        DurableStateReplaySeed{
            .next_xid = base::Xid{4U},
            .latest_completed_xid = base::kInvalidXid,
            .next_object_id = 17U,
        },
        commit_log,
        catalog
    );
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    auto first_attempt = replayer->finalize();
    ASSERT_FALSE(first_attempt);
    EXPECT_EQ(first_attempt.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(catalog.observed_object_ids, std::vector<std::uint32_t>{17U});
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{1U}));
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{3U}));
    EXPECT_EQ(commit_log.durable_next_xid(), base::Xid{4U});

    auto recovered = replayer->finalize();
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    EXPECT_EQ(catalog.observed_object_ids, (std::vector<std::uint32_t>{17U, 17U}));
    auto cached = replayer->finalize();
    ASSERT_TRUE(cached) << cached.error().to_string();
    EXPECT_EQ(catalog.observed_object_ids, (std::vector<std::uint32_t>{17U, 17U}));
}

TEST(DurableStateReplayerTest, ContainsCatalogPortExceptionsAndRetriesFinalization) {
    transaction::CommitLog commit_log;
    RecordingCatalogRecoveryPort catalog;
    catalog.throw_allocation_next = true;
    auto replayer = DurableStateReplayer::create(
        DurableStateReplaySeed{
            .next_xid = base::Xid{4U},
            .latest_completed_xid = base::kInvalidXid,
            .next_object_id = 19U,
        },
        commit_log,
        catalog
    );
    ASSERT_TRUE(replayer) << replayer.error().to_string();

    auto exhausted = replayer->finalize();
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ(commit_log.durable_next_xid(), base::Xid{4U});

    catalog.throw_standard_next = true;
    auto internal = replayer->finalize();
    ASSERT_FALSE(internal);
    EXPECT_EQ(internal.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        internal.error().message().find("catalog recovery adapter failed"),
        std::string_view::npos
    );

    auto recovered = replayer->finalize();
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    EXPECT_EQ(catalog.observed_object_ids, (std::vector<std::uint32_t>{19U, 19U, 19U}));
    auto cached = replayer->finalize();
    ASSERT_TRUE(cached) << cached.error().to_string();
    EXPECT_EQ(catalog.observed_object_ids, (std::vector<std::uint32_t>{19U, 19U, 19U}));
}

}  // namespace
}  // namespace db::recovery
