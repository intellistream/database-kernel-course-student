#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace db::tpcc {

// A fixed SplitMix64 stream keeps local benchmark inputs reproducible across
// supported libstdc++ versions. The seed is intentionally public and is not the
// grader's submission-scoped hidden stream.
class DeterministicRandom final {
public:
    explicit DeterministicRandom(std::uint64_t seed) noexcept
        : state_{seed} {}

    auto next_u64() noexcept -> std::uint64_t {
        state_ += 0x9e37'79b9'7f4a'7c15ULL;
        std::uint64_t value = state_;
        value = (value ^ (value >> 30U)) * 0xbf58'476d'1ce4'e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d0'49bb'1331'11ebULL;
        return value ^ (value >> 31U);
    }

    auto uniform(std::uint32_t minimum, std::uint32_t maximum) noexcept -> std::uint32_t {
        const std::uint64_t range = std::uint64_t{maximum} - minimum + 1U;
        const std::uint64_t threshold = (std::uint64_t{0} - range) % range;
        std::uint64_t value = next_u64();
        while (value < threshold) {
            value = next_u64();
        }
        return minimum + static_cast<std::uint32_t>(value % range);
    }

    template <typename Value>
    void shuffle(const std::span<Value> values) noexcept {
        for (std::size_t index = values.size(); index > 1U; --index) {
            const std::size_t selected = uniform(0U, static_cast<std::uint32_t>(index - 1U));
            std::swap(values[index - 1U], values[selected]);
        }
    }

private:
    std::uint64_t state_;
};

}  // namespace db::tpcc
