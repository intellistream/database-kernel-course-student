#include "commit_log_format.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::transaction {
namespace {

inline constexpr std::uint64_t kMetaMagic = 0x003154454D435246ULL;  // "FRCMET1\0"
inline constexpr std::uint64_t kSegmentMagic = 0x0031474553435246ULL;  // "FRCSEG1\0"
inline constexpr std::uint8_t kStatusMask = 0x03U;
inline constexpr std::array<std::byte, 20> kReservedBytes{};

auto format_error(std::string message, std::string operation) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "transaction/commit-log",
        std::move(operation)
    );
}

auto mutable_bytes_of(std::vector<std::byte>& bytes) noexcept -> base::MutableBytes {
    return base::MutableBytes{bytes.data(), bytes.size()};
}

auto is_all_zero(base::Bytes bytes) noexcept -> bool {
    return std::ranges::all_of(bytes, [](const std::byte byte) {
        return byte == std::byte{0};
    });
}

}  // namespace

auto encode_commit_log_metadata(const CommitLogMetadataImage& metadata)
    -> base::Result<std::vector<std::byte>> {
    std::vector<std::byte> encoded(kCommitLogMetadataSize, std::byte{0});
    base::LittleEndianWriter writer{encoded};
    auto status = writer.write_u64(kMetaMagic);
    if (status) {
        status = writer.write_u16(kCommitLogFormatVersion);
    }
    if (status) {
        status = writer.write_u16(static_cast<std::uint16_t>(kCommitLogMetadataSize));
    }
    if (status) {
        status = writer.write_u32(0U);
    }
    if (status) {
        status = writer.write_u64(metadata.system_identifier);
    }
    if (status) {
        status = writer.write_u64(metadata.generation);
    }
    if (status) {
        status = writer.write_u32(metadata.next_xid.value());
    }
    if (status) {
        status = writer.write_u32(metadata.segment_count);
    }
    if (status) {
        status = writer.write_u32(0U);
    }
    if (status) {
        status = writer.write_bytes(kReservedBytes);
    }
    if (!status) {
        return base::fail(
            std::move(status).error().with_context("transaction/commit-log", "encode metadata")
        );
    }
    if (auto sealed = base::seal_crc32c(mutable_bytes_of(encoded), kCommitLogChecksumOffset);
        !sealed) {
        return base::fail(
            std::move(sealed).error().with_context("transaction/commit-log", "encode metadata")
        );
    }
    return encoded;
}

auto decode_commit_log_metadata(const base::Bytes encoded) -> base::Result<CommitLogMetadataImage> {
    if (encoded.size() != kCommitLogMetadataSize) {
        return base::fail(
            format_error("CLOG metadata has an unexpected length", "decode metadata")
        );
    }
    if (auto status = base::verify_crc32c(encoded, kCommitLogChecksumOffset); !status) {
        return base::fail(
            std::move(status).error().with_context("transaction/commit-log", "decode metadata")
        );
    }
    base::LittleEndianReader reader{encoded};
    auto magic = reader.read_u64();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto reserved = reader.read_u32();
    auto system_identifier = reader.read_u64();
    auto generation = reader.read_u64();
    auto next_xid_value = reader.read_u32();
    auto segment_count = reader.read_u32();
    auto checksum = reader.read_u32();
    auto reserved_tail = reader.read_bytes(kReservedBytes.size());
    auto complete = reader.finish();
    if (!magic || !version || !header_size || !reserved || !system_identifier || !generation
        || !next_xid_value || !segment_count || !checksum || !reserved_tail || !complete) {
        return base::fail(format_error("CLOG metadata fields are truncated", "decode metadata"));
    }
    const auto next_xid = base::Xid{next_xid_value.value()};
    if (magic.value() != kMetaMagic || version.value() != kCommitLogFormatVersion
        || header_size.value() != kCommitLogMetadataSize || reserved.value() != 0U
        || system_identifier.value() == 0U || generation.value() == 0U
        || next_xid == base::kInvalidXid || segment_count.value() == 0U
        || !is_all_zero(reserved_tail.value())) {
        return base::fail(format_error("CLOG metadata fields are invalid", "decode metadata"));
    }
    const auto required_segments = (next_xid.value() - 1U) / kTransactionsPerCommitLogSegment + 1U;
    if (segment_count.value() < required_segments) {
        return base::fail(
            format_error("CLOG metadata omits a required segment", "decode metadata")
        );
    }
    return CommitLogMetadataImage{
        .system_identifier = system_identifier.value(),
        .generation = generation.value(),
        .next_xid = next_xid,
        .segment_count = segment_count.value(),
    };
}

