#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/wal/format.hpp"

#include "format_internal.hpp"

namespace db::wal {
namespace {

using format_internal::corrupt;
using format_internal::finish_write;
using format_internal::invalid;
using format_internal::unsupported;

auto valid_resource_manager(const WalResourceManager manager) noexcept -> bool {
    return manager >= WalResourceManager::kSystem && manager <= WalResourceManager::kCatalog;
}

auto validate_record(const WalRecordHeader& header, const bool decoding) -> base::Status {
    const auto error = [decoding](std::string message) {
        return base::fail(decoding ? corrupt(std::move(message)) : invalid(std::move(message)));
    };
    if (!valid_resource_manager(header.resource_manager)) {
        return error("unknown WAL resource manager");
    }
    if ((header.flags & static_cast<std::uint16_t>(~kWalRecordKnownFlags)) != 0) {
        return error("unknown WAL record flags");
    }
    if (header.total_length < kWalRecordHeaderSize || header.total_length > kMaximumWalRecordSize) {
        return error("WAL record length is outside the supported range");
    }
    const auto block_bytes = static_cast<std::uint64_t>(header.block_count) * kWalBlockHeaderSize;
    const auto minimum =
        static_cast<std::uint64_t>(kWalRecordHeaderSize) + block_bytes + header.payload_length;
    if (minimum > header.total_length) {
        return error("WAL record sections exceed its total length");
    }
    return {};
}

}  // namespace

auto encode_record_header(const WalRecordHeader& header) -> base::Result<WalRecordHeaderBytes> {
    if (auto status = validate_record(header, false); !status) {
        return base::fail(std::move(status).error());
    }
    WalRecordHeaderBytes bytes{};
    base::LittleEndianWriter writer{bytes};
    auto status = writer.write_u32(kWalRecordMagic);
    if (status) {
        status = writer.write_u16(kWalFormatVersion);
    }
    if (status) {
        status = writer.write_u16(kWalRecordHeaderSize);
    }
    if (status) {
        status = writer.write_u32(header.total_length);
    }
    if (status) {
        status = writer.write_u32(header.payload_length);
    }
    if (status) {
        status = writer.write_u64(header.previous_lsn.value());
    }
    if (status) {
        status = writer.write_u32(header.xid.value());
    }
    if (status) {
        status = writer.write_u8(static_cast<std::uint8_t>(header.resource_manager));
    }
    if (status) {
        status = writer.write_u8(header.info);
    }
    if (status) {
        status = writer.write_u16(header.block_count);
    }
    if (status) {
        status = writer.write_u16(header.flags);
    }
    if (status) {
        status = writer.write_u16(0);
    }
    if (status) {
        status = writer.write_u32(header.checksum);
    }
    if (auto finished = finish_write(status, writer, kWalRecordHeaderSize); !finished) {
        return base::fail(std::move(finished).error());
    }
    return bytes;
}

auto decode_record_header(const base::Bytes bytes) -> base::Result<WalRecordHeader> {
    if (bytes.size() != kWalRecordHeaderSize) {
        return base::fail(corrupt("WAL record header has the wrong length"));
    }
    base::LittleEndianReader reader{bytes};
    auto magic = reader.read_u32();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto total_length = reader.read_u32();
    auto payload_length = reader.read_u32();
    auto previous_lsn = reader.read_u64();
    auto xid = reader.read_u32();
    auto resource_manager = reader.read_u8();
    auto info = reader.read_u8();
    auto block_count = reader.read_u16();
    auto flags = reader.read_u16();
    auto reserved = reader.read_u16();
    auto checksum = reader.read_u32();
    if (!magic || !version || !header_size || !total_length || !payload_length || !previous_lsn
        || !xid || !resource_manager || !info || !block_count || !flags || !reserved || !checksum) {
        return base::fail(corrupt("WAL record header is truncated"));
    }
    if (magic.value() != kWalRecordMagic) {
        return base::fail(corrupt("WAL record header has the wrong magic"));
    }
    if (version.value() != kWalFormatVersion) {
        return base::fail(unsupported("WAL record version is unsupported"));
    }
    if (header_size.value() != kWalRecordHeaderSize || reserved.value() != 0) {
        return base::fail(corrupt("WAL record header parameters are invalid"));
    }
    WalRecordHeader header{
        .total_length = total_length.value(),
        .payload_length = payload_length.value(),
        .previous_lsn = base::Lsn{previous_lsn.value()},
        .xid = base::Xid{xid.value()},
        .resource_manager = static_cast<WalResourceManager>(resource_manager.value()),
        .info = info.value(),
        .block_count = block_count.value(),
        .flags = flags.value(),
        .checksum = checksum.value(),
    };
    if (auto status = validate_record(header, true); !status) {
        return base::fail(std::move(status).error());
    }
    return header;
}

}  // namespace db::wal
