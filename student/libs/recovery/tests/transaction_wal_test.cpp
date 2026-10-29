#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/recovery/transaction_wal.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/backend.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/lock_manager.hpp"
#include "db/transaction/manager.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"
#include "db/wal/segment_storage.hpp"

namespace db::recovery {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x5841'4354'5741'4C31ULL;

class OneShotSyncFailure final : public wal::WalStorageFaultPort {
public:
    void arm() {
        const std::lock_guard lock{mutex_};
        armed_ = true;
    }

    auto before_operation(const wal::WalStorageFaultPoint point) -> base::Status override {
        const std::lock_guard lock{mutex_};
        if (!armed_ || point != wal::WalStorageFaultPoint::kSync) {
            return {};
        }
        armed_ = false;
        return base::fail(base::Error{base::ErrorCode::kIo, "injected completion sync failure"});
    }

private:
    std::mutex mutex_;
    bool armed_{false};
};

auto wal_options(std::shared_ptr<wal::WalStorageFaultPort> fault_port = {})
    -> wal::WalManagerOptions {
    return wal::WalManagerOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = 1U,
        .ring_page_count = 4U,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = true,
        .storage_fault_port = std::move(fault_port),
    };
}

auto register_session(transaction::TransactionManager& manager) -> transaction::ProcSlotLease {
    auto registered = manager.register_session();
    EXPECT_TRUE(registered);
    return std::move(*registered);
}

auto assign_xid(transaction::TransactionManager& manager, transaction::ProcSlotLease& lease)
    -> base::Xid {
    auto xid = manager.assign_xid(lease);
    EXPECT_TRUE(xid);
    return *xid;
}

auto create_backend(transaction::TransactionManager& manager)
    -> std::unique_ptr<transaction::TransactionBackend> {
    auto backend = transaction::TransactionBackend::create(manager);
    EXPECT_TRUE(backend);
    return std::move(*backend);
}

TEST(TransactionCompletionWalAdapterTest, EmitsCanonicalRecordsAndFlushesCommitBeforeClog) {
    auto root = test_support::TestDirectory::create("recovery-transaction-wal");
    ASSERT_TRUE(root);
    auto commit_log_result = transaction::CommitLog::open(
        root->join("xact"),
        transaction::CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(commit_log_result);
    auto commit_log =
        std::make_shared<transaction::CommitLog>(std::move(commit_log_result).value());
    auto wal_result = wal::WalManager::open(root->join("wal"), wal_options());
    ASSERT_TRUE(wal_result);
    auto wal_manager = std::make_shared<wal::WalManager>(std::move(wal_result).value());
    auto startup_state = transaction::TransactionStartupState::from_clean_state(
        commit_log,
        transaction::XidHorizon{}
    );
    ASSERT_TRUE(startup_state);
    auto manager_result = transaction::TransactionManager::from_startup_state(
        std::move(*startup_state),
        std::make_unique<TransactionCompletionWalAdapter>(*wal_manager)
    );
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;

    auto committed = register_session(manager);
    const auto committed_xid = assign_xid(manager, committed);
    ASSERT_TRUE(
        manager.complete(committed, committed_xid, transaction::TransactionStatus::kCommitted)
    );
    EXPECT_TRUE(commit_log->is_committed(committed_xid));
    EXPECT_FALSE(committed.slot()->has_active_transaction());
    EXPECT_EQ(wal_manager->flushed_lsn(), wal_manager->end_lsn());
    EXPECT_GE(wal_manager->successful_sync_count(), 1U);

    auto aborted = register_session(manager);
    const auto aborted_xid = assign_xid(manager, aborted);
    ASSERT_TRUE(manager.complete(aborted, aborted_xid, transaction::TransactionStatus::kAborted));
    EXPECT_TRUE(commit_log->is_aborted(aborted_xid));
    ASSERT_TRUE(wal_manager->flush_through(wal_manager->end_lsn()));
    ASSERT_TRUE(manager.flush_commit_log());

    auto reader = wal::WalReader::open(
        root->join("wal"),
        wal::WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1U,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader);
    std::vector<wal::WalRecord> records;
    for (;;) {
        auto next = reader->read_next();
        ASSERT_TRUE(next);
        if (!next->has_value()) {
            break;
        }
        records.push_back(std::move(next->value().record));
    }
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].xid, committed_xid);
    EXPECT_EQ(records[0].resource_manager, wal::WalResourceManager::kTransaction);
    EXPECT_EQ(records[0].info, wal::opcode::transaction::kCommit);
    EXPECT_TRUE(records[0].blocks.empty());
    EXPECT_TRUE(records[0].main_payload.empty());
    EXPECT_EQ(records[1].xid, aborted_xid);
    EXPECT_EQ(records[1].resource_manager, wal::WalResourceManager::kTransaction);
    EXPECT_EQ(records[1].info, wal::opcode::transaction::kAbort);
    EXPECT_TRUE(records[1].blocks.empty());
    EXPECT_TRUE(records[1].main_payload.empty());

    auto reopened = transaction::CommitLog::open(
        root->join("xact"),
        transaction::CommitLogOpenOptions{.system_identifier = kSystemIdentifier}
    );
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->is_committed(committed_xid));
    EXPECT_TRUE(reopened->is_aborted(aborted_xid));
    ASSERT_TRUE(committed.close());
    ASSERT_TRUE(aborted.close());
}