auto encode_commit_log_segment(
    const std::uint64_t system_identifier,
    const std::uint32_t segment_number,
    const std::uint64_t generation,
    const base::Bytes payload
) -> base::Result<std::vector<std::byte>> {
    if (payload.size() != kCommitLogSegmentPayloadSize) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "CLOG segment encoder received an invalid payload size",
            }
        );
    }
    std::vector<std::byte> encoded(
        kCommitLogSegmentHeaderSize + kCommitLogSegmentPayloadSize,
        std::byte{0}
    );
    base::LittleEndianWriter writer{encoded};
    auto status = writer.write_u64(kSegmentMagic);
    if (status) {
        status = writer.write_u16(kCommitLogFormatVersion);
    }
    if (status) {
        status = writer.write_u16(static_cast<std::uint16_t>(kCommitLogSegmentHeaderSize));
    }
    if (status) {
        status = writer.write_u32(segment_number);
    }
    if (status) {
        status = writer.write_u64(system_identifier);
    }
    if (status) {
        status = writer.write_u32(segment_number * kTransactionsPerCommitLogSegment);
    }
    if (status) {
        status = writer.write_u32(static_cast<std::uint32_t>(kCommitLogSegmentPayloadSize));
    }
    if (status) {
        status = writer.write_u64(generation);
    }
    if (status) {
        status = writer.write_u32(0U);
    }
    if (status) {
        status = writer.write_bytes(kReservedBytes);
    }
    if (status) {
        status = writer.write_bytes(payload);
    }
    if (!status) {
        return base::fail(
            std::move(status).error().with_context("transaction/commit-log", "encode segment")
        );
    }
    if (auto sealed = base::seal_crc32c(mutable_bytes_of(encoded), kCommitLogChecksumOffset);
        !sealed) {
        return base::fail(
            std::move(sealed).error().with_context("transaction/commit-log", "encode segment")
        );
    }
    return encoded;
}

auto decode_commit_log_segment(
    const base::Bytes encoded,
    const std::uint64_t system_identifier,
    const std::uint32_t expected_segment_number
) -> base::Result<CommitLogSegmentImage> {
    if (encoded.size() != kCommitLogSegmentHeaderSize + kCommitLogSegmentPayloadSize) {
        return base::fail(format_error("CLOG segment has an unexpected length", "decode segment"));
    }
    if (auto status = base::verify_crc32c(encoded, kCommitLogChecksumOffset); !status) {
        return base::fail(
            std::move(status).error().with_context("transaction/commit-log", "decode segment")
        );
    }
    base::LittleEndianReader reader{encoded};
    auto magic = reader.read_u64();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto segment_number = reader.read_u32();
    auto stored_system = reader.read_u64();
    auto first_xid = reader.read_u32();
    auto payload_size = reader.read_u32();
    auto generation = reader.read_u64();
    auto checksum = reader.read_u32();
    auto reserved = reader.read_bytes(kReservedBytes.size());
    auto payload = reader.read_bytes(kCommitLogSegmentPayloadSize);
    auto complete = reader.finish();
    if (!magic || !version || !header_size || !segment_number || !stored_system || !first_xid
        || !payload_size || !generation || !checksum || !reserved || !payload || !complete) {
        return base::fail(format_error("CLOG segment fields are truncated", "decode segment"));
    }
    if (magic.value() != kSegmentMagic || version.value() != kCommitLogFormatVersion
        || header_size.value() != kCommitLogSegmentHeaderSize
        || segment_number.value() != expected_segment_number
        || stored_system.value() != system_identifier
        || first_xid.value() != segment_number.value() * kTransactionsPerCommitLogSegment
        || payload_size.value() != kCommitLogSegmentPayloadSize || generation.value() == 0U
        || !is_all_zero(reserved.value())) {
        return base::fail(format_error("CLOG segment header fields are invalid", "decode segment"));
    }

    for (const std::byte byte : payload.value()) {
        const auto value = std::to_integer<std::uint8_t>(byte);
        for (std::uint8_t shift = 0; shift < 8; shift = static_cast<std::uint8_t>(shift + 2U)) {
            if (((value >> shift) & kStatusMask) == kStatusMask) {
                return base::fail(
                    format_error("CLOG segment contains reserved status bits", "decode segment")
                );
            }
        }
    }

    return CommitLogSegmentImage{
        .generation = generation.value(),
        .payload = {payload->begin(), payload->end()},
    };
}

}  // namespace db::transaction
