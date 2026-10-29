#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {
namespace {

TEST(XidTest, ComparesNormalIdsWithExplicitWraparoundSemantics) {
    auto ascending = compare_xids(base::Xid{41}, base::Xid{42});
    ASSERT_TRUE(ascending);
    EXPECT_EQ(*ascending, XidOrder::kBefore);
    auto descending = compare_xids(base::Xid{42}, base::Xid{41});
    ASSERT_TRUE(descending);
    EXPECT_EQ(*descending, XidOrder::kAfter);
    auto equal = compare_xids(base::Xid{42}, base::Xid{42});
    ASSERT_TRUE(equal);
    EXPECT_EQ(*equal, XidOrder::kEqual);

    auto across_wrap = compare_xids(base::Xid{UINT32_MAX - 1U}, base::Xid{1});
    ASSERT_TRUE(across_wrap);
    EXPECT_EQ(*across_wrap, XidOrder::kBefore);
}

TEST(XidTest, RejectsSentinelsAndTheAmbiguousHalfRange) {
    auto invalid = compare_xids(base::kInvalidXid, base::Xid{1});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);
    auto boundary = compare_xids(kXidWrapBoundary, base::Xid{1});
    ASSERT_FALSE(boundary);
    EXPECT_EQ(boundary.error().code(), base::ErrorCode::kInvalidArgument);
    auto ambiguous = compare_xids(base::Xid{1}, base::Xid{1U + (UINT32_C(1) << 31U)});
    ASSERT_FALSE(ambiguous);
    EXPECT_EQ(ambiguous.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(XidGeneratorTest, AllocatesMonotonicNormalIdsAndReportsHorizon) {
    XidGenerator generator;
    EXPECT_EQ(generator.next_xid(), kFirstNormalXid);
    const auto initial_remaining = generator.remaining_before_wrap();

    for (std::uint32_t expected = 1; expected <= 4; ++expected) {
        auto xid = generator.allocate();
        ASSERT_TRUE(xid);
        EXPECT_EQ(*xid, base::Xid{expected});
        EXPECT_TRUE(is_normal_xid(*xid));
    }
    EXPECT_EQ(generator.next_xid(), base::Xid{5});
    EXPECT_EQ(generator.remaining_before_wrap(), initial_remaining - 4U);
}

TEST(XidGeneratorTest, RestoresAndStopsBeforeSilentWraparound) {
    auto invalid = XidGenerator::from_next_xid(base::kInvalidXid);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);

    auto restored = XidGenerator::from_next_xid(base::Xid{UINT32_MAX - 1U});
    ASSERT_TRUE(restored);
    auto last = (*restored)->allocate();
    ASSERT_TRUE(last);
    EXPECT_EQ(*last, base::Xid{UINT32_MAX - 1U});
    EXPECT_EQ((*restored)->next_xid(), kXidWrapBoundary);
    EXPECT_EQ((*restored)->remaining_before_wrap(), 0U);

    auto exhausted = (*restored)->allocate();
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ((*restored)->next_xid(), kXidWrapBoundary);

    auto reopened = XidGenerator::from_next_xid(kXidWrapBoundary);
    ASSERT_TRUE(reopened);
    auto still_exhausted = (*reopened)->allocate();
    ASSERT_FALSE(still_exhausted);
    EXPECT_EQ(still_exhausted.error().code(), base::ErrorCode::kResourceExhausted);
}

TEST(XidGeneratorTest, ConcurrentCallersReceiveEveryIdExactlyOnce) {
    constexpr std::size_t k_threads = 8;
    constexpr std::size_t k_per_thread = 250;
    XidGenerator generator;
    std::mutex results_mutex;
    std::vector<std::uint32_t> results;
    results.reserve(k_threads * k_per_thread);

    std::vector<std::thread> threads;
    threads.reserve(k_threads);
    for (std::size_t thread_index = 0; thread_index < k_threads; ++thread_index) {
        threads.emplace_back([&] {
            std::vector<std::uint32_t> local;
            local.reserve(k_per_thread);
            for (std::size_t allocation = 0; allocation < k_per_thread; ++allocation) {
                auto xid = generator.allocate();
                ASSERT_TRUE(xid);
                local.push_back(xid->value());
            }
            const std::lock_guard lock{results_mutex};
            results.insert(results.end(), local.begin(), local.end());
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    std::ranges::sort(results);
    ASSERT_EQ(results.size(), k_threads * k_per_thread);
    for (std::size_t index = 0; index < results.size(); ++index) {
        EXPECT_EQ(results[index], index + 1U);
    }
    EXPECT_EQ(generator.next_xid(), base::Xid{static_cast<std::uint32_t>(results.size() + 1U)});
}

}  // namespace
}  // namespace db::transaction
