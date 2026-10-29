#include "segment_file.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"

namespace db::wal::segment_file {
namespace {

auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.segment_storage",
        "validate"
    );
}

auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.segment_storage",
        "open"
    );
}

}  // namespace

auto make_header(
    const std::uint64_t system_identifier,
    const std::uint32_t timeline,
    const std::uint64_t segment_number
) -> base::Result<WalSegmentHeaderBytes> {
    if (segment_number > std::numeric_limits<std::uint64_t>::max() / kWalSegmentSize) {
        return base::fail(invalid("WAL segment number overflows its first LSN"));
    }
    auto header = encode_segment_header(
        WalSegmentHeader{
            .system_identifier = system_identifier,
            .segment_number = segment_number,
            .first_lsn = base::Lsn{segment_number * kWalSegmentSize},
            .timeline = timeline,
            .checksum = 0,
        }
    );
    if (!header) {
        return base::fail(
            std::move(header).error().with_context("wal.segment_storage", "encode segment header")
        );
    }
    if (auto status = base::seal_crc32c(header.value(), kWalSegmentChecksumOffset); !status) {
        return base::fail(
            std::move(status).error().with_context("wal.segment_storage", "seal segment header")
        );
    }
    return std::move(header).value();
}

auto open_existing(
    const std::filesystem::path& path,
    const std::uint64_t expected_system_identifier,
    const std::uint32_t expected_timeline,
    const std::uint64_t expected_segment_number
) -> base::Result<OpenedSegment> {
    auto file =
        io::PosixFile::open(path, io::FileOpenOptions{.access = io::FileAccess::kReadWrite});
    if (!file) {
        return base::fail(
            std::move(file).error().with_context("wal.segment_storage", "open segment")
        );
    }
    auto physical_size = file->size();
    if (!physical_size) {
        return base::fail(std::move(physical_size).error());
    }
    if (physical_size.value() < kWalSegmentHeaderSize
        || physical_size.value() - kWalSegmentHeaderSize > kWalSegmentSize) {
        return base::fail(corrupt("WAL segment has an invalid physical length"));
    }
    WalSegmentHeaderBytes encoded{};
    if (auto status = file->read_exact_at(0, encoded); !status) {
        return base::fail(
            std::move(status).error().with_context("wal.segment_storage", "read segment header")
        );
    }
    if (auto status = base::verify_crc32c(encoded, kWalSegmentChecksumOffset); !status) {
        return base::fail(
            std::move(status).error().with_context("wal.segment_storage", "validate segment header")
        );
    }
    auto header = decode_segment_header(encoded);
    if (!header) {
        return base::fail(
            std::move(header).error().with_context("wal.segment_storage", "decode segment header")
        );
    }
    if (header->system_identifier != expected_system_identifier
        || header->timeline != expected_timeline
        || header->segment_number != expected_segment_number) {
        return base::fail(corrupt("WAL segment identity does not match the storage instance"));
    }
    return OpenedSegment{
        .file = std::move(file).value(),
        .data_length = physical_size.value() - kWalSegmentHeaderSize,
    };
}

}  // namespace db::wal::segment_file
