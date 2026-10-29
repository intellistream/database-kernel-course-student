#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/io/posix.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/format.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"
#include "db/wal/ring_buffer.hpp"
#include "db/wal/segment_storage.hpp"

#include "wal_fault_port.hpp"

namespace db::wal {
namespace {

using namespace std::chrono_literals;

inline constexpr std::uint64_t kSystemIdentifier = 0x8877'6655'4433'2211ULL;

auto manager_options(
    const bool create,
    const std::size_t ring_pages = 4,
    std::shared_ptr<WalStorageFaultPort> storage_fault_port = nullptr
) -> WalManagerOptions {
    return WalManagerOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = 1,
        .ring_page_count = ring_pages,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = create,
        .storage_fault_port = std::move(storage_fault_port),
    };
}

auto record(const std::uint32_t xid, const std::size_t payload_size = 8) -> WalRecord {
    std::vector<std::byte> payload(payload_size, static_cast<std::byte>(xid & 0xFFU));
    return WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{xid},
        .resource_manager = WalResourceManager::kTransaction,
        .info = 0,
        .blocks = {},
        .main_payload = std::move(payload),
    };
}

auto page_record(const std::uint32_t xid) -> WalRecord {
    return WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{xid},
        .resource_manager = WalResourceManager::kHeap,
        .info = opcode::heap::kUpdate,
        .blocks = {WalBlockReference{
            .relation_id = base::RelationId{71},
            .page_id = base::PageId{9},
            .will_initialize = false,
            .standard_page = true,
            .full_page_image = std::nullopt,
            .delta = encode_page_delta(
                         std::array{
                             PageDeltaPatch{.offset = 80, .bytes = {std::byte{0xA5}}},
                         }
            )
                         .value(),
        }},
        .main_payload = {},
    };
}

auto page_image() -> WalPageImage {
    WalPageImage image{};
    for (std::size_t index = 0U; index < 32U; ++index) {
        image.at(index) = static_cast<std::byte>(index);
    }
    image.at(4032U) = std::byte{0xA5};
    image.back() = std::byte{0x5A};
    return image;
}

inline constexpr WalPageImageHole kPageImageHole{.offset = 32U, .length = 4000U};

auto initial_control() -> WalControlImage {
    return WalControlImage{
        .system_identifier = kSystemIdentifier,
        .generation = kInitialWalControlGeneration,
        .checkpoint_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .next_xid = base::Xid{1},
        .latest_completed_xid = base::kInvalidXid,
        .next_object_id = 1,
        .state = DatabaseState::kInProduction,
        .checksum = 0,
    };
}

TEST(WalManagerTest, InsertsFlushesAndResumesAtTheDurableTail) {
    auto root = test_support::TestDirectory::create("wal-manager-reopen");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    WalInsertResult second{};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        const auto first = manager->insert(record(1));
        ASSERT_TRUE(first);
        const auto inserted = manager->insert(record(2));
        ASSERT_TRUE(inserted);
        second = inserted.value();
        EXPECT_EQ(first->end_lsn, second.start_lsn);
        EXPECT_EQ(manager->written_lsn(), kInitialWalRingLsn);
        ASSERT_TRUE(manager->flush_through(second.end_lsn));
        EXPECT_EQ(manager->flushed_lsn(), second.end_lsn);
        ASSERT_TRUE(manager->shutdown());
    }

    auto reopened = WalManager::open(directory, manager_options(false));
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->end_lsn(), second.end_lsn);
    const auto third = reopened->insert(record(3));
    ASSERT_TRUE(third);
    EXPECT_EQ(third->start_lsn, second.end_lsn);
    ASSERT_TRUE(reopened->flush_through(third->end_lsn));
}

