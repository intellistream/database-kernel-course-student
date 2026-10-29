#include "db/wal/record.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/wal/compression.hpp"
#include "db/wal/format.hpp"

#include "record_image.hpp"

namespace db::wal {
namespace {

struct BlockLayout final {
    std::optional<record_image::EncodedImage> image;
    base::Bytes delta;
};

struct RecordLayout final {
    std::vector<BlockLayout> blocks;
    std::size_t unpadded_length{0};
    std::size_t total_length{0};
    std::uint16_t flags{0};
};

auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.record",
        "encode"
    );
}

auto known_resource_manager(const WalResourceManager manager) noexcept -> bool {
    return manager >= WalResourceManager::kSystem && manager <= WalResourceManager::kCatalog;
}

auto make_block_layout(const WalBlockReference& block) -> base::Result<BlockLayout> {
    if (block.relation_id == base::kInvalidRelationId || block.page_id == base::kInvalidPageId) {
        return base::fail(invalid("WAL block contains an invalid page address"));
    }
    BlockLayout layout;
    if (block.full_page_image.has_value()) {
        auto image = record_image::encode(
            block.full_page_image.value(),
            block.image_hole,
            block.standard_page
        );
        if (!image) {
            return base::fail(std::move(image).error());
        }
        layout.image = std::move(image).value();
    } else if (block.image_hole.offset != 0 || block.image_hole.length != 0) {
        return base::fail(invalid("WAL block without an image carries hole metadata"));
    }
    layout.delta = base::Bytes{block.delta};
    return layout;
}

auto add_record_size(const std::size_t current, const std::size_t amount)
    -> base::Result<std::size_t> {
    if (current > kMaximumWalRecordSize || amount > kMaximumWalRecordSize - current) {
        return base::fail(invalid("WAL record exceeds the maximum size"));
    }
    return current + amount;
}

auto validate_record_input(const WalRecord& record) -> base::Status {
    if (!known_resource_manager(record.resource_manager)) {
        return base::fail(invalid("unknown WAL resource manager"));
    }
    if (record.blocks.size() > kMaximumWalBlockReferences
        || record.blocks.size() > std::numeric_limits<std::uint16_t>::max()) {
        return base::fail(invalid("too many WAL block references"));
    }
    if (record.main_payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(invalid("WAL main payload is too large"));
    }
    return {};
}

auto make_record_layout(const WalRecord& record, RecordLayout& result) -> base::Status {
    const auto fixed_length = kWalRecordHeaderSize + record.blocks.size() * kWalBlockHeaderSize;
    auto length = add_record_size(fixed_length, record.main_payload.size());
    if (!length) {
        return base::fail(std::move(length).error());
    }
    result.blocks.clear();
    result.blocks.reserve(record.blocks.size());
    result.unpadded_length = 0U;
    result.total_length = 0U;
    result.flags = 0U;
    bool has_image = false;
    bool has_delta = false;
    for (const auto& block : record.blocks) {
        auto layout = make_block_layout(block);
        if (!layout) {
            return base::fail(std::move(layout).error());
        }
        const auto image_length = layout->image.has_value() ? layout->image->bytes.size() : 0;
        length = add_record_size(length.value(), image_length);
        if (!length) {
            return base::fail(std::move(length).error());
        }
        length = add_record_size(length.value(), layout->delta.size());
        if (!length) {
            return base::fail(std::move(length).error());
        }
        has_image = has_image || layout->image.has_value();
        has_delta = has_delta || !layout->delta.empty();
        result.blocks.push_back(std::move(layout).value());
    }
    if (length.value() > kMaximumWalRecordSize - 7U) {
        return base::fail(invalid("WAL record cannot be aligned within the maximum size"));
    }
    result.unpadded_length = length.value();
    result.total_length = (length.value() + 7U) & ~std::size_t{7U};
    if (has_image) {
        result.flags |= kWalRecordHasFullPageImage;
    }
    if (has_delta) {
        result.flags |= kWalRecordHasDelta;
    }
    if (result.total_length != result.unpadded_length) {
        result.flags |= kWalRecordPadding;
    }
    return {};
}

