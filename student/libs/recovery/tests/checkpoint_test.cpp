#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/store.hpp"
#include "db/recovery/checkpoint.hpp"
#include "db/recovery/transaction_wal.hpp"
#include "db/recovery/wal_durability.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/xid.hpp"
#include "db/types/data_type.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

constexpr std::uint64_t kCheckpointSystemIdentifier = 0xC0DE'CAFE'1234'5678ULL;

auto catalog_options() -> catalog::CatalogStoreOptions {
    return catalog::CatalogStoreOptions{
        .decode = catalog::CatalogDecodeOptions{
            .row_size_limit = catalog::RowSizeLimit{.maximum_encoded_size = 4'036U},
        },
    };
}

auto initial_control() -> wal::WalControlImage {
    return wal::WalControlImage{
        .system_identifier = kCheckpointSystemIdentifier,
        .generation = wal::kInitialWalControlGeneration,
        .checkpoint_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .next_xid = transaction::kFirstNormalXid,
        .latest_completed_xid = base::kInvalidXid,
        .next_object_id = 1U,
        .state = wal::DatabaseState::kInProduction,
        .checksum = 0U,
    };
}

class CheckpointCoordinatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto root = test_support::TestDirectory::create("recovery-checkpoint");
        ASSERT_TRUE(root) << root.error().to_string();
        root_.emplace(std::move(root).value());

        auto control = wal::WalControlStore::create(root_->join("control"), initial_control());
        ASSERT_TRUE(control) << control.error().to_string();
        control_.emplace(std::move(control).value());

        auto wal_manager = wal::WalManager::open(
            root_->join("wal"),
            wal::WalManagerOptions{
                .system_identifier = kCheckpointSystemIdentifier,
                .timeline = 1U,
                .ring_page_count = 4U,
                .retention_lsn = base::kInvalidLsn,
                .redo_lsn = base::kInvalidLsn,
                .create = true,
            }
        );
        ASSERT_TRUE(wal_manager) << wal_manager.error().to_string();
        wal_manager_ = std::make_shared<wal::WalManager>(std::move(wal_manager).value());

        auto commit_log = transaction::CommitLog::open(
            root_->join("xact"),
            transaction::CommitLogOpenOptions{
                .system_identifier = kCheckpointSystemIdentifier,
                .create = true,
            }
        );
        ASSERT_TRUE(commit_log) << commit_log.error().to_string();
        commit_log_ = std::make_shared<transaction::CommitLog>(std::move(commit_log).value());

        auto startup_state = transaction::TransactionStartupState::from_clean_state(
            commit_log_,
            transaction::XidHorizon{
                .next_xid = transaction::kFirstNormalXid,
                .latest_completed_xid = base::kInvalidXid,
            }
        );
        ASSERT_TRUE(startup_state) << startup_state.error().to_string();
        auto transaction_manager = transaction::TransactionManager::from_startup_state(
            std::move(*startup_state),
            std::make_unique<TransactionCompletionWalAdapter>(*wal_manager_)
        );
        ASSERT_TRUE(transaction_manager) << transaction_manager.error().to_string();
        transaction_manager_ = std::move(transaction_manager).value();

        auto catalog = catalog::CatalogStore::create(root_->join("catalog"), catalog_options());
        ASSERT_TRUE(catalog) << catalog.error().to_string();
        catalog_.emplace(std::move(catalog).value());

        auto disk = storage::FileDiskManager::open(root_->join("relations"));
        ASSERT_TRUE(disk) << disk.error().to_string();
        auto buffer_pool = storage::BufferPool::create(
            4U,
            std::move(disk).value(),
            nullptr,
            std::make_unique<WalPageDurabilityAdapter>(wal_manager_)
        );
        ASSERT_TRUE(buffer_pool) << buffer_pool.error().to_string();
        buffer_pool_ = std::move(buffer_pool).value();
    }

    void TearDown() override {
        if (buffer_pool_ != nullptr) {
            EXPECT_TRUE(buffer_pool_->shutdown());
            buffer_pool_.reset();
        }
        transaction_manager_.reset();
        commit_log_.reset();
        if (wal_manager_ != nullptr) {
            EXPECT_TRUE(wal_manager_->shutdown());
            wal_manager_.reset();
        }
        catalog_.reset();
        control_.reset();
        root_.reset();
    }

    auto checkpoint_context() -> CheckpointContext {
        return CheckpointContext{
            .catalog = catalog_.value(),
            .transaction_manager = *transaction_manager_,
            .buffer_pool = *buffer_pool_,
            .control_store = control_.value(),
            .wal_manager = *wal_manager_,
        };
    }

    auto find_record(const base::Lsn start_lsn) const -> std::optional<wal::DecodedWalRecord> {
        auto reader = wal::WalReader::open(
            root_->join("wal"),
            wal::WalReaderOptions{
                .system_identifier = kCheckpointSystemIdentifier,
                .timeline = 1U,
                .retention_lsn = base::kInvalidLsn,
            }
        );
        EXPECT_TRUE(reader) << reader.error().to_string();
        if (!reader) {
            return std::nullopt;
        }
        for (;;) {
            auto record = reader->read_next();
            EXPECT_TRUE(record) << record.error().to_string();
            if (!record || !record->has_value()) {
                return std::nullopt;
            }
            if (record->value().start_lsn == start_lsn) {
                return std::move(record).value();
            }
        }
    }

    std::optional<test_support::TestDirectory> root_;
    std::optional<wal::WalControlStore> control_;
    std::shared_ptr<wal::WalManager> wal_manager_;
    std::shared_ptr<transaction::CommitLog> commit_log_;
    std::unique_ptr<transaction::TransactionManager> transaction_manager_;
    std::optional<catalog::CatalogStore> catalog_;
    std::unique_ptr<storage::BufferPool> buffer_pool_;
    CheckpointCoordinator coordinator_;
};