TEST(WalManagerTest, BackgroundWriterWritesOnlyCompletedPagesWithoutSyncing) {
    auto root = test_support::TestDirectory::create("wal-manager-background");
    ASSERT_TRUE(root);
    auto manager = WalManager::open(root->join("wal"), manager_options(true));
    ASSERT_TRUE(manager);
    const auto inserted = manager->insert(record(1, kWalPageSize));
    ASSERT_TRUE(inserted);
    const base::Lsn completed_page{
        inserted->end_lsn.value() - inserted->end_lsn.value() % kWalPageSize
    };
    ASSERT_GT(completed_page.value(), kInitialWalRingLsn.value());
    ASSERT_LT(completed_page.value(), inserted->end_lsn.value());

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (manager->written_lsn().value() < completed_page.value()
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    EXPECT_EQ(manager->written_lsn(), completed_page);
    EXPECT_EQ(manager->flushed_lsn(), kInitialWalRingLsn);
    ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
}

TEST(WalManagerTest, BackgroundWriteFailureRemainsForegroundRetryable) {
    auto root = test_support::TestDirectory::create("wal-manager-background-write-failure");
    ASSERT_TRUE(root);
    auto faults = std::make_shared<test::OneShotWalStorageFaultPort>();
    auto manager = WalManager::open(root->join("wal"), manager_options(true, 4, faults));
    ASSERT_TRUE(manager);
    faults->arm(WalStorageFaultPoint::kWrite);
    const auto inserted = manager->insert(record(1, kWalPageSize));
    ASSERT_TRUE(inserted);

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!faults->triggered() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    ASSERT_TRUE(faults->triggered());
    EXPECT_EQ(manager->written_lsn(), kInitialWalRingLsn);
    ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
    EXPECT_EQ(manager->written_lsn(), inserted->end_lsn);
    EXPECT_EQ(manager->flushed_lsn(), inserted->end_lsn);
}

TEST(WalManagerTest, BackgroundPortExceptionCannotEscapeTheWorkerThread) {
    auto root = test_support::TestDirectory::create("wal-manager-background-port-exception");
    ASSERT_TRUE(root);
    auto faults = std::make_shared<test::OneShotThrowingWalStorageFaultPort>();
    auto manager = WalManager::open(root->join("wal"), manager_options(true, 4, faults));
    ASSERT_TRUE(manager);
    faults->arm(WalStorageFaultPoint::kWrite, test::WalStorageFaultException::kStandard);
    const auto inserted = manager->insert(record(1, kWalPageSize));
    ASSERT_TRUE(inserted);

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!faults->triggered() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    ASSERT_TRUE(faults->triggered());
    EXPECT_EQ(manager->written_lsn(), kInitialWalRingLsn);
    ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
    EXPECT_EQ(manager->flushed_lsn(), inserted->end_lsn);
}

TEST(WalManagerTest, BackpressureWritesAnOversizedRecordBeforeRetrying) {
    auto root = test_support::TestDirectory::create("wal-manager-backpressure");
    ASSERT_TRUE(root);
    auto manager = WalManager::open(root->join("wal"), manager_options(true, 1));
    ASSERT_TRUE(manager);
    const auto first = manager->insert(record(1, kWalPageSize));
    ASSERT_TRUE(first);
    const auto second = manager->insert(record(2));
    ASSERT_TRUE(second);
    EXPECT_GE(manager->written_lsn().value(), first->end_lsn.value());
    EXPECT_EQ(manager->flushed_lsn(), kInitialWalRingLsn);
    EXPECT_GE(second->start_lsn.value(), first->end_lsn.value());
}

TEST(WalManagerTest, BackgroundWriterMakesOversizedRecordProgressWithoutForegroundFlush) {
    auto root = test_support::TestDirectory::create("wal-manager-background-oversized");
    ASSERT_TRUE(root);
    auto manager = WalManager::open(root->join("wal"), manager_options(true, 1));
    ASSERT_TRUE(manager);
    const auto oversized = manager->insert(record(1, kWalPageSize + 8U));
    ASSERT_TRUE(oversized);

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (manager->written_lsn().value() < oversized->end_lsn.value()
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    EXPECT_EQ(manager->written_lsn(), oversized->end_lsn);
    EXPECT_EQ(manager->flushed_lsn(), kInitialWalRingLsn);

    const auto following = manager->insert(record(2));
    ASSERT_TRUE(following);
    EXPECT_GE(following->start_lsn.value(), oversized->end_lsn.value());
    ASSERT_TRUE(manager->flush_through(following->end_lsn));
}

TEST(WalManagerTest, ConcurrentInsertionsReserveNonOverlappingRanges) {
    auto root = test_support::TestDirectory::create("wal-manager-concurrent");
    ASSERT_TRUE(root);
    auto manager = WalManager::open(root->join("wal"), manager_options(true, 2));
    ASSERT_TRUE(manager);
    constexpr std::size_t thread_count = 8;
    constexpr std::size_t records_per_thread = 40;
    std::mutex results_mutex;
    std::vector<WalInsertResult> results;
    results.reserve(thread_count * records_per_thread);
    std::array<std::atomic<bool>, thread_count> succeeded{};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([&manager, &results_mutex, &results, &succeeded, thread_index] {
            bool local_success = true;
            for (std::size_t record_index = 0; record_index < records_per_thread; ++record_index) {
                const auto xid = static_cast<std::uint32_t>(
                    thread_index * records_per_thread + record_index + 1U
                );
                auto inserted = manager->insert(record(xid));
                if (!inserted) {
                    local_success = false;
                    break;
                }
                const std::scoped_lock results_lock{results_mutex};
                results.push_back(inserted.value());
            }
            succeeded.at(thread_index).store(local_success, std::memory_order_relaxed);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (const auto& success : succeeded) {
        EXPECT_TRUE(success.load(std::memory_order_relaxed));
    }
    ASSERT_EQ(results.size(), thread_count * records_per_thread);
    std::sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
        return left.start_lsn.value() < right.start_lsn.value();
    });
    for (std::size_t index = 1; index < results.size(); ++index) {
        EXPECT_GE(results.at(index).start_lsn.value(), results.at(index - 1U).end_lsn.value());
    }
    ASSERT_TRUE(manager->flush_through(results.back().end_lsn));
}

TEST(WalManagerTest, ShutdownFlushesRejectsNewInsertionsAndIsIdempotent) {
    auto root = test_support::TestDirectory::create("wal-manager-shutdown");
    ASSERT_TRUE(root);
    auto manager = WalManager::open(root->join("wal"), manager_options(true));
    ASSERT_TRUE(manager);
    const auto inserted = manager->insert(record(1));
    ASSERT_TRUE(inserted);
    ASSERT_TRUE(manager->shutdown());
    EXPECT_TRUE(manager->is_shutdown());
    EXPECT_EQ(manager->flushed_lsn(), inserted->end_lsn);
    EXPECT_FALSE(manager->insert(record(2)));
    EXPECT_TRUE(manager->shutdown());
    EXPECT_TRUE(manager->flush_through(inserted->end_lsn));
}

TEST(WalManagerTest, DestructorDurablyFlushesBufferedRecords) {
    auto root = test_support::TestDirectory::create("wal-manager-destructor");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    base::Lsn end{base::kInvalidLsn};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        const auto inserted = manager->insert(record(1));
        ASSERT_TRUE(inserted);
        end = inserted->end_lsn;
    }
    auto storage = WalSegmentStorage::open(
        directory,
        WalSegmentStorageOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
            .create = false,
        }
    );
    ASSERT_TRUE(storage);
    EXPECT_EQ(storage->logical_size(), end.value());
}

