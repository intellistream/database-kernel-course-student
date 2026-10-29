#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::test_support {

inline constexpr std::uint64_t kDefaultTestSeed = 0x4D59'5DF4'D0F3'3173ULL;

class DeterministicRandom final {
public:
    constexpr explicit DeterministicRandom(const std::uint64_t seed = kDefaultTestSeed) noexcept
        : state_{seed} {}

    constexpr auto seed_state() const noexcept -> std::uint64_t {
        return state_;
    }

    constexpr auto next_u64() noexcept -> std::uint64_t {
        state_ += 0x9E37'79B9'7F4A'7C15ULL;
        std::uint64_t value = state_;
        value = (value ^ (value >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
        value = (value ^ (value >> 27U)) * 0x94D0'49BB'1331'11EBULL;
        return value ^ (value >> 31U);
    }

    auto uniform(const std::uint64_t upper_exclusive) -> db::base::Result<std::uint64_t> {
        if (upper_exclusive == 0) {
            return db::base::fail(
                db::base::Error{
                    db::base::ErrorCode::kInvalidArgument,
                    "random upper bound must be positive",
                }
            );
        }
        const std::uint64_t threshold = (0U - upper_exclusive) % upper_exclusive;
        std::uint64_t value = 0;
        do {
            value = next_u64();
        } while (value < threshold);
        return value % upper_exclusive;
    }

    void fill(const db::base::MutableBytes output) noexcept {
        std::uint64_t random = 0;
        for (std::size_t index = 0; index < output.size(); ++index) {
            if (index % sizeof(random) == 0) {
                random = next_u64();
            }
            output[index] = static_cast<std::byte>(random >> ((index % sizeof(random)) * 8U));
        }
    }

    template <typename Value>
    void shuffle(const std::span<Value> values) {
        for (std::size_t size = values.size(); size > 1; --size) {
            const std::size_t selected = *uniform(size);
            std::swap(values[size - 1], values[selected]);
        }
    }

private:
    std::uint64_t state_;
};

}  // namespace db::test_support
