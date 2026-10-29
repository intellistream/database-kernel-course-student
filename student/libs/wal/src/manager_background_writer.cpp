#include "manager_background_writer.hpp"

#include <string>
#include <system_error>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::wal::manager_detail {
namespace {

[[maybe_unused]] auto unavailable(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, std::move(message)}.with_context(
        "wal.manager",
        "start background writer"
    );
}

}  // namespace

ManagerBackgroundWriter::ManagerBackgroundWriter(Work work)
    : work_{std::move(work)} {}

ManagerBackgroundWriter::~ManagerBackgroundWriter() {
    stop();
}

[[maybe_unused]] auto ManagerBackgroundWriter::start() -> base::Status {
    // LAB-S10.Q04:R11-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R11"});
    // LAB-S10.Q04:R11-END
}

[[maybe_unused]] auto ManagerBackgroundWriter::request([[maybe_unused]] const std::uint64_t target)
    -> bool {
    // LAB-S10.Q04:R12-BEGIN
    base::lab::missing_algorithm("S10.Q04:R12");
    // LAB-S10.Q04:R12-END
}

void ManagerBackgroundWriter::stop() {
    const std::scoped_lock stop_lock{stop_mutex_};
    {
        const std::scoped_lock lock{mutex_};
        stop_ = true;
    }
    wakeup_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
    // The manager callback retains the shared Inner while the thread is live.
    // Release it after join so the Inner -> worker -> callback cycle cannot
    // outlive an explicit shutdown.
    work_ = nullptr;
}

[[maybe_unused]] void ManagerBackgroundWriter::run() noexcept {
    // LAB-S10.Q04:R13-BEGIN

    base::lab::missing_algorithm("S10.Q04:R13");
    // LAB-S10.Q04:R13-END
}

}  // namespace db::wal::manager_detail
