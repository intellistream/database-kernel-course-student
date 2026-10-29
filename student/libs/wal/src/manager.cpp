#include "db/wal/manager.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/result.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/record.hpp"
#include "db/wal/ring_buffer.hpp"
#include "db/wal/writer.hpp"

#include "manager_background_writer.hpp"
#include "manager_checkpoint.hpp"
#include "manager_page_images.hpp"
#include "manager_startup.hpp"

namespace db::wal {
namespace {

enum class ManagerState : std::uint8_t {
    kRunning,
    kShuttingDown,
    kStopped,
};

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.manager",
        "validate"
    );
}

[[maybe_unused]] auto cancelled(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCancelled, std::move(message)}.with_context(
        "wal.manager",
        "lifecycle"
    );
}

[[maybe_unused]] auto inactive() -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, "WAL manager has been moved from"}
        .with_context("wal.manager", "access manager");
}

[[maybe_unused]] auto encoded_record_buffer() -> std::vector<std::byte>& {
    thread_local std::vector<std::byte> buffer;
    return buffer;
}

}  // namespace

struct WalManager::Inner final {
    Inner(
        std::filesystem::path owned_directory,
        WalWriter owned_writer,
        const base::Lsn initial_redo_lsn,
        WalStartupValidation initial_startup_validation
    )
        : directory{std::move(owned_directory)},
          writer{std::move(owned_writer)},
          redo{initial_redo_lsn.value()},
          startup_validation{initial_startup_validation} {}

    std::filesystem::path directory;
    WalWriter writer;
    std::atomic<std::uint64_t> redo;
    WalStartupValidation startup_validation;
    std::shared_mutex checkpoint_mutex;

    std::atomic<ManagerState> state{ManagerState::kRunning};
    std::mutex shutdown_mutex;

    std::unique_ptr<manager_detail::ManagerBackgroundWriter> background_writer;

    static auto ensure_running(const std::shared_ptr<Inner>& inner) -> base::Status;
    void request_background_write(const WalReservation& completed) const;
    void stop_background_writer() const;
    auto flush_through(base::Lsn target) -> base::Status;
    auto append_encoded(base::Bytes encoded) -> base::Result<WalInsertResult>;
    void force_stop();
};

[[maybe_unused]] auto
WalManager::Inner::ensure_running([[maybe_unused]] const std::shared_ptr<Inner>& inner)
    -> base::Status {
    // LAB-S10.Q04:R01-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R01"});
    // LAB-S10.Q04:R01-END
}

[[maybe_unused]] void WalManager::Inner::request_background_write(
    [[maybe_unused]] const WalReservation& completed
) const {
    // LAB-S10.Q04:R02-BEGIN
    base::lab::missing_algorithm("S10.Q04:R02");
    // LAB-S10.Q04:R02-END
}

void WalManager::Inner::stop_background_writer() const {
    if (background_writer != nullptr) {
        background_writer->stop();
    }
}

[[maybe_unused]] auto WalManager::Inner::flush_through([[maybe_unused]] const base::Lsn target)
    -> base::Status {
    // LAB-S10.Q04:R03-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R03"});
    // LAB-S10.Q04:R03-END
}

[[maybe_unused]] auto WalManager::Inner::append_encoded([[maybe_unused]] const base::Bytes encoded)
    -> base::Result<WalInsertResult> {
    // LAB-S10.Q04:R04-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R04"});
    // LAB-S10.Q04:R04-END
}

void WalManager::Inner::force_stop() {
    const std::scoped_lock shutdown_lock{shutdown_mutex};
    stop_background_writer();
    state.store(ManagerState::kStopped, std::memory_order_release);
}

[[maybe_unused]] auto WalManager::open(
    [[maybe_unused]] const std::filesystem::path& directory,
    [[maybe_unused]] const WalManagerOptions& options
) -> base::Result<WalManager> {
    // LAB-S10.Q04:R05-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R05"});
    // LAB-S10.Q04:R05-END
}

