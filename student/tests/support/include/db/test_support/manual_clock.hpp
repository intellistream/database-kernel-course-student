#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include "db/base/checked.hpp"
#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::test_support {

class ManualClock final {
public:
    using Duration = std::chrono::nanoseconds;
    using TimePoint = std::chrono::time_point<ManualClock, Duration>;

    constexpr ManualClock() noexcept = default;

    explicit ManualClock(const TimePoint start) noexcept
        : ticks_{start.time_since_epoch().count()} {}

    auto now() const noexcept -> TimePoint {
        return TimePoint{Duration{ticks_.load(std::memory_order_acquire)}};
    }

    auto advance(const Duration delta) -> db::base::Status {
        if (delta.count() < 0) {
            return db::base::fail(
                db::base::Error{
                    db::base::ErrorCode::kInvalidArgument,
                    "manual clock cannot move backward",
                }
            );
        }

        std::int64_t current = ticks_.load(std::memory_order_acquire);
        while (true) {
            auto next = db::base::checked_add(current, delta.count());
            if (!next) {
                return db::base::fail(std::move(next).error());
            }
            if (ticks_.compare_exchange_weak(
                    current,
                    *next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )) {
                return {};
            }
        }
    }

private:
    std::atomic<std::int64_t> ticks_{0};
};

}  // namespace db::test_support
