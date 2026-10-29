#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/geometry.hpp"
#include "db/wal/record.hpp"
#include "db/wal/segment_storage.hpp"

namespace db::wal {

inline constexpr std::size_t kWalReadAheadSize = std::size_t{256} * 1024U;

struct WalReaderOptions final {
    std::uint64_t system_identifier{0};
    std::uint32_t timeline{1};
    base::Lsn retention_lsn{base::kInvalidLsn};
};

struct DecodedWalRecord final {
    base::Lsn start_lsn{base::kInvalidLsn};
    base::Lsn end_lsn{base::kInvalidLsn};
    WalRecord record;

    auto operator==(const DecodedWalRecord&) const -> bool = default;
};

struct WalTailScanResult final {
    base::Lsn valid_end_lsn{base::kInvalidLsn};
    std::size_t records_scanned{0};
    bool incomplete_tail{false};
    bool requested_boundary_found{false};

    auto operator==(const WalTailScanResult&) const noexcept -> bool = default;
};

// Strict forward reader for the physical C++ WAL v1 stream. An independently
// opened reader owns segment storage; from_storage explicitly borrows it for
// manager tail validation and must not outlive the storage object.
class WalReader final {
public:
    static auto open(const std::filesystem::path& directory, WalReaderOptions options)
        -> base::Result<WalReader>;
    static auto from_storage(const WalSegmentStorage& storage) -> base::Result<WalReader>;
    static auto from_storage(WalSegmentStorage&& storage) -> base::Result<WalReader> = delete;

    ~WalReader();

    WalReader(const WalReader&) = delete;
    auto operator=(const WalReader&) -> WalReader& = delete;
    WalReader(WalReader&& other) noexcept;
    auto operator=(WalReader&& other) noexcept -> WalReader&;

    // Moving transfers the complete immutable stream snapshot. The source
    // becomes inert: observers return their invalid/zero sentinel and every
    // operation returns kUnavailable rather than dereferencing an empty PImpl.
    auto valid() const noexcept -> bool;
    auto position() const noexcept -> base::Lsn;
    auto stream_start() const noexcept -> base::Lsn;
    auto logical_end() const noexcept -> base::Lsn;
    auto storage_read_count() const noexcept -> std::uint64_t;

    auto seek(base::Lsn lsn) -> base::Status;
    auto read_next() -> base::Result<std::optional<DecodedWalRecord>>;

    // Returns the last complete physical record boundary. Only a physically
    // incomplete final page/header/body is truncatable; complete malformed or
    // checksum-invalid input remains a hard error.
    auto scan_valid_end(base::Lsn requested_boundary) -> base::Result<WalTailScanResult>;

private:
    struct Inner;

    explicit WalReader(std::unique_ptr<Inner> inner) noexcept;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::wal