TEST(CheckpointPayloadTest, RoundTripsVersionedCheckpointHorizon) {
    const CheckpointPayload expected{
        .next_xid = base::Xid{91U},
        .latest_completed_xid = base::Xid{90U},
        .next_object_id = 47U,
    };

    auto encoded = encode_checkpoint_payload(expected);
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    ASSERT_EQ(encoded->size(), kCheckpointPayloadSize);
    auto decoded = decode_checkpoint_payload(*encoded);
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(decoded.value(), expected);
}

TEST(CheckpointPayloadTest, RejectsMalformedVersionAndHorizon) {
    const CheckpointPayload payload{
        .next_xid = base::Xid{91U},
        .latest_completed_xid = base::Xid{90U},
        .next_object_id = 47U,
    };
    auto encoded = encode_checkpoint_payload(payload);
    ASSERT_TRUE(encoded);

    auto unsupported_version = encoded.value();
    unsupported_version.at(4U) = std::byte{2U};
    auto rejected_version = decode_checkpoint_payload(unsupported_version);
    ASSERT_FALSE(rejected_version);
    EXPECT_EQ(rejected_version.error().code(), base::ErrorCode::kUnsupported);

    auto invalid_horizon = encoded.value();
    invalid_horizon.at(12U) = std::byte{91U};
    auto rejected_horizon = decode_checkpoint_payload(invalid_horizon);
    ASSERT_FALSE(rejected_horizon);
    EXPECT_EQ(rejected_horizon.error().code(), base::ErrorCode::kCorruption);
}

