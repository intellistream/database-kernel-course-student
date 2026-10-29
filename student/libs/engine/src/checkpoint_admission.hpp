#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/result.hpp"

namespace db::engine::detail {

// The database owns one admission domain. A lease spans an entire explicit
// transaction, or an implicit command including its commit/abort. Acquisition
// and closing the barrier use the same mutex, so no transaction can slip
// between the drain predicate and the barrier publication.
class CheckpointAdmission final {
public:
    [[maybe_unused]] auto enter() -> base::Status {
        // LAB-S10.Q08:R1001-BEGIN
        base::lab::missing_algorithm("S10.Q08:R1001");
        // LAB-S10.Q08:R1001-END
    }

    void leave() noexcept {
        const std::lock_guard lock{mutex_};
        --active_;
        if (active_ == 0U) {
            changed_.notify_all();
        }
    }

    [[maybe_unused]] auto
    run([[maybe_unused]] const std::chrono::milliseconds timeout,
        [[maybe_unused]] const std::function<base::Status()>& action) -> base::Status {
        // LAB-S10.Q08:R1002-BEGIN
        base::lab::missing_algorithm("S10.Q08:R1002");
        // LAB-S10.Q08:R1002-END
    }

private:
    [[maybe_unused]] static auto unavailable([[maybe_unused]] const char* message) -> base::Error {
        // LAB-S10.Q08:R1003-BEGIN
        base::lab::missing_algorithm("S10.Q08:R1003");
        // LAB-S10.Q08:R1003-END
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t active_{0U};
    bool blocked_{false};
};

class AdmissionLease final {
public:
    [[maybe_unused]] explicit AdmissionLease(
        [[maybe_unused]] std::shared_ptr<CheckpointAdmission> admission
    )
        : admission_{std::move(admission)} {
        // LAB-S10.Q08:R1004-BEGIN
        base::lab::missing_algorithm("S10.Q08:R1004");
        // LAB-S10.Q08:R1004-END
    }

    ~AdmissionLease() {
        release();
    }

    AdmissionLease(const AdmissionLease&) = delete;
    auto operator=(const AdmissionLease&) -> AdmissionLease& = delete;

    [[maybe_unused]] auto acquire() -> base::Status {
        // LAB-S10.Q08:R1005-BEGIN
        base::lab::missing_algorithm("S10.Q08:R1005");
        // LAB-S10.Q08:R1005-END
    }

    void release() noexcept {
        if (std::exchange(held_, false)) {
            admission_->leave();
        }
    }

private:
    std::shared_ptr<CheckpointAdmission> admission_;
    bool held_{false};
};

}  // namespace db::engine::detail
