#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::wal {

inline constexpr std::size_t kMaximumWalPreallocationSegments = 16;

enum class WalStorageFaultPoint : std::uint8_t {
    kWrite,
    kSync,
    kTruncate,
    kRecycle,
};

// Optional composition/test port. Implementations must be thread-safe and must
// not re-enter the storage instance from before_operation(). Escaped exceptions
// are converted to structured storage errors at this consumer-owned boundary.
class WalStorageFaultPort {
public:
    WalStorageFaultPort() = default;
    WalStorageFaultPort(const WalStorageFaultPort&) = delete;
    auto operator=(const WalStorageFaultPort&) -> WalStorageFaultPort& = delete;
    WalStorageFaultPort(WalStorageFaultPort&&) = delete;
    auto operator=(WalStorageFaultPort&&) -> WalStorageFaultPort& = delete;
    virtual ~WalStorageFaultPort() = default;

    virtual auto before_operation(WalStorageFaultPoint point) -> base::Status = 0;
};

struct WalSegmentStorageOptions final {
    std::uint64_t system_identifier{0};
    std::uint32_t timeline{1};
    base::Lsn retention_lsn{base::kInvalidLsn};
    bool create{false};
    std::shared_ptr<WalStorageFaultPort> fault_port{};
};

// Thread-safe logical WAL byte storage backed by independently checksummed
// fixed-capacity segment files. Physical segment headers are outside LSN space.
class WalSegmentStorage final {
public:
    static auto
    open(const std::filesystem::path& directory, const WalSegmentStorageOptions& options)
        -> base::Result<WalSegmentStorage>;

    ~WalSegmentStorage();

    WalSegmentStorage(const WalSegmentStorage&) = delete;
    auto operator=(const WalSegmentStorage&) -> WalSegmentStorage& = delete;
    WalSegmentStorage(WalSegmentStorage&& other) noexcept;
    auto operator=(WalSegmentStorage&& other) noexcept -> WalSegmentStorage&;

    // Moving transfers ownership and leaves the source as an inert value.
    auto valid() const noexcept -> bool;
    auto directory() const noexcept -> const std::filesystem::path&;
    auto system_identifier() const noexcept -> std::uint64_t;
    auto timeline() const noexcept -> std::uint32_t;
    auto logical_size() const noexcept -> std::uint64_t;
    auto retention_lsn() const noexcept -> base::Lsn;

    auto segment_path(std::uint64_t segment_number) const -> std::filesystem::path;
    auto allocated_segment_count() const -> base::Result<std::size_t>;

    // Creates and durably publishes header-only segments through the segment
    // containing logical_end_exclusive - 1. It does not change logical_size().
    auto preallocate_through(std::uint64_t logical_end_exclusive) -> base::Status;

    auto read_exact_at(std::uint64_t offset, base::MutableBytes output) const -> base::Status;
    auto write_all_at(std::uint64_t offset, base::Bytes input) -> base::Status;
    auto sync() -> base::Status;

    // Tail truncation is for a reader-validated logical boundary. Cleanup may
    // remove only whole segments before an already-durable retention horizon.
    auto truncate_to(std::uint64_t logical_end) -> base::Status;
    auto remove_segments_before(base::Lsn durable_retention_lsn) -> base::Status;

private:
    struct Inner;

    explicit WalSegmentStorage(std::unique_ptr<Inner> inner) noexcept;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::wal
