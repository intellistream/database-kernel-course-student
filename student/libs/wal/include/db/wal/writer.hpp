#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/ring_buffer.hpp"
#include "db/wal/segment_storage.hpp"

namespace db::wal {

// Owns the in-memory ring and segment storage and serializes physical WAL I/O.
// Concurrent flush callers elect one leader; followers sleep on a completion
// generation and reuse the leader's write and sync progress.
class WalWriter final {
public:
    static auto create(WalSegmentStorage storage, std::size_t ring_page_count)
        -> base::Result<WalWriter>;

    ~WalWriter();

    WalWriter(const WalWriter&) = delete;
    auto operator=(const WalWriter&) -> WalWriter& = delete;
    WalWriter(WalWriter&& other) noexcept;
    auto operator=(WalWriter&& other) noexcept -> WalWriter&;

    // Moving transfers ownership and leaves the source as an inert value.
    auto valid() const noexcept -> bool;
    auto reserved_lsn() const noexcept -> base::Lsn;
    auto inserted_lsn() const noexcept -> base::Lsn;
    auto written_lsn() const noexcept -> base::Lsn;
    auto flushed_lsn() const noexcept -> base::Lsn;
    auto retention_lsn() const noexcept -> base::Lsn;
    auto successful_sync_count() const noexcept -> std::uint64_t;

    auto try_reserve(std::size_t payload_size) -> base::Result<WalReserveOutcome>;
    auto copy_in(const WalReservation& reservation, base::Bytes payload) -> base::Status;

    // Waits for the requested reservation prefix, then copies the newest
    // continuously completed prefix to the kernel without syncing it.
    auto write_through(base::Lsn target_lsn) -> base::Status;

    // Opportunistically writes only complete pages. The one oversized batch
    // exception is written whole so it cannot block later reservations.
    auto write_completed_pages() -> base::Result<bool>;

    // Elects one physical I/O leader, widens to the latest completed prefix,
    // writes it, performs one storage sync, and wakes all followers.
    auto flush_through(base::Lsn target_lsn) -> base::Status;

    // Commit-only group flush variant. A newly elected leader delays while it
    // owns the I/O role so concurrent committers can join the same sync.
    auto flush_commit(base::Lsn target_lsn, std::chrono::microseconds leader_delay) -> base::Status;

    // The caller must durably publish a control image that references this
    // retention horizon before removing any complete older segment.
    auto remove_segments_before(base::Lsn durable_retention_lsn) -> base::Status;

private:
    struct Inner;

    explicit WalWriter(std::unique_ptr<Inner> inner) noexcept;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::wal
