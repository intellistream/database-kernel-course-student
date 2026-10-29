#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/geometry.hpp"

namespace db::wal {

inline constexpr std::size_t kMaximumWalRingPages = std::size_t{16} * 1024U;
inline constexpr base::Lsn kInitialWalRingLsn{kWalPageSize};

struct WalReservation final {
    std::uint64_t identifier{0};
    base::Lsn start_lsn{base::kInvalidLsn};
    base::Lsn end_lsn{base::kInvalidLsn};
    std::size_t payload_size{0};
    bool oversized{false};

    auto operator==(const WalReservation&) const noexcept -> bool = default;
};

struct WalBackpressure final {
    base::Lsn write_target{base::kInvalidLsn};

    auto operator==(const WalBackpressure&) const noexcept -> bool = default;
};

using WalReserveOutcome = std::variant<WalReservation, WalBackpressure>;

// Thread-safe circular buffer for physical C++ WAL v1 bytes. Reservation is
// serialized, while disjoint reservation payloads may be copied concurrently.
class WalRingBuffer final {
public:
    static auto create(std::size_t page_count, base::Lsn initial_lsn = kInitialWalRingLsn)
        -> base::Result<WalRingBuffer>;

    ~WalRingBuffer();

    WalRingBuffer(const WalRingBuffer&) = delete;
    auto operator=(const WalRingBuffer&) -> WalRingBuffer& = delete;
    WalRingBuffer(WalRingBuffer&& other) noexcept;
    auto operator=(WalRingBuffer&& other) noexcept -> WalRingBuffer&;

    // Moving transfers ownership and leaves the source as an inert value.
    auto valid() const noexcept -> bool;
    auto page_count() const noexcept -> std::size_t;
    auto payload_capacity() const noexcept -> std::size_t;
    auto released_lsn() const noexcept -> base::Lsn;
    auto reserved_lsn() const noexcept -> base::Lsn;
    auto inserted_lsn() const noexcept -> base::Lsn;
    auto oversized_batch_active() const -> bool;

    auto try_reserve(std::size_t payload_size) -> base::Result<WalReserveOutcome>;
    auto copy_in(const WalReservation& reservation, base::Bytes payload) -> base::Status;

    // Copies already completed physical bytes, including page headers and
    // canonical zero padding, from [start_lsn, end_lsn).
    auto copy_out(base::Lsn start_lsn, base::Lsn end_lsn, std::vector<std::byte>& output) const
        -> base::Status;

    // Releases complete ring pages before a written prefix. An oversized batch
    // is released only as a whole.
    auto release_before(base::Lsn written_lsn) -> base::Status;

private:
    struct Inner;

    explicit WalRingBuffer(std::unique_ptr<Inner> inner) noexcept;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::wal
