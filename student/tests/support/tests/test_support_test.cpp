#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/test_support/deterministic_random.hpp"
#include "db/test_support/fault_injector.hpp"
#include "db/test_support/manual_clock.hpp"
#include "db/test_support/test_directory.hpp"

namespace db::test_support {
namespace {

TEST(DeterministicRandomTest, PreservesStableSequenceForSeed) {
    DeterministicRandom random{0};

    EXPECT_EQ(random.next_u64(), 0xE220'A839'7B1D'CDAFULL);
    EXPECT_EQ(random.next_u64(), 0x6E78'9E6A'A1B9'65F4ULL);
    EXPECT_EQ(random.next_u64(), 0x06C4'5D18'8009'454FULL);
}

TEST(DeterministicRandomTest, BoundsFillsAndShufflesDeterministically) {
    DeterministicRandom first{42};
    DeterministicRandom second{42};
    std::array<std::byte, 19> first_bytes{};
    std::array<std::byte, 19> second_bytes{};
    std::array first_values{1, 2, 3, 4, 5, 6};
    std::array second_values{1, 2, 3, 4, 5, 6};

    first.fill(first_bytes);
    second.fill(second_bytes);
    first.shuffle<int>(first_values);
    second.shuffle<int>(second_values);

    EXPECT_EQ(first_bytes, second_bytes);
    EXPECT_EQ(first_values, second_values);
    EXPECT_LT(*first.uniform(17), 17U);
    const auto invalid = first.uniform(0);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), db::base::ErrorCode::kInvalidArgument);
}

TEST(ManualClockTest, AdvancesMonotonicallyAndRejectsInvalidMovement) {
    using namespace std::chrono_literals;
    ManualClock clock{ManualClock::TimePoint{5ns}};

    ASSERT_TRUE(clock.advance(37ns));
    EXPECT_EQ(clock.now().time_since_epoch(), 42ns);
    const auto backward = clock.advance(-1ns);
    ASSERT_FALSE(backward);
    EXPECT_EQ(backward.error().code(), db::base::ErrorCode::kInvalidArgument);

    ManualClock near_limit{
        ManualClock::TimePoint{ManualClock::Duration{std::numeric_limits<std::int64_t>::max()}}
    };
    const auto overflow = near_limit.advance(1ns);
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code(), db::base::ErrorCode::kOutOfRange);
}

TEST(FaultInjectorTest, FailsExactlyOnScriptedInvocation) {
    FaultInjector faults;
    ASSERT_TRUE(faults.arm(
        "storage.write",
        2,
        db::base::Error{db::base::ErrorCode::kIo, "injected write failure"}
    ));

    EXPECT_TRUE(faults.check("storage.write"));
    const auto injected = faults.check("storage.write");
    ASSERT_FALSE(injected);
    EXPECT_EQ(injected.error().code(), db::base::ErrorCode::kIo);
    EXPECT_EQ(
        injected.error().to_string(),
        "fault_injection.storage.write: injected write failure"
    );
    EXPECT_TRUE(faults.check("storage.write"));
    EXPECT_EQ(faults.observed("storage.write"), 3U);
    EXPECT_TRUE(faults.check("unarmed"));

    faults.clear();
    EXPECT_EQ(faults.observed("storage.write"), 0U);
    EXPECT_FALSE(faults.arm("", 1, db::base::Error{db::base::ErrorCode::kIo, "bad"}));
}

TEST(TestSupportConcurrencyTest, ClockAndFaultPlanRemainThreadSafeUnderContention) {
    using namespace std::chrono_literals;
    constexpr std::size_t thread_count = 4;
    constexpr std::size_t iterations = 1'000;
    ManualClock clock;
    FaultInjector faults;
    ASSERT_TRUE(faults.arm(
        "concurrent",
        (thread_count * iterations) / 2,
        db::base::Error{db::base::ErrorCode::kUnavailable, "injected"}
    ));
    std::atomic<std::size_t> failures{0};
    std::atomic<std::size_t> clock_failures{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&] {
            for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
                if (!faults.check("concurrent")) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }
                if (!clock.advance(1ns)) {
                    clock_failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 1U);
    EXPECT_EQ(clock_failures.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(faults.observed("concurrent"), thread_count * iterations);
    EXPECT_EQ(clock.now().time_since_epoch(), (thread_count * iterations) * 1ns);
}

TEST(TestDirectoryTest, CreatesJoinsMovesAndCleansUp) {
    auto created = TestDirectory::create("unit/path");
    ASSERT_TRUE(created);
    TestDirectory directory = std::move(created).value();
    const std::filesystem::path original = directory.path();

    EXPECT_TRUE(std::filesystem::is_directory(original));
    EXPECT_EQ(original.filename().string().find('/'), std::string::npos);
    EXPECT_EQ(directory.join("child"), original / "child");

    TestDirectory moved = std::move(directory);
    ASSERT_TRUE(moved.cleanup());
    EXPECT_FALSE(std::filesystem::exists(original));
    EXPECT_TRUE(moved.cleanup());
}

}  // namespace
}  // namespace db::test_support