TEST_F(CheckpointCoordinatorTest, PublishesDurableRedoAndCheckpointHorizon) {
    auto lease = transaction_manager_->register_session();
    ASSERT_TRUE(lease) << lease.error().to_string();
    auto assigned_xid = transaction_manager_->assign_xid(*lease);
    ASSERT_TRUE(assigned_xid) << assigned_xid.error().to_string();
    ASSERT_TRUE(transaction_manager_->complete(
        *lease,
        assigned_xid.value(),
        transaction::TransactionStatus::kCommitted
    ));
    ASSERT_TRUE(lease->close());
    const auto expected_horizon = transaction_manager_->xid_horizon();

    auto schema = catalog::Schema::create(
        std::vector<catalog::Column>{
            catalog::Column{.name = "id", .data_type = types::kInt64Type},
        },
        catalog::RowSizeLimit{.maximum_encoded_size = 4'036U}
    );
    ASSERT_TRUE(schema) << schema.error().to_string();
    auto table = catalog_->create_table(
        catalog::CreateTableRequest{
            .name = "checkpoint_horizon",
            .schema = std::move(schema).value(),
            .fillfactor = catalog::kDefaultHeapFillfactor,
            .if_not_exists = false,
        }
    );
    ASSERT_TRUE(table) << table.error().to_string();
    ASSERT_TRUE(table->has_value());
    auto expected_ids = catalog_->id_state();
    ASSERT_TRUE(expected_ids) << expected_ids.error().to_string();

    auto pre_checkpoint_record = wal_manager_->insert(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::kInvalidXid,
            .resource_manager = wal::WalResourceManager::kSystem,
            .info = wal::opcode::system::kCheckpointRedo,
            .blocks = {},
            .main_payload = {},
        }
    );
    ASSERT_TRUE(pre_checkpoint_record) << pre_checkpoint_record.error().to_string();

    const storage::PageAddress address{base::RelationId{77U}, base::PageId{0U}};
    {
        auto pin = buffer_pool_->allocate_page(base::RelationId{77U}, storage::PageKind::kHeap);
        ASSERT_TRUE(pin) << pin.error().to_string();
        EXPECT_EQ(pin->address(), address);
        auto write = pin->write_latch();
        ASSERT_TRUE(write) << write.error().to_string();
        ASSERT_TRUE(write->page()->set_lsn(pre_checkpoint_record->end_lsn));
        auto payload = write->page()->mutable_view(storage::kPageHeaderSize, 1U);
        ASSERT_TRUE(payload) << payload.error().to_string();
        payload->front() = std::byte{0xA5U};
    }

    auto completed =
        coordinator_.run(checkpoint_context(), CheckpointOptions{.dirty_page_batch_frames = 1U});
    ASSERT_TRUE(completed) << completed.error().to_string();
    EXPECT_EQ(completed->marked_dirty_pages, 1U);
    EXPECT_EQ(completed->flushed_dirty_pages, 1U);
    EXPECT_NE(completed->redo_lsn, base::kInvalidLsn);
    EXPECT_NE(completed->checkpoint_lsn, base::kInvalidLsn);
    EXPECT_GT(completed->checkpoint_lsn.value(), completed->redo_lsn.value());
    EXPECT_GT(completed->checkpoint_end_lsn.value(), completed->checkpoint_lsn.value());

    const auto control = control_->current();
    EXPECT_EQ(control.generation, wal::kInitialWalControlGeneration + 1U);
    EXPECT_EQ(control.state, wal::DatabaseState::kInProduction);
    EXPECT_EQ(control.redo_lsn, completed->redo_lsn);
    EXPECT_EQ(control.checkpoint_lsn, completed->checkpoint_lsn);
    EXPECT_EQ(control.next_xid, expected_horizon.next_xid);
    EXPECT_EQ(control.latest_completed_xid, expected_horizon.latest_completed_xid);
    EXPECT_EQ(control.next_object_id, expected_ids->next_oid);

    auto frame = buffer_pool_->frame_snapshot(address);
    ASSERT_TRUE(frame) << frame.error().to_string();
    EXPECT_FALSE(frame->dirty);
    EXPECT_FALSE(frame->checkpoint_needed);

    const auto redo_record = find_record(completed->redo_lsn);
    ASSERT_TRUE(redo_record.has_value());
    EXPECT_EQ(redo_record->record.resource_manager, wal::WalResourceManager::kSystem);
    EXPECT_EQ(redo_record->record.info, wal::opcode::system::kCheckpointRedo);
    EXPECT_TRUE(redo_record->record.main_payload.empty());

    const auto final_record = find_record(completed->checkpoint_lsn);
    ASSERT_TRUE(final_record.has_value());
    EXPECT_EQ(final_record->end_lsn, completed->checkpoint_end_lsn);
    EXPECT_EQ(final_record->record.resource_manager, wal::WalResourceManager::kSystem);
    EXPECT_EQ(final_record->record.info, wal::opcode::system::kCheckpoint);
    EXPECT_TRUE(final_record->record.blocks.empty());
    auto checkpoint_payload = decode_checkpoint_payload(final_record->record.main_payload);
    ASSERT_TRUE(checkpoint_payload) << checkpoint_payload.error().to_string();
    EXPECT_EQ(checkpoint_payload->next_xid, control.next_xid);
    EXPECT_EQ(checkpoint_payload->latest_completed_xid, control.latest_completed_xid);
    EXPECT_EQ(checkpoint_payload->next_object_id, control.next_object_id);
}

TEST_F(CheckpointCoordinatorTest, QuiescentModeRejectsAuthoritativeActiveTransactionsBeforeMarker) {
    auto lease = transaction_manager_->register_session();
    ASSERT_TRUE(lease);
    auto xid = transaction_manager_->assign_xid(*lease);
    ASSERT_TRUE(xid);
    const auto before = wal_manager_->end_lsn();
    auto rejected =
        coordinator_.run(checkpoint_context(), CheckpointOptions{.require_quiescence = true});
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(wal_manager_->end_lsn(), before);
    EXPECT_EQ(control_->current().generation, wal::kInitialWalControlGeneration);
    ASSERT_TRUE(
        transaction_manager_->complete(*lease, *xid, transaction::TransactionStatus::kAborted)
    );
    ASSERT_TRUE(lease->close());
}

TEST_F(CheckpointCoordinatorTest, QuiescentModeResolvesAbandonedTransactionsBeforePublishing) {
    base::Xid xid = base::kInvalidXid;
    {
        auto lease = transaction_manager_->register_session();
        ASSERT_TRUE(lease);
        auto assigned = transaction_manager_->assign_xid(*lease);
        ASSERT_TRUE(assigned);
        xid = *assigned;
    }
    ASSERT_TRUE(transaction_manager_->proc_array().has_at_least_active_transactions(1U));
    auto completed =
        coordinator_.run(checkpoint_context(), CheckpointOptions{.require_quiescence = true});
    ASSERT_TRUE(completed) << completed.error().to_string();
    EXPECT_TRUE(transaction_manager_->commit_log().is_aborted(xid));
    EXPECT_FALSE(transaction_manager_->proc_array().has_at_least_active_transactions(1U));
    EXPECT_EQ(control_->current().latest_completed_xid, xid);
}