TEST(WalManagerTest, CheckpointHorizonControlsAutomaticAndForcedPageImages) {
    auto root = test_support::TestDirectory::create("wal-manager-checkpoint-images");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    auto manager = WalManager::open(directory, manager_options(true));
    ASSERT_TRUE(manager);

    const auto image = page_image();
    auto automatic = std::array{WalPageImageRegistration{
        .image = &image,
        .page_lsn = kInitialWalRingLsn,
        .hole = kPageImageHole,
        .policy = WalPageImagePolicy::kAutomatic,
        .keep_delta = false,
    }};
    const auto before = manager->insert_with_page_images(page_record(1), automatic);
    ASSERT_TRUE(before);

    const auto redo_marker = manager->begin_checkpoint();
    ASSERT_TRUE(redo_marker);
    EXPECT_EQ(manager->redo_lsn(), redo_marker->start_lsn);
    const auto after = manager->insert_with_page_images(page_record(2), automatic);
    ASSERT_TRUE(after);

    automatic.front().page_lsn = base::Lsn{manager->end_lsn().value() + 1024U};
    const auto future = manager->insert_with_page_images(page_record(3), automatic);
    ASSERT_TRUE(future);
    const std::array forced{WalPageImageRegistration{
        .image = &image,
        .page_lsn = automatic.front().page_lsn,
        .hole = kPageImageHole,
        .policy = WalPageImagePolicy::kForce,
        .keep_delta = true,
    }};
    const auto forced_result = manager->insert_with_page_images(page_record(4), forced);
    ASSERT_TRUE(forced_result);
    const std::array missing_force{WalPageImageRegistration{
        .image = nullptr,
        .policy = WalPageImagePolicy::kForce,
        .keep_delta = false,
    }};
    EXPECT_FALSE(manager->insert_with_page_images(page_record(5), missing_force));
    ASSERT_TRUE(manager->flush_through(forced_result->end_lsn));
    ASSERT_TRUE(manager->shutdown());

    auto reader = WalReader::open(
        directory,
        WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader);
    std::vector<DecodedWalRecord> records;
    for (;;) {
        auto decoded = reader->read_next();
        ASSERT_TRUE(decoded);
        if (!decoded->has_value()) {
            break;
        }
        records.push_back(std::move(decoded->value()));
    }
    ASSERT_EQ(records.size(), 5U);
    EXPECT_FALSE(records[0].record.blocks.front().full_page_image.has_value());
    EXPECT_FALSE(records[0].record.blocks.front().delta.empty());
    EXPECT_EQ(records[1].record.resource_manager, WalResourceManager::kSystem);
    EXPECT_EQ(records[1].record.info, opcode::system::kCheckpointRedo);
    EXPECT_TRUE(records[2].record.blocks.front().full_page_image.has_value());
    EXPECT_TRUE(records[2].record.blocks.front().delta.empty());
    EXPECT_FALSE(records[3].record.blocks.front().full_page_image.has_value());
    EXPECT_FALSE(records[3].record.blocks.front().delta.empty());
    EXPECT_TRUE(records[4].record.blocks.front().full_page_image.has_value());
    EXPECT_FALSE(records[4].record.blocks.front().delta.empty());
}