TEST(TransactionCompletionWalAdapterTest, FailedCommitFlushLeavesTransactionRetryable) {
    auto root = test_support::TestDirectory::create("recovery-transaction-wal-failure");
    ASSERT_TRUE(root);
    auto faults = std::make_shared<OneShotSyncFailure>();
    auto wal_result = wal::WalManager::open(root->join("wal"), wal_options(faults));
    ASSERT_TRUE(wal_result);
    auto wal_manager = std::make_shared<wal::WalManager>(std::move(wal_result).value());
    auto manager_result = transaction::TransactionManager::with_completion_journal(
        std::make_unique<TransactionCompletionWalAdapter>(*wal_manager)
    );
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;
    auto lease = register_session(manager);
    const auto xid = assign_xid(manager, lease);
    ASSERT_TRUE(manager.lock_manager().acquire_table(
        xid,
        base::RelationId{12U},
        transaction::TableLockMode::kAccessExclusive
    ));

    faults->arm();
    auto failed = manager.complete(lease, xid, transaction::TransactionStatus::kCommitted);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_TRUE(manager.commit_log().is_in_progress(xid));
    EXPECT_EQ(lease.slot()->xid(), xid);
    EXPECT_EQ(manager.xid_horizon().latest_completed_xid, base::kInvalidXid);
    EXPECT_EQ(
        manager.lock_manager().held_table_mode(xid, base::RelationId{12U}),
        transaction::TableLockMode::kAccessExclusive
    );

    ASSERT_TRUE(manager.complete(lease, xid, transaction::TransactionStatus::kCommitted));
    EXPECT_TRUE(manager.commit_log().is_committed(xid));
    EXPECT_FALSE(lease.slot()->has_active_transaction());
    EXPECT_EQ(manager.lock_manager().stats(), transaction::LockManagerStats{});
    ASSERT_TRUE(lease.close());
}

TEST(TransactionCompletionWalAdapterTest, ConcurrentCommitsShareWalSyncGroups) {
    constexpr std::size_t backend_count = 8U;
    auto root = test_support::TestDirectory::create("recovery-transaction-group-commit");
    ASSERT_TRUE(root);
    auto wal_result = wal::WalManager::open(root->join("wal"), wal_options());
    ASSERT_TRUE(wal_result);
    auto wal_manager = std::make_shared<wal::WalManager>(std::move(wal_result).value());
    auto manager_result = transaction::TransactionManager::with_completion_journal(
        std::make_unique<TransactionCompletionWalAdapter>(*wal_manager)
    );
    ASSERT_TRUE(manager_result);
    std::vector<std::unique_ptr<transaction::TransactionBackend>> backends;
    backends.reserve(backend_count);
    for (std::size_t index = 0U; index < backend_count; ++index) {
        auto backend = create_backend(**manager_result);
        ASSERT_TRUE(backend->begin_explicit());
        backends.push_back(std::move(backend));
    }

    const auto initial_syncs = wal_manager->successful_sync_count();
    std::barrier ready{static_cast<std::ptrdiff_t>(backend_count + 1U)};
    std::atomic<bool> succeeded{true};
    std::vector<std::thread> workers;
    workers.reserve(backend_count);
    for (const auto& backend : backends) {
        auto* worker_backend = backend.get();
        workers.emplace_back([worker_backend, &ready, &succeeded] {
            ready.arrive_and_wait();
            if (!worker_backend->commit_explicit()) {
                succeeded.store(false, std::memory_order_relaxed);
            }
        });
    }
    ready.arrive_and_wait();
    for (auto& worker : workers) {
        worker.join();
    }
    ASSERT_TRUE(succeeded.load(std::memory_order_relaxed));
    const auto syncs = wal_manager->successful_sync_count() - initial_syncs;
    EXPECT_GT(syncs, 0U);
    EXPECT_LT(syncs, backend_count);
    for (auto& backend : backends) {
        ASSERT_TRUE(backend->close());
    }
}

}  // namespace
}  // namespace db::recovery