class FailingAbortJournal final : public transaction::CompletionJournalPort {
public:
    explicit FailingAbortJournal(wal::WalManager& manager)
        : delegate_{manager} {}

    bool fail{true};

    auto append_completion(const base::Xid xid, const transaction::CompletionOutcome outcome)
        -> base::Result<base::Lsn> override {
        if (fail) {
            return base::fail(
                base::Error{base::ErrorCode::kIo, "injected abandoned abort failure"}
            );
        }
        return delegate_.append_completion(xid, outcome);
    }

    auto flush_through(const base::Lsn end, const std::chrono::microseconds delay)
        -> base::Status override {
        return delegate_.flush_through(end, delay);
    }

private:
    TransactionCompletionWalAdapter delegate_;
};

TEST_F(CheckpointCoordinatorTest, FailedAbandonedCleanupCannotPublishOrRecycleAStaticCheckpoint) {
    auto startup = transaction::TransactionStartupState::from_clean_state(
        commit_log_,
        transaction_manager_->xid_horizon()
    );
    ASSERT_TRUE(startup);
    auto journal = std::make_unique<FailingAbortJournal>(*wal_manager_);
    auto* journal_view = journal.get();
    auto manager = transaction::TransactionManager::from_startup_state(
        std::move(*startup),
        std::move(journal)
    );
    ASSERT_TRUE(manager);
    transaction_manager_ = std::move(*manager);
    base::Xid xid = base::kInvalidXid;
    {
        auto lease = transaction_manager_->register_session();
        ASSERT_TRUE(lease);
        auto assigned = transaction_manager_->assign_xid(*lease);
        ASSERT_TRUE(assigned);
        xid = *assigned;
    }
    const auto before = wal_manager_->end_lsn();
    const auto retention = wal_manager_->retention_lsn();
    auto failed =
        coordinator_.run(checkpoint_context(), CheckpointOptions{.require_quiescence = true});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(wal_manager_->end_lsn(), before);
    EXPECT_EQ(wal_manager_->retention_lsn(), retention);
    EXPECT_EQ(control_->current().generation, wal::kInitialWalControlGeneration);
    EXPECT_TRUE(transaction_manager_->commit_log().is_in_progress(xid));
    journal_view->fail = false;
    ASSERT_TRUE(
        coordinator_.run(checkpoint_context(), CheckpointOptions{.require_quiescence = true})
    );
    EXPECT_TRUE(transaction_manager_->commit_log().is_aborted(xid));
}

TEST_F(CheckpointCoordinatorTest, RejectsZeroBatchWithoutPublishingAMarker) {
    const auto end_before = wal_manager_->end_lsn();
    auto rejected =
        coordinator_.run(checkpoint_context(), CheckpointOptions{.dirty_page_batch_frames = 0U});
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kInvalidArgument);
    const auto control = control_->current();
    EXPECT_EQ(control.generation, wal::kInitialWalControlGeneration);
    EXPECT_EQ(control.checkpoint_lsn, base::kInvalidLsn);
    EXPECT_EQ(control.redo_lsn, base::kInvalidLsn);
    EXPECT_EQ(control.state, wal::DatabaseState::kInProduction);
    EXPECT_EQ(wal_manager_->end_lsn(), end_before);
}

TEST_F(CheckpointCoordinatorTest, SerializesConcurrentCheckpointGenerations) {
    std::barrier start{3};
    std::array<std::optional<base::Result<CheckpointResult>>, 2U> results;
    const auto run = [this, &start, &results](const std::size_t index) {
        start.arrive_and_wait();
        results.at(index).emplace(coordinator_.run(checkpoint_context()));
    };

    std::thread first{run, 0U};
    std::thread second{run, 1U};
    start.arrive_and_wait();
    first.join();
    second.join();

    ASSERT_TRUE(results[0U].has_value());
    ASSERT_TRUE(results[1U].has_value());
    ASSERT_TRUE(results[0U].value()) << results[0U]->error().to_string();
    ASSERT_TRUE(results[1U].value()) << results[1U]->error().to_string();
    EXPECT_NE(results[0U]->value().redo_lsn, results[1U]->value().redo_lsn);
    EXPECT_NE(results[0U]->value().checkpoint_lsn, results[1U]->value().checkpoint_lsn);
    EXPECT_EQ(control_->current().generation, wal::kInitialWalControlGeneration + 2U);
}

}  // namespace
}  // namespace db::recovery
