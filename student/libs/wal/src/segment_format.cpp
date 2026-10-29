#include <cstdint>
#include <limits>
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

auto validate_segment(const WalSegmentHeader& header, const bool decoding) -> base::Status {
    const auto error = [decoding](std::string message) {
        return base::fail(decoding ? corrupt(std::move(message)) : invalid(std::move(message)));
    };
    if (header.system_identifier == 0) {
        return error("WAL segment requires a non-zero system identifier");
    }
    if (header.timeline == 0) {
        return error("WAL segment requires a non-zero timeline");
    }
    if (header.segment_number > std::numeric_limits<std::uint64_t>::max() / kWalSegmentSize
        || header.first_lsn.value() != header.segment_number * kWalSegmentSize) {
        return error("WAL segment first LSN does not match its number");
    }
    return {};
}

}  // namespace

auto encode_segment_header(const WalSegmentHeader& header) -> base::Result<WalSegmentHeaderBytes> {
    if (auto status = validate_segment(header, false); !status) {
        return base::fail(std::move(status).error());
    }
    WalSegmentHeaderBytes bytes{};
    base::LittleEndianWriter writer{bytes};
    auto status = writer.write_u64(kWalSegmentMagic);
    if (status) {
        status = writer.write_u16(kWalFormatVersion);
    }
    if (status) {
        status = writer.write_u16(kWalSegmentHeaderSize);
    }
    if (status) {
        status = writer.write_u32(kWalPageSize);
    }
    if (status) {
        status = writer.write_u64(kWalSegmentSize);
    }
    if (status) {
        status = writer.write_u64(header.system_identifier);
    }
    if (status) {
        status = writer.write_u64(header.segment_number);
    }
    if (status) {
        status = writer.write_u64(header.first_lsn.value());
    }
    if (status) {
        status = writer.write_u32(header.timeline);
    }
    if (status) {
        status = writer.write_u32(header.checksum);
    }
    if (status) {
        status = writer.write_u64(0);
    }
    if (auto finished = finish_write(status, writer, kWalSegmentHeaderSize); !finished) {
        return base::fail(std::move(finished).error());
    }
    return bytes;
}

auto decode_segment_header(const base::Bytes bytes) -> base::Result<WalSegmentHeader> {
    if (bytes.size() != kWalSegmentHeaderSize) {
        return base::fail(corrupt("WAL segment header has the wrong length"));
    }
    base::LittleEndianReader reader{bytes};
    auto magic = reader.read_u64();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto page_size = reader.read_u32();
    auto segment_size = reader.read_u64();
    auto system_identifier = reader.read_u64();
    auto segment_number = reader.read_u64();
    auto first_lsn = reader.read_u64();
    auto timeline = reader.read_u32();
    auto checksum = reader.read_u32();
    auto reserved = reader.read_u64();
    if (!magic || !version || !header_size || !page_size || !segment_size || !system_identifier
        || !segment_number || !first_lsn || !timeline || !checksum || !reserved) {
        return base::fail(corrupt("WAL segment header is truncated"));
    }
    if (magic.value() != kWalSegmentMagic) {
        return base::fail(corrupt("WAL segment header has the wrong magic"));
    }
    if (version.value() != kWalFormatVersion) {
        return base::fail(unsupported("WAL segment version is unsupported"));
    }
    if (header_size.value() != kWalSegmentHeaderSize || page_size.value() != kWalPageSize
        || segment_size.value() != kWalSegmentSize || reserved.value() != 0) {
        return base::fail(corrupt("WAL segment header parameters are invalid"));
    }
    WalSegmentHeader header{
        .system_identifier = system_identifier.value(),
        .segment_number = segment_number.value(),
        .first_lsn = base::Lsn{first_lsn.value()},
        .timeline = timeline.value(),
        .checksum = checksum.value(),
    };
    if (auto status = validate_segment(header, true); !status) {
        return base::fail(std::move(status).error());
    }
    return header;
}

}  // namespace db::wal