WalManager::WalManager(std::shared_ptr<Inner> inner) noexcept
    : inner_{std::move(inner)} {}

WalManager::~WalManager() {
    if (!inner_) {
        return;
    }
    if (auto status = shutdown(); !status) {
        inner_->force_stop();
    }
}

WalManager::WalManager(WalManager&& other) noexcept = default;

auto WalManager::valid() const noexcept -> bool {
    return inner_ != nullptr;
}

auto WalManager::directory() const noexcept -> const std::filesystem::path& {
    static const std::filesystem::path kEmptyPath;
    return inner_ == nullptr ? kEmptyPath : inner_->directory;
}

auto WalManager::end_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : inner_->writer.reserved_lsn();
}

auto WalManager::written_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : inner_->writer.written_lsn();
}

auto WalManager::flushed_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : inner_->writer.flushed_lsn();
}

auto WalManager::redo_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn
                             : base::Lsn{inner_->redo.load(std::memory_order_acquire)};
}

auto WalManager::retention_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : inner_->writer.retention_lsn();
}

auto WalManager::startup_validation() const noexcept -> WalStartupValidation {
    return inner_ == nullptr ? WalStartupValidation{} : inner_->startup_validation;
}

auto WalManager::successful_sync_count() const noexcept -> std::uint64_t {
    return inner_ == nullptr ? 0U : inner_->writer.successful_sync_count();
}

auto WalManager::is_shutdown() const -> bool {
    if (inner_ == nullptr) {
        return true;
    }
    return inner_->state.load(std::memory_order_acquire) == ManagerState::kStopped;
}

[[maybe_unused]] auto WalManager::insert([[maybe_unused]] const WalRecord& record)
    -> base::Result<WalInsertResult> {
    // LAB-S10.Q04:R06-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R06"});
    // LAB-S10.Q04:R06-END
}

[[maybe_unused]] auto WalManager::insert_with_page_images(
    [[maybe_unused]] WalRecord record,
    [[maybe_unused]] const std::span<const WalPageImageRegistration> registrations
) -> base::Result<WalInsertResult> {
    // LAB-S10.Q05:R16-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R16"});
    // LAB-S10.Q05:R16-END
}

[[maybe_unused]] auto WalManager::begin_checkpoint() -> base::Result<WalInsertResult> {
    // LAB-S10.Q07:R1016-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q07:R1016"});
    // LAB-S10.Q07:R1016-END
}

[[maybe_unused]] auto WalManager::flush_through([[maybe_unused]] const base::Lsn target_lsn)
    -> base::Status {
    // LAB-S10.Q04:R07-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R07"});
    // LAB-S10.Q04:R07-END
}

[[maybe_unused]] auto WalManager::flush_commit(
    [[maybe_unused]] const base::Lsn target_lsn,
    [[maybe_unused]] const std::chrono::microseconds leader_delay
) -> base::Status {
    // LAB-S10.Q04:R08-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R08"});
    // LAB-S10.Q04:R08-END
}

[[maybe_unused]] auto WalManager::complete_checkpoint(
    [[maybe_unused]] WalControlStore& control_store,
    [[maybe_unused]] WalControlImage next_control,
    [[maybe_unused]] const base::Lsn checkpoint_end_lsn
) -> base::Status {
    // LAB-S10.Q07:R1017-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q07:R1017"});
    // LAB-S10.Q07:R1017-END
}

[[maybe_unused]] auto WalManager::recycle_before([[maybe_unused]] const base::Lsn durable_redo_lsn)
    -> base::Status {
    // LAB-S10.Q04:R09-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R09"});
    // LAB-S10.Q04:R09-END
}

[[maybe_unused]] auto WalManager::shutdown() -> base::Status {
    // LAB-S10.Q04:R10-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q04:R10"});
    // LAB-S10.Q04:R10-END
}

}  // namespace db::wal
