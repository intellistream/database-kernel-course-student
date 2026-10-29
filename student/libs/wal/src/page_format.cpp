#include <cstdint>
#include <string>
#include <utility>

#include "db/base/bytes.hpp"
#include "db/wal/format.hpp"

#include "format_internal.hpp"

namespace db::wal {
namespace {

using format_internal::corrupt;
using format_internal::finish_write;
using format_internal::invalid;
using format_internal::unsupported;

auto validate_page(const WalPageHeader& header, const bool decoding) -> base::Status {
    const auto error = [decoding](std::string message) {
        return base::fail(decoding ? corrupt(std::move(message)) : invalid(std::move(message)));
    };
    if ((header.flags & static_cast<std::uint16_t>(~kWalPageKnownFlags)) != 0) {
        return error("unknown WAL page flags");
    }
    if (header.page_start_lsn == base::kInvalidLsn
        || header.page_start_lsn.value() % kWalPageSize != 0) {
        return error("WAL page start LSN is not page aligned");
    }
    if (header.segment_number != header.page_start_lsn.value() / kWalSegmentSize) {
        return error("WAL page segment number does not match its LSN");
    }
    return {};
}

}  // namespace

auto encode_page_header(const WalPageHeader& header) -> base::Result<WalPageHeaderBytes> {
    if (auto status = validate_page(header, false); !status) {
        return base::fail(std::move(status).error());
    }
    WalPageHeaderBytes bytes{};
    base::LittleEndianWriter writer{bytes};
    auto status = writer.write_u32(kWalPageMagic);
    if (status) {
        status = writer.write_u16(kWalFormatVersion);
    }
    if (status) {
        status = writer.write_u16(kWalPageHeaderSize);
    }
    if (status) {
        status = writer.write_u64(header.page_start_lsn.value());
    }
    if (status) {
        status = writer.write_u64(header.segment_number);
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
    if (auto finished = finish_write(status, writer, kWalPageHeaderSize); !finished) {
        return base::fail(std::move(finished).error());
    }
    return bytes;
}

auto decode_page_header(const base::Bytes bytes) -> base::Result<WalPageHeader> {
    if (bytes.size() != kWalPageHeaderSize) {
        return base::fail(corrupt("WAL page header has the wrong length"));
    }
    base::LittleEndianReader reader{bytes};
    auto magic = reader.read_u32();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto page_start = reader.read_u64();
    auto segment_number = reader.read_u64();
    auto flags = reader.read_u16();
    auto reserved = reader.read_u16();
    auto checksum = reader.read_u32();
    if (!magic || !version || !header_size || !page_start || !segment_number || !flags || !reserved
        || !checksum) {
        return base::fail(corrupt("WAL page header is truncated"));
    }
    if (magic.value() != kWalPageMagic) {
        return base::fail(corrupt("WAL page header has the wrong magic"));
    }
    if (version.value() != kWalFormatVersion) {
        return base::fail(unsupported("WAL page version is unsupported"));
    }
    if (header_size.value() != kWalPageHeaderSize || reserved.value() != 0) {
        return base::fail(corrupt("WAL page header parameters are invalid"));
    }
    WalPageHeader header{
        .page_start_lsn = base::Lsn{page_start.value()},
        .segment_number = segment_number.value(),
        .flags = flags.value(),
        .checksum = checksum.value(),
    };
    if (auto status = validate_page(header, true); !status) {
        return base::fail(std::move(status).error());
    }
    return header;
}

}  // namespace db::wal
