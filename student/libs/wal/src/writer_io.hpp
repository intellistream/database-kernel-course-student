#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

#include "db/base/lab_missing.hpp"

namespace db::wal::writer_detail {

// Serializes every physical I/O owner and publishes one completion generation
// after that owner releases the I/O mutex. Holding completion_mutex_ while
// trying the I/O mutex makes follower sleep free of lost wakeups.
class WriterIoCoordinator final {
public:
    class Leader final {
    public:
        ~Leader();

        Leader(const Leader&) = delete;
        auto operator=(const Leader&) -> Leader& = delete;
        Leader(Leader&& other) noexcept;
        auto operator=(Leader&&) -> Leader& = delete;

    private:
        friend class WriterIoCoordinator;

        Leader(WriterIoCoordinator& owner, std::unique_lock<std::mutex> io_lock) noexcept;

        WriterIoCoordinator* owner_;
        std::unique_lock<std::mutex> io_lock_;
    };

    WriterIoCoordinator() = default;
    ~WriterIoCoordinator() = default;

    WriterIoCoordinator(const WriterIoCoordinator&) = delete;
    auto operator=(const WriterIoCoordinator&) -> WriterIoCoordinator& = delete;
    WriterIoCoordinator(WriterIoCoordinator&&) = delete;
    auto operator=(WriterIoCoordinator&&) -> WriterIoCoordinator& = delete;

    auto lock() -> Leader;

    template <typename Ready>
    [[maybe_unused]] auto elect_or_wait([[maybe_unused]] const Ready& ready)
        -> std::optional<Leader> {
        // LAB-S10.Q02:R37-BEGIN
        base::lab::missing_algorithm("S10.Q02:R37");
        // LAB-S10.Q02:R37-END
    }

private:
    void publish_completion();

    std::mutex io_mutex_;
    std::mutex completion_mutex_;
    std::condition_variable io_completed_;
    std::uint64_t completion_generation_{0};
};

}  // namespace db::wal::writer_detail
