#include <atomic>
#include <chrono>
#include <cstddef>
#include <semaphore>

#include <gtest/gtest.h>

#include "manager_background_writer.hpp"

namespace db::wal::manager_detail {
namespace {

using namespace std::chrono_literals;

TEST(ManagerBackgroundWriterTest, CoalescesDuplicateTargetsAndRetainsNewWork) {
    std::binary_semaphore entered{0};
    std::binary_semaphore resume{0};
    std::atomic<std::size_t> callbacks{0};
    ManagerBackgroundWriter writer{[&] {
        callbacks.fetch_add(1U, std::memory_order_relaxed);
        entered.release();
        resume.acquire();
    }};
    ASSERT_TRUE(writer.start());

    EXPECT_TRUE(writer.request(100U));
    if (!entered.try_acquire_for(2s)) {
        resume.release();
        writer.stop();
        FAIL() << "background writer did not process the initial request";
    }
    for (std::size_t request = 0; request < 256U; ++request) {
        EXPECT_FALSE(writer.request(100U));
    }
    EXPECT_FALSE(writer.request(99U));

    EXPECT_TRUE(writer.request(200U));
    EXPECT_FALSE(writer.request(200U));
    resume.release();
    if (!entered.try_acquire_for(2s)) {
        resume.release();
        writer.stop();
        FAIL() << "background writer lost an advancing request";
    }
    EXPECT_EQ(callbacks.load(std::memory_order_relaxed), 2U);

    resume.release();
    writer.stop();
}

}  // namespace
}  // namespace db::wal::manager_detail
