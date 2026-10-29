#include <cstdint>
#include <string>
#include <utility>

#include "db/base/bytes.hpp"
#include "db/wal/format.hpp"

#include "format_internal.hpp"

namespace db::wal {
namespace {

using format_internal::all_zero;
using format_internal::corrupt;
using format_internal::finish_write;
using format_internal::invalid;
using format_internal::unsupported;

auto valid_database_state(const DatabaseState state) noexcept -> bool {
    return state >= DatabaseState::kShutdown && state <= DatabaseState::kShuttingDown;
}

auto validate_control(const WalControlImage& control, const bool decoding) -> base::Status {
    const auto error = [decoding](std::string message) {
        return base::fail(decoding ? corrupt(std::move(message)) : invalid(std::move(message)));
    };
    if (control.system_identifier == 0) {
        return error("control image requires a non-zero system identifier");
    }
    if (!valid_database_state(control.state)) {
        return error("control image has an unknown database state");
    }
    if (control.next_xid == base::kInvalidXid || control.next_object_id == 0) {
        return error("control image has an invalid allocation frontier");
    }
    if (control.checkpoint_lsn == base::kInvalidLsn && control.redo_lsn != base::kInvalidLsn) {
        return error("control image has redo without a checkpoint");
    }
    if (control.checkpoint_lsn != base::kInvalidLsn
        && control.redo_lsn.value() > control.checkpoint_lsn.value()) {
        return error("control image redo LSN follows its checkpoint");
    }
    return {};
}

}  // namespace

auto encode_control_image(const WalControlImage& control) -> base::Result<WalControlFileBytes> {
    if (auto status = validate_control(control, false); !status) {
        return base::fail(std::move(status).error());
    }
    WalControlFileBytes bytes{};
    base::LittleEndianWriter writer{bytes};
    auto status = writer.write_u64(kWalControlMagic);
    if (status) {
        status = writer.write_u16(kWalFormatVersion);
    }
    if (status) {
        status = writer.write_u16(kWalControlHeaderSize);
    }
    if (status) {
        status = writer.write_u32(kWalControlFileSize);
    }
    if (status) {
        status = writer.write_u64(control.system_identifier);
    }
    if (status) {
        status = writer.write_u64(control.generation);
    }
    if (status) {
        status = writer.write_u64(control.checkpoint_lsn.value());
    }
    if (status) {
        status = writer.write_u64(control.redo_lsn.value());
    }
    if (status) {
        status = writer.write_u32(control.next_xid.value());
    }
    if (status) {
        status = writer.write_u32(control.latest_completed_xid.value());
    }
    if (status) {
        status = writer.write_u32(control.next_object_id);
    }
    if (status) {
        status = writer.write_u8(static_cast<std::uint8_t>(control.state));
    }
    if (status) {
        status = writer.write_u8(0);
    }
    if (status) {
        status = writer.write_u16(0);
    }
    if (status) {
        status = writer.write_u32(control.checksum);
    }
    if (status) {
        status = writer.write_u32(0);
    }
    if (auto finished = finish_write(status, writer, kWalControlHeaderSize); !finished) {
        return base::fail(std::move(finished).error());
    }
    return bytes;
}

auto decode_control_image(const base::Bytes bytes) -> base::Result<WalControlImage> {
    if (bytes.size() != kWalControlFileSize) {
        return base::fail(corrupt("WAL control image has the wrong length"));
    }
    base::LittleEndianReader reader{bytes.first(kWalControlHeaderSize)};
    auto magic = reader.read_u64();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto file_size = reader.read_u32();
    auto system_identifier = reader.read_u64();
    auto generation = reader.read_u64();
    auto checkpoint_lsn = reader.read_u64();
    auto redo_lsn = reader.read_u64();
    auto next_xid = reader.read_u32();
    auto latest_completed_xid = reader.read_u32();
    auto next_object_id = reader.read_u32();
    auto state = reader.read_u8();
    auto reserved_byte = reader.read_u8();
    auto reserved_short = reader.read_u16();
    auto checksum = reader.read_u32();
    auto reserved_word = reader.read_u32();
    if (!magic || !version || !header_size || !file_size || !system_identifier || !generation
        || !checkpoint_lsn || !redo_lsn || !next_xid || !latest_completed_xid || !next_object_id
        || !state || !reserved_byte || !reserved_short || !checksum || !reserved_word) {
        return base::fail(corrupt("WAL control header is truncated"));
    }
    if (magic.value() != kWalControlMagic) {
        return base::fail(corrupt("WAL control image has the wrong magic"));
    }
    if (version.value() != kWalFormatVersion) {
        return base::fail(unsupported("WAL control version is unsupported"));
    }
    if (header_size.value() != kWalControlHeaderSize || file_size.value() != kWalControlFileSize
        || reserved_byte.value() != 0 || reserved_short.value() != 0 || reserved_word.value() != 0
        || !all_zero(bytes.subspan(kWalControlHeaderSize))) {
        return base::fail(corrupt("WAL control image parameters or reserved bytes are invalid"));
    }
    WalControlImage control{
        .system_identifier = system_identifier.value(),
        .generation = generation.value(),
        .checkpoint_lsn = base::Lsn{checkpoint_lsn.value()},
        .redo_lsn = base::Lsn{redo_lsn.value()},
        .next_xid = base::Xid{next_xid.value()},
        .latest_completed_xid = base::Xid{latest_completed_xid.value()},
        .next_object_id = next_object_id.value(),
        .state = static_cast<DatabaseState>(state.value()),
        .checksum = checksum.value(),
    };
    if (auto status = validate_control(control, true); !status) {
        return base::fail(std::move(status).error());
    }
    return control;
}

}  // namespace db::wal
