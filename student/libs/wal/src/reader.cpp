#include "db/wal/reader.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/result.hpp"
#include "db/wal/record.hpp"
#include "db/wal/segment_storage.hpp"

#include "reader_dispatch.hpp"
#include "reader_framing.hpp"

namespace db::wal {
namespace {

[[maybe_unused]] auto unavailable(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, std::move(message)}.with_context(
        "wal.reader",
        "access stream"
    );
}

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.reader",
        "decode stream"
    );
}

using NextResult =
    std::variant<DecodedWalRecord, reader_detail::EndOfStream, reader_detail::IncompleteTail>;

}  // namespace

struct WalReader::Inner final {
    Inner(
        std::unique_ptr<WalSegmentStorage> owned,
        const WalSegmentStorage* borrowed,
        const reader_detail::StreamSnapshot snapshot
    )
        : owned_storage{std::move(owned)},
          storage{owned_storage ? owned_storage.get() : borrowed},
          framing{*storage, snapshot, kWalReadAheadSize} {}

    std::unique_ptr<WalSegmentStorage> owned_storage;
    const WalSegmentStorage* storage;
    reader_detail::FramingCursor framing;

    static auto create(std::unique_ptr<WalSegmentStorage> owned, const WalSegmentStorage* borrowed)
        -> base::Result<std::unique_ptr<Inner>>;
    auto read_next() -> base::Result<NextResult>;
};

[[maybe_unused]] auto WalReader::Inner::create(
    [[maybe_unused]] std::unique_ptr<WalSegmentStorage> owned,
    [[maybe_unused]] const WalSegmentStorage* borrowed
) -> base::Result<std::unique_ptr<WalReader::Inner>> {
    // LAB-S10.Q03:R01-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R01"});
    // LAB-S10.Q03:R01-END
}

[[maybe_unused]] auto WalReader::Inner::read_next() -> base::Result<NextResult> {
    // LAB-S10.Q03:R02-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R02"});
    // LAB-S10.Q03:R02-END
}

namespace {

[[maybe_unused]] auto incomplete_error(const reader_detail::IncompleteTail& incomplete)
    -> base::Error {
    return corrupt(
        "incomplete WAL " + incomplete.part + " at LSN " + std::to_string(incomplete.lsn)
    );
}

}  // namespace

[[maybe_unused]] auto WalReader::open(
    [[maybe_unused]] const std::filesystem::path& directory,
    [[maybe_unused]] const WalReaderOptions options
) -> base::Result<WalReader> {
    // LAB-S10.Q03:R03-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R03"});
    // LAB-S10.Q03:R03-END
}

[[maybe_unused]] auto WalReader::from_storage([[maybe_unused]] const WalSegmentStorage& storage)
    -> base::Result<WalReader> {
    // LAB-S10.Q03:R04-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R04"});
    // LAB-S10.Q03:R04-END
}

WalReader::WalReader(std::unique_ptr<Inner> inner) noexcept
    : inner_{std::move(inner)} {}

WalReader::~WalReader() = default;
WalReader::WalReader(WalReader&& other) noexcept = default;
auto WalReader::operator=(WalReader&& other) noexcept -> WalReader& = default;

auto WalReader::valid() const noexcept -> bool {
    return inner_ != nullptr;
}

auto WalReader::position() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : base::Lsn{inner_->framing.position()};
}

auto WalReader::stream_start() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : base::Lsn{inner_->framing.stream_start()};
}

auto WalReader::logical_end() const noexcept -> base::Lsn {
    return inner_ == nullptr ? base::kInvalidLsn : base::Lsn{inner_->framing.logical_end()};
}

auto WalReader::storage_read_count() const noexcept -> std::uint64_t {
    return inner_ == nullptr ? 0U : inner_->framing.storage_read_count();
}

[[maybe_unused]] auto WalReader::seek([[maybe_unused]] const base::Lsn lsn) -> base::Status {
    // LAB-S10.Q03:R05-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R05"});
    // LAB-S10.Q03:R05-END
}

[[maybe_unused]] auto WalReader::read_next() -> base::Result<std::optional<DecodedWalRecord>> {
    // LAB-S10.Q03:R06-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R06"});
    // LAB-S10.Q03:R06-END
}

[[maybe_unused]] auto WalReader::scan_valid_end([[maybe_unused]] const base::Lsn requested_boundary)
    -> base::Result<WalTailScanResult> {
    // LAB-S10.Q03:R07-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R07"});
    // LAB-S10.Q03:R07-END
}

}  // namespace db::wal
