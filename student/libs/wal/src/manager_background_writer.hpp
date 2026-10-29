#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include "db/base/result.hpp"

namespace db::wal::manager_detail {

// Owns only the background thread's request/stop protocol. The callback owns the
// WAL-specific best-effort write policy and must not make a durability claim.
class ManagerBackgroundWriter final {
public:
    using Work = std::function<void()>;

    explicit ManagerBackgroundWriter(Work work);
    ~ManagerBackgroundWriter();

    ManagerBackgroundWriter(const ManagerBackgroundWriter&) = delete;
    auto operator=(const ManagerBackgroundWriter&) -> ManagerBackgroundWriter& = delete;
    ManagerBackgroundWriter(ManagerBackgroundWriter&&) = delete;
    auto operator=(ManagerBackgroundWriter&&) -> ManagerBackgroundWriter& = delete;

    auto start() -> base::Status;
    // Returns true only when target advances the monotonic requested-write
    // frontier. Duplicate record-level requests avoid the wake mutex entirely.
    auto request(std::uint64_t target) -> bool;
    void stop();

private:
    void run() noexcept;

    Work work_;
    std::atomic<std::uint64_t> requested_target_{0};
    std::mutex stop_mutex_;
    std::mutex mutex_;
    std::condition_variable wakeup_;
    bool wake_{false};
    bool stop_{false};
    std::thread thread_;
};

}  // namespace db::wal::manager_detail