TEST(WalManagerTest, PageImagePolicyHonorsWillInitializeSuppressAndRegistrationShape) {
    auto root = test_support::TestDirectory::create("wal-manager-page-image-policy");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    auto manager = WalManager::open(directory, manager_options(true));
    ASSERT_TRUE(manager);

    const auto image = page_image();
    const auto marker = manager->begin_checkpoint();
    ASSERT_TRUE(marker);

    auto initialized = page_record(1);
    initialized.blocks.front().will_initialize = true;
    const std::array automatic{WalPageImageRegistration{
        .image = &image,
        .page_lsn = kInitialWalRingLsn,
        .hole = kPageImageHole,
        .policy = WalPageImagePolicy::kAutomatic,
        .keep_delta = false,
    }};
    const auto automatic_result = manager->insert_with_page_images(initialized, automatic);
    ASSERT_TRUE(automatic_result);

    initialized.xid = base::Xid{2};
    const std::array forced{WalPageImageRegistration{
        .image = &image,
        .page_lsn = kInitialWalRingLsn,
        .hole = kPageImageHole,
        .policy = WalPageImagePolicy::kForce,
        .keep_delta = false,
    }};
    const auto forced_result = manager->insert_with_page_images(initialized, forced);
    ASSERT_TRUE(forced_result);

    const std::array suppressed{WalPageImageRegistration{
        .image = &image,
        .page_lsn = kInitialWalRingLsn,
        .hole = kPageImageHole,
        .policy = WalPageImagePolicy::kSuppress,
        .keep_delta = false,
    }};
    const auto suppressed_result = manager->insert_with_page_images(page_record(3), suppressed);
    ASSERT_TRUE(suppressed_result);
    const auto end_before_bad_shape = manager->end_lsn();
    EXPECT_FALSE(manager->insert_with_page_images(page_record(4), {}));
    EXPECT_EQ(manager->end_lsn(), end_before_bad_shape);
    ASSERT_TRUE(manager->flush_through(suppressed_result->end_lsn));
    ASSERT_TRUE(manager->shutdown());

    auto reader = WalReader::open(
        directory,
        WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader);
    const auto decoded_marker = reader->read_next();
    ASSERT_TRUE(decoded_marker);
    ASSERT_TRUE(decoded_marker->has_value());
    EXPECT_EQ(decoded_marker->value().record.info, opcode::system::kCheckpointRedo);

    const auto decoded_automatic = reader->read_next();
    ASSERT_TRUE(decoded_automatic);
    ASSERT_TRUE(decoded_automatic->has_value());
    EXPECT_FALSE(decoded_automatic->value().record.blocks.front().full_page_image.has_value());
    EXPECT_FALSE(decoded_automatic->value().record.blocks.front().delta.empty());

    const auto decoded_forced = reader->read_next();
    ASSERT_TRUE(decoded_forced);
    ASSERT_TRUE(decoded_forced->has_value());
    EXPECT_TRUE(decoded_forced->value().record.blocks.front().full_page_image.has_value());
    EXPECT_TRUE(decoded_forced->value().record.blocks.front().delta.empty());

    const auto decoded_suppressed = reader->read_next();
    ASSERT_TRUE(decoded_suppressed);
    ASSERT_TRUE(decoded_suppressed->has_value());
    EXPECT_FALSE(decoded_suppressed->value().record.blocks.front().full_page_image.has_value());
    EXPECT_FALSE(decoded_suppressed->value().record.blocks.front().delta.empty());
    const auto end = reader->read_next();
    ASSERT_TRUE(end);
    EXPECT_FALSE(end->has_value());
}