auto write_block_header(
    const base::MutableBytes output,
    const WalBlockReference& block,
    const BlockLayout& layout,
    const std::uint8_t block_id
) -> base::Status {
    std::uint16_t flags = 0;
    if (layout.image.has_value()) {
        flags |= kWalBlockHasImage;
    }
    if (!layout.delta.empty()) {
        flags |= kWalBlockHasDelta;
    }
    if (block.will_initialize) {
        flags |= kWalBlockWillInitialize;
    }
    if (block.standard_page) {
        flags |= kWalBlockStandardPage;
    }
    const auto image_length = layout.image.has_value() ? layout.image->bytes.size() : 0;
    const auto compression =
        layout.image.has_value() ? layout.image->compression : CompressionMethod::kNone;
    const auto hole_offset =
        layout.image.has_value() ? layout.image->hole.offset : std::uint16_t{0};
    const auto hole_length =
        layout.image.has_value() ? layout.image->hole.length : std::uint16_t{0};

    base::LittleEndianWriter writer{output};
    auto status = writer.write_u32(block.relation_id.value());
    if (status) {
        status = writer.write_u32(block.page_id.value());
    }
    if (status) {
        status = writer.write_u32(static_cast<std::uint32_t>(image_length));
    }
    if (status) {
        status = writer.write_u32(static_cast<std::uint32_t>(layout.delta.size()));
    }
    if (status) {
        status = writer.write_u8(block_id);
    }
    if (status) {
        status = writer.write_u8(static_cast<std::uint8_t>(compression));
    }
    if (status) {
        status = writer.write_u16(flags);
    }
    if (status) {
        status = writer.write_u16(hole_offset);
    }
    if (status) {
        status = writer.write_u16(hole_length);
    }
    if (!status || writer.position() != kWalBlockHeaderSize) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "WAL block encoder did not fill its fixed header",
            }
                .with_context("wal.record", "encode block header")
        );
    }
    return {};
}

auto write_record_headers(
    const base::MutableBytes output,
    const WalRecord& record,
    const RecordLayout& layout
) -> base::Status {
    std::size_t cursor = kWalRecordHeaderSize;
    for (std::size_t index = 0; index < record.blocks.size(); ++index) {
        auto status = write_block_header(
            output.subspan(cursor, kWalBlockHeaderSize),
            record.blocks.at(index),
            layout.blocks.at(index),
            static_cast<std::uint8_t>(index)
        );
        if (!status) {
            return status;
        }
        cursor += kWalBlockHeaderSize;
    }
    return {};
}

auto write_record_payload(
    const base::MutableBytes output,
    const WalRecord& record,
    const RecordLayout& layout
) -> base::Status {
    const auto payload_start = kWalRecordHeaderSize + record.blocks.size() * kWalBlockHeaderSize;
    base::LittleEndianWriter writer{
        output.subspan(payload_start, layout.unpadded_length - payload_start)
    };
    base::Status status;
    for (const auto& block : layout.blocks) {
        if (block.image.has_value() && status) {
            status = writer.write_bytes(block.image->bytes);
        }
        if (status) {
            status = writer.write_bytes(block.delta);
        }
    }
    if (status) {
        status = writer.write_bytes(record.main_payload);
    }
    if (!status || writer.position() != layout.unpadded_length - payload_start) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "WAL record encoder produced an inconsistent payload layout",
            }
                .with_context("wal.record", "encode payload")
        );
    }
    return {};
}

}  // namespace

auto encode_record_into(const WalRecord& record, std::vector<std::byte>& output) -> base::Status {
    if (auto status = validate_record_input(record); !status) {
        return base::fail(std::move(status).error());
    }
    thread_local RecordLayout layout;
    if (auto status = make_record_layout(record, layout); !status) {
        return base::fail(std::move(status).error());
    }
    WalRecordHeader header{
        .total_length = static_cast<std::uint32_t>(layout.total_length),
        .payload_length = static_cast<std::uint32_t>(record.main_payload.size()),
        .previous_lsn = record.previous_lsn,
        .xid = record.xid,
        .resource_manager = record.resource_manager,
        .info = record.info,
        .block_count = static_cast<std::uint16_t>(record.blocks.size()),
        .flags = layout.flags,
        .checksum = 0,
    };
    auto encoded_header = encode_record_header(header);
    if (!encoded_header) {
        return base::fail(std::move(encoded_header).error().with_context("wal.record", "encode"));
    }

    output.clear();
    output.resize(layout.total_length);
    std::copy(encoded_header->begin(), encoded_header->end(), output.begin());
    if (auto status = write_record_headers(output, record, layout); !status) {
        return base::fail(std::move(status).error());
    }
    if (auto status = write_record_payload(output, record, layout); !status) {
        return base::fail(std::move(status).error());
    }
    if (auto status = base::seal_crc32c(output, kWalRecordChecksumOffset); !status) {
        return base::fail(std::move(status).error().with_context("wal.record", "encode"));
    }
    return {};
}

auto encode_record(const WalRecord& record) -> base::Result<std::vector<std::byte>> {
    std::vector<std::byte> encoded;
    if (auto status = encode_record_into(record, encoded); !status) {
        return base::fail(std::move(status).error());
    }
    return encoded;
}

}  // namespace db::wal
