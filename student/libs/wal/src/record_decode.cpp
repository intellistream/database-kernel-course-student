#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/wal/format.hpp"
#include "db/wal/record.hpp"

#include "record_image.hpp"

namespace db::wal {
namespace {

struct WireBlockHeader final {
    base::RelationId relation_id{base::kInvalidRelationId};
    base::PageId page_id{base::kInvalidPageId};
    std::uint32_t image_length{0};
    std::uint32_t delta_length{0};
    std::uint8_t block_id{0};
    CompressionMethod compression{CompressionMethod::kNone};
    std::uint16_t flags{0};
    std::uint16_t hole_offset{0};
    std::uint16_t hole_length{0};
};

struct RecordSections final {
    std::size_t block_data_start{0};
    std::size_t main_start{0};
    std::size_t padding{0};
};

struct DecodedBlocks final {
    std::vector<WalBlockReference> blocks;
    std::size_t bytes_consumed{0};
};

auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.record",
        "decode"
    );
}

auto validate_image_metadata(const WireBlockHeader& header) -> base::Status {
    const bool has_image = (header.flags & kWalBlockHasImage) != 0;
    if (!has_image) {
        if (header.compression != CompressionMethod::kNone || header.hole_offset != 0
            || header.hole_length != 0) {
            return base::fail(corrupt("WAL block without an image carries image metadata"));
        }
        return {};
    }
    return record_image::validate_metadata(
        header.compression,
        header.image_length,
        WalPageImageHole{.offset = header.hole_offset, .length = header.hole_length},
        (header.flags & kWalBlockStandardPage) != 0
    );
}

auto validate_wire_block_header(const WireBlockHeader& header, const std::size_t expected_block_id)
    -> base::Status {
    if (header.block_id != expected_block_id) {
        return base::fail(corrupt("WAL block identifiers are not canonical and unique"));
    }
    if (header.relation_id == base::kInvalidRelationId || header.page_id == base::kInvalidPageId) {
        return base::fail(corrupt("WAL block contains an invalid page address"));
    }
    if ((header.flags & static_cast<std::uint16_t>(~kWalBlockKnownFlags)) != 0) {
        return base::fail(corrupt("WAL block contains unknown flags"));
    }
    if (header.compression != CompressionMethod::kNone
        && header.compression != CompressionMethod::kRle) {
        return base::fail(corrupt("WAL block contains an unknown compression method"));
    }
    const bool has_image = (header.flags & kWalBlockHasImage) != 0;
    const bool has_delta = (header.flags & kWalBlockHasDelta) != 0;
    if (has_image != (header.image_length != 0) || has_delta != (header.delta_length != 0)) {
        return base::fail(corrupt("WAL block presence flags disagree with section lengths"));
    }
    return validate_image_metadata(header);
}

auto decode_block_header(const base::Bytes bytes, const std::size_t expected_block_id)
    -> base::Result<WireBlockHeader> {
    base::LittleEndianReader reader{bytes};
    auto relation_id = reader.read_u32();
    auto page_id = reader.read_u32();
    auto image_length = reader.read_u32();
    auto delta_length = reader.read_u32();
    auto block_id = reader.read_u8();
    auto compression = reader.read_u8();
    auto flags = reader.read_u16();
    auto hole_offset = reader.read_u16();
    auto hole_length = reader.read_u16();
    if (!relation_id || !page_id || !image_length || !delta_length || !block_id || !compression
        || !flags || !hole_offset || !hole_length) {
        return base::fail(corrupt("WAL block header is truncated"));
    }
    WireBlockHeader header{
        .relation_id = base::RelationId{relation_id.value()},
        .page_id = base::PageId{page_id.value()},
        .image_length = image_length.value(),
        .delta_length = delta_length.value(),
        .block_id = block_id.value(),
        .compression = static_cast<CompressionMethod>(compression.value()),
        .flags = flags.value(),
        .hole_offset = hole_offset.value(),
        .hole_length = hole_length.value(),
    };
    if (auto status = validate_wire_block_header(header, expected_block_id); !status) {
        return base::fail(std::move(status).error());
    }
    return header;
}

auto validate_record_envelope(const base::Bytes bytes, const WalRecordHeader& header)
    -> base::Status {
    if (bytes.size() < kWalRecordHeaderSize || bytes.size() > kMaximumWalRecordSize) {
        return base::fail(corrupt("WAL record has an invalid size"));
    }
    if (header.total_length != bytes.size() || bytes.size() % 8U != 0) {
        return base::fail(corrupt("WAL record length is not exact and aligned"));
    }
    if (header.block_count > kMaximumWalBlockReferences) {
        return base::fail(corrupt("WAL record has too many block references"));
    }
    if (auto checksum = base::verify_crc32c(bytes, kWalRecordChecksumOffset); !checksum) {
        return base::fail(std::move(checksum).error().with_context("wal.record", "decode"));
    }
    return {};
}

auto decode_wire_headers(const base::Bytes bytes, const std::uint16_t block_count)
    -> base::Result<std::vector<WireBlockHeader>> {
    std::vector<WireBlockHeader> headers;
    headers.reserve(block_count);
    std::size_t cursor = kWalRecordHeaderSize;
    for (std::size_t index = 0; index < block_count; ++index) {
        if (kWalBlockHeaderSize > bytes.size() - cursor) {
            return base::fail(corrupt("WAL block header section is truncated"));
        }
        auto block = decode_block_header(bytes.subspan(cursor, kWalBlockHeaderSize), index);
        if (!block) {
            return base::fail(std::move(block).error());
        }
        headers.push_back(std::move(block).value());
        cursor += kWalBlockHeaderSize;
    }
    return headers;
}

