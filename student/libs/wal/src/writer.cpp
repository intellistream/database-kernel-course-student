#include "db/wal/writer.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/wal/format.hpp"

#include "writer_io.hpp"

namespace db::wal {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.writer",
        "validate"
    );
}

[[maybe_unused]] auto invariant(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)}.with_context(
        "wal.writer",
        "maintain progress"
    );
}

[[maybe_unused]] auto inactive() -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, "WAL writer has been moved from"}
        .with_context("wal.writer", "access writer");
}

}  // namespace

struct WalWriter::Inner final {
    Inner(
        WalSegmentStorage owned_storage,
        WalRingBuffer owned_ring,
        const std::uint64_t initial_lsn
    )
        : storage{std::move(owned_storage)},
          ring{std::move(owned_ring)},
          initial{initial_lsn},
          written{initial_lsn},
          flushed{initial_lsn} {}

    WalSegmentStorage storage;
    WalRingBuffer ring;
    std::uint64_t initial;
    std::atomic<std::uint64_t> written;
    std::atomic<std::uint64_t> flushed;
    std::atomic<std::uint64_t> successful_syncs{0};

    std::mutex insertion_mutex;
    std::condition_variable insertion_completed;

    writer_detail::WriterIoCoordinator io;

    [[maybe_unused]] auto validate_target([[maybe_unused]] const base::Lsn target) const
        -> base::Status {
        // LAB-S10.Q02:R23-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R23"});
        // LAB-S10.Q02:R23-END
    }

    [[maybe_unused]] auto wait_for_insertion([[maybe_unused]] const base::Lsn target)
        -> base::Status {
        // LAB-S10.Q02:R24-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R24"});
        // LAB-S10.Q02:R24-END
    }

    [[maybe_unused]] auto write_completed_prefix([[maybe_unused]] const base::Lsn target)
        -> base::Status {
        // LAB-S10.Q02:R25-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R25"});
        // LAB-S10.Q02:R25-END
    }

    [[maybe_unused]] auto flush_with_delay(
        [[maybe_unused]] const base::Lsn target,
        [[maybe_unused]] const std::chrono::microseconds leader_delay
    ) -> base::Status {
        // LAB-S10.Q02:R26-BEGIN
        return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R26"});
        // LAB-S10.Q02:R26-END
    }
};

[[maybe_unused]] auto WalWriter::create(
    [[maybe_unused]] WalSegmentStorage storage,
    [[maybe_unused]] const std::size_t ring_page_count
) -> base::Result<WalWriter> {
    // LAB-S10.Q02:R27-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R27"});
    // LAB-S10.Q02:R27-END
}

WalWriter::WalWriter(std::unique_ptr<Inner> inner) noexcept
    : inner_{std::move(inner)} {}

WalWriter::~WalWriter() = default;

WalWriter::WalWriter(WalWriter&& other) noexcept = default;

auto WalWriter::operator=(WalWriter&& other) noexcept -> WalWriter& = default;

auto WalWriter::valid() const noexcept -> bool {
    return inner_ != nullptr;
}

auto WalWriter::reserved_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : inner_->ring.reserved_lsn();
}

auto WalWriter::inserted_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : inner_->ring.inserted_lsn();
}

auto WalWriter::written_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn
                             : base::Lsn{inner_->written.load(std::memory_order_acquire)};
}

auto WalWriter::flushed_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn
                             : base::Lsn{inner_->flushed.load(std::memory_order_acquire)};
}

auto WalWriter::retention_lsn() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : inner_->storage.retention_lsn();
}

auto WalWriter::successful_sync_count() const noexcept -> std::uint64_t {
    return inner_ == nullptr ? 0U : inner_->successful_syncs.load(std::memory_order_relaxed);
}

[[maybe_unused]] auto WalWriter::try_reserve([[maybe_unused]] const std::size_t payload_size)
    -> base::Result<WalReserveOutcome> {
    // LAB-S10.Q02:R28-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R28"});
    // LAB-S10.Q02:R28-END
}

[[maybe_unused]] auto WalWriter::copy_in(
    [[maybe_unused]] const WalReservation& reservation,
    [[maybe_unused]] const base::Bytes payload
) -> base::Status {
    // LAB-S10.Q02:R29-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R29"});
    // LAB-S10.Q02:R29-END
}

[[maybe_unused]] auto WalWriter::write_through([[maybe_unused]] const base::Lsn target_lsn)
    -> base::Status {
    // LAB-S10.Q02:R30-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R30"});
    // LAB-S10.Q02:R30-END
}

[[maybe_unused]] auto WalWriter::write_completed_pages() -> base::Result<bool> {
    // LAB-S10.Q02:R31-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R31"});
    // LAB-S10.Q02:R31-END
}

[[maybe_unused]] auto WalWriter::flush_through([[maybe_unused]] const base::Lsn target_lsn)
    -> base::Status {
    // LAB-S10.Q02:R32-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R32"});
    // LAB-S10.Q02:R32-END
}

[[maybe_unused]] auto WalWriter::flush_commit(
    [[maybe_unused]] const base::Lsn target_lsn,
    [[maybe_unused]] const std::chrono::microseconds leader_delay
) -> base::Status {
    // LAB-S10.Q02:R33-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R33"});
    // LAB-S10.Q02:R33-END
}

[[maybe_unused]] auto
WalWriter::remove_segments_before([[maybe_unused]] const base::Lsn durable_retention_lsn)
    -> base::Status {
    // LAB-S10.Q02:R34-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R34"});
    // LAB-S10.Q02:R34-END
}

}  // namespace db::wal
