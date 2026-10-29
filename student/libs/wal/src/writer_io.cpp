#include "writer_io.hpp"

#include <mutex>
#include <utility>

#include "db/base/lab_missing.hpp"

namespace db::wal::writer_detail {

WriterIoCoordinator::Leader::Leader(
    WriterIoCoordinator& owner,
    std::unique_lock<std::mutex> io_lock
) noexcept
    : owner_{&owner},
      io_lock_{std::move(io_lock)} {}

WriterIoCoordinator::Leader::~Leader() {
    if (owner_ == nullptr || !io_lock_.owns_lock()) {
        return;
    }
    io_lock_.unlock();
    owner_->publish_completion();
}

WriterIoCoordinator::Leader::Leader(Leader&& other) noexcept
    : owner_{std::exchange(other.owner_, nullptr)},
      io_lock_{std::move(other.io_lock_)} {}

[[maybe_unused]] auto WriterIoCoordinator::lock() -> WriterIoCoordinator::Leader {
    // LAB-S10.Q02:R35-BEGIN
    base::lab::missing_algorithm("S10.Q02:R35");
    // LAB-S10.Q02:R35-END
}

[[maybe_unused]] void WriterIoCoordinator::publish_completion() {
    // LAB-S10.Q02:R36-BEGIN
    base::lab::missing_algorithm("S10.Q02:R36");
    // LAB-S10.Q02:R36-END
}

}  // namespace db::wal::writer_detail