TEST(WalManagerTest, CheckpointMarkerSerializesConcurrentPageImageDecisions) {
    auto root = test_support::TestDirectory::create("wal-manager-checkpoint-concurrency");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    auto manager = WalManager::open(directory, manager_options(true, 8));
    ASSERT_TRUE(manager);
    const auto image = page_image();
    const std::array automatic{WalPageImageRegistration{
        .image = &image,
        .page_lsn = kInitialWalRingLsn,
        .hole = kPageImageHole,
        .policy = WalPageImagePolicy::kAutomatic,
        .keep_delta = false,
    }};
    ASSERT_TRUE(manager->insert_with_page_images(page_record(1), automatic));

    constexpr std::size_t k_thread_count = 4;
    constexpr std::size_t k_records_per_thread = 20;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(k_thread_count);
    for (std::size_t thread = 0; thread < k_thread_count; ++thread) {
        threads.emplace_back([&, thread] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::size_t index = 0; index < k_records_per_thread; ++index) {
                const auto xid =
                    static_cast<std::uint32_t>(10U + thread * k_records_per_thread + index);
                if (!manager->insert_with_page_images(page_record(xid), automatic)) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    start.store(true, std::memory_order_release);
    const auto marker = manager->begin_checkpoint();
    for (auto& thread : threads) {
        thread.join();
    }
    ASSERT_TRUE(marker);
    ASSERT_EQ(failures.load(std::memory_order_relaxed), 0U);
    const auto final = manager->insert_with_page_images(page_record(1000), automatic);
    ASSERT_TRUE(final);
    ASSERT_TRUE(manager->flush_through(final->end_lsn));
    ASSERT_TRUE(manager->shutdown());

    auto reader = WalReader::open(
        directory,
        WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader);
    bool saw_before = false;
    bool saw_after = false;
    for (;;) {
        auto decoded = reader->read_next();
        ASSERT_TRUE(decoded);
        if (!decoded->has_value()) {
            break;
        }
        if (decoded->value().record.resource_manager != WalResourceManager::kHeap) {
            continue;
        }
        const bool has_image = decoded->value().record.blocks.front().full_page_image.has_value();
        if (decoded->value().start_lsn.value() < marker->start_lsn.value()) {
            saw_before = true;
            EXPECT_FALSE(has_image);
        } else {
            saw_after = true;
            EXPECT_TRUE(has_image);
        }
    }
    EXPECT_TRUE(saw_before);
    EXPECT_TRUE(saw_after);
}

TEST(WalManagerTest, ConcurrentCheckpointsAndInsertionsPreserveEveryRecord) {
    auto root = test_support::TestDirectory::create("wal-manager-checkpoint-record-stress");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    auto manager = WalManager::open(directory, manager_options(true, 8));
    ASSERT_TRUE(manager);

    constexpr std::size_t k_thread_count = 4;
    constexpr std::size_t k_records_per_thread = 100;
    constexpr std::size_t k_checkpoint_count = 20;
    constexpr std::size_t k_record_count = k_thread_count * k_records_per_thread;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(k_thread_count + 1U);
    for (std::size_t thread = 0; thread < k_thread_count; ++thread) {
        threads.emplace_back([&, thread] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::size_t index = 0; index < k_records_per_thread; ++index) {
                const auto xid =
                    static_cast<std::uint32_t>(thread * k_records_per_thread + index + 1U);
                if (!manager->insert(record(xid))) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    threads.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t index = 0; index < k_checkpoint_count; ++index) {
            if (!manager->begin_checkpoint()) {
                failures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    });
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }
    ASSERT_EQ(failures.load(std::memory_order_relaxed), 0U);
    ASSERT_TRUE(manager->flush_through(manager->end_lsn()));
    ASSERT_TRUE(manager->shutdown());

    auto reader = WalReader::open(
        directory,
        WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader);
    std::array<bool, k_record_count + 1U> seen{};
    std::size_t ordinary_count = 0;
    std::size_t checkpoint_count = 0;
    for (;;) {
        auto decoded = reader->read_next();
        ASSERT_TRUE(decoded);
        if (!decoded->has_value()) {
            break;
        }
        const auto& wal_record = decoded->value().record;
        if (wal_record.resource_manager == WalResourceManager::kSystem) {
            EXPECT_EQ(wal_record.info, opcode::system::kCheckpointRedo);
            ++checkpoint_count;
            continue;
        }
        ASSERT_EQ(wal_record.resource_manager, WalResourceManager::kTransaction);
        const auto xid = static_cast<std::size_t>(wal_record.xid.value());
        ASSERT_GE(xid, 1U);
        ASSERT_LE(xid, k_record_count);
        EXPECT_FALSE(seen.at(xid));
        seen.at(xid) = true;
        ++ordinary_count;
    }
    EXPECT_EQ(ordinary_count, k_record_count);
    EXPECT_EQ(checkpoint_count, k_checkpoint_count);
    EXPECT_TRUE(std::ranges::all_of(seen | std::views::drop(1U), [](const bool value) {
        return value;
    }));
}

TEST(WalManagerTest, CheckpointPublishesControlBeforeRemovingOldSegments) {
    auto root = test_support::TestDirectory::create("wal-manager-checkpoint-publication");
    ASSERT_TRUE(root);
    const auto wal_directory = root->join("wal");
    auto control = WalControlStore::create(root->join("control"), initial_control());
    ASSERT_TRUE(control);
    auto manager = WalManager::open(wal_directory, manager_options(true, 8));
    ASSERT_TRUE(manager);

    const auto large = manager->insert(record(1, static_cast<std::size_t>(kWalSegmentSize)));
    ASSERT_TRUE(large);
    const auto redo_marker = manager->begin_checkpoint();
    ASSERT_TRUE(redo_marker);
    ASSERT_GT(redo_marker->start_lsn.value(), kWalSegmentSize);
    const WalRecord checkpoint_record{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::kInvalidXid,
        .resource_manager = WalResourceManager::kSystem,
        .info = opcode::system::kCheckpoint,
        .blocks = {},
        .main_payload = {},
    };
    const auto checkpoint = manager->insert(checkpoint_record);
    ASSERT_TRUE(checkpoint);

    auto next = initial_control();
    next.generation = 3;
    next.checkpoint_lsn = checkpoint->start_lsn;
    next.redo_lsn = redo_marker->start_lsn;
    const auto rejected = manager->complete_checkpoint(control.value(), next, checkpoint->end_lsn);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(control->current().generation, kInitialWalControlGeneration);
    EXPECT_EQ(manager->retention_lsn(), base::kInvalidLsn);
    EXPECT_GE(manager->flushed_lsn().value(), checkpoint->end_lsn.value());
    EXPECT_TRUE(std::filesystem::exists(wal_directory / "0000000000000000.wal"));

    next.generation = 2;
    ASSERT_TRUE(manager->complete_checkpoint(control.value(), next, checkpoint->end_lsn));
    EXPECT_EQ(control->current().generation, 2U);
    EXPECT_EQ(control->current().checkpoint_lsn, checkpoint->start_lsn);
    EXPECT_EQ(control->current().redo_lsn, redo_marker->start_lsn);
    EXPECT_EQ(manager->retention_lsn(), redo_marker->start_lsn);
    EXPECT_FALSE(std::filesystem::exists(wal_directory / "0000000000000000.wal"));
    EXPECT_FALSE(manager->recycle_before(base::Lsn{manager->redo_lsn().value() + 1U}));
    ASSERT_TRUE(manager->shutdown());

    auto reopen_options = manager_options(false, 8);
    reopen_options.retention_lsn = redo_marker->start_lsn;
    reopen_options.redo_lsn = redo_marker->start_lsn;
    auto reopened = WalManager::open(wal_directory, reopen_options);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->redo_lsn(), redo_marker->start_lsn);
    EXPECT_EQ(reopened->startup_validation().redo_lsn(), redo_marker->start_lsn);
    EXPECT_EQ(reopened->retention_lsn(), redo_marker->start_lsn);
    EXPECT_EQ(reopened->end_lsn(), checkpoint->end_lsn);
}

TEST(WalManagerTest, InjectedRecycleFailureLeavesPublishedCheckpointRetryable) {
    auto root = test_support::TestDirectory::create("wal-manager-failed-checkpoint-recycle");
    ASSERT_TRUE(root);
    const auto wal_directory = root->join("wal");
    auto control = WalControlStore::create(root->join("control"), initial_control());
    ASSERT_TRUE(control);
    auto faults = std::make_shared<test::OneShotWalStorageFaultPort>();
    auto manager = WalManager::open(wal_directory, manager_options(true, 8, faults));
    ASSERT_TRUE(manager);

    const auto large = manager->insert(record(1, static_cast<std::size_t>(kWalSegmentSize)));
    ASSERT_TRUE(large);
    const auto redo_marker = manager->begin_checkpoint();
    ASSERT_TRUE(redo_marker);
    ASSERT_GT(redo_marker->start_lsn.value(), kWalSegmentSize);
    const WalRecord checkpoint_record{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::kInvalidXid,
        .resource_manager = WalResourceManager::kSystem,
        .info = opcode::system::kCheckpoint,
        .blocks = {},
        .main_payload = {},
    };
    const auto checkpoint = manager->insert(checkpoint_record);
    ASSERT_TRUE(checkpoint);
    auto next = initial_control();
    next.generation = 2;
    next.checkpoint_lsn = checkpoint->start_lsn;
    next.redo_lsn = redo_marker->start_lsn;

    faults->arm(WalStorageFaultPoint::kRecycle);
    const auto failed = manager->complete_checkpoint(control.value(), next, checkpoint->end_lsn);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(control->current().generation, 2U);
    EXPECT_EQ(control->current().redo_lsn, redo_marker->start_lsn);
    EXPECT_EQ(manager->retention_lsn(), base::kInvalidLsn);
    EXPECT_TRUE(std::filesystem::exists(wal_directory / "0000000000000000.wal"));

    ASSERT_TRUE(manager->recycle_before(redo_marker->start_lsn));
    EXPECT_EQ(manager->retention_lsn(), redo_marker->start_lsn);
    EXPECT_FALSE(std::filesystem::exists(wal_directory / "0000000000000000.wal"));
    ASSERT_TRUE(manager->shutdown());
}

TEST(WalManagerTest, InjectedTailTruncateFailureLeavesRepairRetryable) {
    auto root = test_support::TestDirectory::create("wal-manager-failed-tail-truncate");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    base::Lsn valid_end{base::kInvalidLsn};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        const auto inserted = manager->insert(record(1));
        ASSERT_TRUE(inserted);
        valid_end = inserted->end_lsn;
        ASSERT_TRUE(manager->flush_through(valid_end));
        ASSERT_TRUE(manager->shutdown());
    }

    const auto segment_path = directory / "0000000000000000.wal";
    auto file = io::PosixFile::open(
        segment_path,
        io::FileOpenOptions{.access = io::FileAccess::kReadWrite}
    );
    ASSERT_TRUE(file);
    const auto original_physical_size = file->size();
    ASSERT_TRUE(original_physical_size);
    constexpr std::array garbage{std::byte{0xA5}, std::byte{0x5A}, std::byte{0x11}};
    ASSERT_TRUE(file->write_all_at(original_physical_size.value(), garbage));
    ASSERT_TRUE(file->sync_data());
    ASSERT_TRUE(file->close());

    auto faults = std::make_shared<test::OneShotWalStorageFaultPort>();
    faults->arm(WalStorageFaultPoint::kTruncate);
    const auto failed = WalManager::open(directory, manager_options(false, 4, faults));
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(
        std::filesystem::file_size(segment_path),
        original_physical_size.value() + garbage.size()
    );

    auto repaired = WalManager::open(directory, manager_options(false, 4, faults));
    ASSERT_TRUE(repaired);
    EXPECT_EQ(repaired->end_lsn(), valid_end);
    EXPECT_EQ(std::filesystem::file_size(segment_path), original_physical_size.value());
    ASSERT_TRUE(repaired->shutdown());
}

}  // namespace
}  // namespace db::wal