auto expected_record_flags(
    const std::span<const WireBlockHeader> headers,
    const std::size_t padding
) noexcept -> std::uint16_t {
    std::uint16_t flags = 0;
    for (const auto& header : headers) {
        if (header.image_length != 0) {
            flags |= kWalRecordHasFullPageImage;
        }
        if (header.delta_length != 0) {
            flags |= kWalRecordHasDelta;
        }
    }
    if (padding != 0) {
        flags |= kWalRecordPadding;
    }
    return flags;
}

auto locate_record_sections(
    const base::Bytes bytes,
    const WalRecordHeader& header,
    const std::span<const WireBlockHeader> wire_headers
) -> base::Result<RecordSections> {
    std::size_t cursor = kWalRecordHeaderSize + wire_headers.size() * kWalBlockHeaderSize;
    const auto block_data_start = cursor;
    for (const auto& block : wire_headers) {
        const auto section_length = static_cast<std::size_t>(block.image_length)
                                    + static_cast<std::size_t>(block.delta_length);
        if (section_length > bytes.size() - cursor) {
            return base::fail(corrupt("WAL block payload section is truncated"));
        }
        cursor += section_length;
    }
    if (header.payload_length > bytes.size() - cursor) {
        return base::fail(corrupt("WAL main payload section is truncated"));
    }
    const auto main_start = cursor;
    cursor += header.payload_length;
    const auto padding = bytes.size() - cursor;
    const auto padding_bytes = bytes.subspan(cursor);
    const bool zero_padding =
        std::all_of(padding_bytes.begin(), padding_bytes.end(), [](const std::byte byte) {
            return byte == std::byte{0};
        });
    if (padding > 7U || !zero_padding) {
        return base::fail(corrupt("WAL record has invalid padding"));
    }
    if (header.flags != expected_record_flags(wire_headers, padding)) {
        return base::fail(corrupt("WAL record flags disagree with its sections"));
    }
    return RecordSections{
        .block_data_start = block_data_start,
        .main_start = main_start,
        .padding = padding,
    };
}

auto decode_block(const base::Bytes bytes, const WireBlockHeader& header)
    -> base::Result<std::pair<WalBlockReference, std::size_t>> {
    WalBlockReference block{
        .relation_id = header.relation_id,
        .page_id = header.page_id,
        .will_initialize = (header.flags & kWalBlockWillInitialize) != 0,
        .standard_page = (header.flags & kWalBlockStandardPage) != 0,
        .full_page_image = std::nullopt,
        .image_hole =
            WalPageImageHole{
                .offset = header.hole_offset,
                .length = header.hole_length,
            },
        .delta = {},
    };
    std::size_t cursor = 0;
    if (header.image_length != 0) {
        auto image = record_image::decode(
            bytes.first(header.image_length),
            header.compression,
            WalPageImageHole{.offset = header.hole_offset, .length = header.hole_length},
            (header.flags & kWalBlockStandardPage) != 0
        );
        if (!image) {
            return base::fail(std::move(image).error());
        }
        block.full_page_image = std::move(image).value();
        cursor += header.image_length;
    }
    if (header.delta_length != 0) {
        const auto delta = bytes.subspan(cursor, header.delta_length);
        block.delta.assign(delta.begin(), delta.end());
        cursor += header.delta_length;
    }
    return std::pair<WalBlockReference, std::size_t>{std::move(block), cursor};
}

auto decode_blocks(const base::Bytes block_data, const std::span<const WireBlockHeader> headers)
    -> base::Result<DecodedBlocks> {
    DecodedBlocks result;
    result.blocks.reserve(headers.size());
    for (const auto& header : headers) {
        auto block = decode_block(block_data.subspan(result.bytes_consumed), header);
        if (!block) {
            return base::fail(std::move(block).error());
        }
        result.bytes_consumed += block->second;
        result.blocks.push_back(std::move(block->first));
    }
    if (result.bytes_consumed != block_data.size()) {
        return base::fail(corrupt("WAL block data cursor does not reach the main payload"));
    }
    return result;
}

}  // namespace

auto decode_record(const base::Bytes bytes) -> base::Result<WalRecord> {
    if (bytes.size() < kWalRecordHeaderSize || bytes.size() > kMaximumWalRecordSize) {
        return base::fail(corrupt("WAL record has an invalid size"));
    }
    auto header = decode_record_header(bytes.first(kWalRecordHeaderSize));
    if (!header) {
        return base::fail(std::move(header).error().with_context("wal.record", "decode"));
    }
    return decode_record(bytes, header.value());
}

auto decode_record(const base::Bytes bytes, const WalRecordHeader& header)
    -> base::Result<WalRecord> {
    if (auto envelope = validate_record_envelope(bytes, header); !envelope) {
        return base::fail(std::move(envelope).error());
    }
    auto wire_headers = decode_wire_headers(bytes, header.block_count);
    if (!wire_headers) {
        return base::fail(std::move(wire_headers).error());
    }
    auto sections = locate_record_sections(bytes, header, wire_headers.value());
    if (!sections) {
        return base::fail(std::move(sections).error());
    }
    const auto block_data_length = sections->main_start - sections->block_data_start;
    auto blocks = decode_blocks(
        bytes.subspan(sections->block_data_start, block_data_length),
        wire_headers.value()
    );
    if (!blocks) {
        return base::fail(std::move(blocks).error());
    }

    WalRecord record{
        .previous_lsn = header.previous_lsn,
        .xid = header.xid,
        .resource_manager = header.resource_manager,
        .info = header.info,
        .blocks = std::move(blocks->blocks),
        .main_payload = {},
    };
    const auto main_payload = bytes.subspan(sections->main_start, header.payload_length);
    record.main_payload.assign(main_payload.begin(), main_payload.end());
    return record;
}

}  // namespace db::wal
