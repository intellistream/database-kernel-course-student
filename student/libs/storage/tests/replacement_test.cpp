#include <cstddef>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/storage/frame.hpp"
#include "db/storage/page.hpp"
#include "db/storage/replacement.hpp"

namespace db::storage {
namespace {

struct FrameSet final {
    explicit FrameSet(const std::size_t size) {
        owned.reserve(size);
        frames.reserve(size);
        for (std::size_t index = 0; index < size; ++index) {
            owned.push_back(
                std::make_unique<Frame>(base::FrameId{
                    static_cast<std::uint32_t>(index),
                })
            );
            frames.push_back(owned.back().get());
            const PageAddress address{
                base::RelationId{1},
                base::PageId{static_cast<std::uint32_t>(index)},
            };
            auto page = Page::create(PageKind::kHeap, address);
            EXPECT_TRUE(page);
            auto pin = owned.back()->install(address, std::move(page).value());
            EXPECT_TRUE(pin);
        }
    }

    std::vector<std::unique_ptr<Frame>> owned;
    std::vector<Frame*> frames;
};

TEST(LruReplacerTest, EvictsLeastRecentlyPinnedFrame) {
    FrameSet set{3};
    auto replacer = LruReplacer::create(3);
    ASSERT_TRUE(replacer);
    replacer.value()->on_pin(base::FrameId{0});
    replacer.value()->on_pin(base::FrameId{1});
    replacer.value()->on_pin(base::FrameId{2});
    auto victim = replacer.value()->evict(set.frames);
    ASSERT_TRUE(victim);
    EXPECT_EQ(victim.value().frame_id(), base::FrameId{0});
}

TEST(LruReplacerTest, ReaccessMovesFrameToMostRecentPosition) {
    FrameSet set{3};
    auto replacer = LruReplacer::create(3);
    ASSERT_TRUE(replacer);
    for (std::uint32_t index = 0; index < 3; ++index) {
        replacer.value()->on_pin(base::FrameId{index});
    }
    replacer.value()->on_pin(base::FrameId{0});
    auto victim = replacer.value()->evict(set.frames);
    ASSERT_TRUE(victim);
    EXPECT_EQ(victim.value().frame_id(), base::FrameId{1});
}

TEST(LruReplacerTest, SkipsPinnedFramesAndReportsExhaustion) {
    FrameSet set{2};
    auto replacer = LruReplacer::create(2);
    ASSERT_TRUE(replacer);
    replacer.value()->on_pin(base::FrameId{0});
    replacer.value()->on_pin(base::FrameId{1});
    auto first_pin = set.owned[0]->try_pin(PageAddress{base::RelationId{1}, base::PageId{0}});
    auto second_pin = set.owned[1]->try_pin(PageAddress{base::RelationId{1}, base::PageId{1}});
    ASSERT_TRUE(first_pin);
    ASSERT_TRUE(second_pin);
    const auto victim = replacer.value()->evict(set.frames);
    ASSERT_FALSE(victim);
    EXPECT_EQ(victim.error().code(), base::ErrorCode::kUnavailable);
}

TEST(LruReplacerTest, ShardsTrackFramesIndependently) {
    FrameSet set{4};
    auto replacer = LruReplacer::create(4, 2);
    ASSERT_TRUE(replacer);
    EXPECT_EQ(replacer.value()->bucket_count(), 2U);
    for (std::uint32_t index = 0; index < 4; ++index) {
        replacer.value()->on_pin(base::FrameId{index});
    }
    auto first = replacer.value()->evict(set.frames);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.value().frame_id(), base::FrameId{0});
    auto second = replacer.value()->evict(set.frames);
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value().frame_id(), base::FrameId{1});
}

TEST(LruReplacerTest, RestoresAClaimedVictimAfterReplacementFailure) {
    FrameSet set{1};
    auto replacer = LruReplacer::create(1);
    ASSERT_TRUE(replacer);
    replacer.value()->on_pin(base::FrameId{0});
    {
        auto victim = replacer.value()->evict(set.frames);
        ASSERT_TRUE(victim);
        replacer.value()->on_restore(victim.value().frame_id());
    }
    auto retry = replacer.value()->evict(set.frames);
    ASSERT_TRUE(retry);
    EXPECT_EQ(retry.value().frame_id(), base::FrameId{0});
}

TEST(ClockReplacerTest, GivesReferencedFramesASecondChance) {
    FrameSet set{3};
    auto replacer = ClockReplacer::create(3);
    ASSERT_TRUE(replacer);
    replacer.value()->on_pin(base::FrameId{0});
    replacer.value()->on_pin(base::FrameId{1});
    EXPECT_EQ(replacer.value()->usage(base::FrameId{0}).value(), 1U);
    auto victim = replacer.value()->evict(set.frames);
    ASSERT_TRUE(victim);
    EXPECT_EQ(victim.value().frame_id(), base::FrameId{2});
    EXPECT_EQ(replacer.value()->usage(base::FrameId{0}).value(), 0U);
    EXPECT_EQ(replacer.value()->usage(base::FrameId{1}).value(), 0U);
}

TEST(ClockReplacerTest, SkipsPinnedFramesAndCapsConcurrentUsage) {
    FrameSet set{2};
    auto replacer = ClockReplacer::create(2);
    ASSERT_TRUE(replacer);
    std::vector<std::thread> threads;
    threads.reserve(8U);
    for (std::size_t thread = 0; thread < 8; ++thread) {
        threads.emplace_back([&] {
            for (std::size_t hit = 0; hit < 100; ++hit) {
                replacer.value()->on_pin(base::FrameId{0});
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(replacer.value()->usage(base::FrameId{0}).value(), ClockReplacer::kMaximumUsage);
    auto pinned = set.owned[0]->try_pin(PageAddress{base::RelationId{1}, base::PageId{0}});
    ASSERT_TRUE(pinned);
    auto victim = replacer.value()->evict(set.frames);
    ASSERT_TRUE(victim);
    EXPECT_EQ(victim.value().frame_id(), base::FrameId{1});
}

TEST(ReplacementPolicyTest, RejectsInvalidConfigurationAndReportsUsageDomain) {
    EXPECT_FALSE(LruReplacer::create(0));
    EXPECT_FALSE(LruReplacer::create(2, 0));
    EXPECT_FALSE(LruReplacer::create(2, 3));
    EXPECT_FALSE(ClockReplacer::create(0));
    auto lru = LruReplacer::create(2);
    auto clock = ClockReplacer::create(2);
    ASSERT_TRUE(lru);
    ASSERT_TRUE(clock);
    EXPECT_FALSE(clock.value()->usage(base::FrameId{2}));
}

}  // namespace
}  // namespace db::storage
