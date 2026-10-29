#include "db/recovery/wal_stream.hpp"

#include <optional>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/wal/reader.hpp"

namespace db::recovery {
namespace {

auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "recovery.wal_stream",
        "validate recovery input"
    );
}

auto unavailable(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, std::move(message)}.with_context(
        "recovery.wal_stream",
        "access recovery input"
    );
}

auto validate_trusted_tail(
    const wal::WalReader& reader,
    const wal::WalStartupValidation& validation
) -> base::Status {
    if (!validation.valid()) {
        return base::fail(corrupt("WAL startup validation proof is inactive"));
    }
    if (reader.stream_start() != validation.stream_start_lsn()) {
        return base::fail(corrupt("WAL stream start changed after startup tail validation"));
    }
    if (reader.logical_end() != validation.end_lsn()) {
        return base::fail(corrupt("WAL logical end changed after startup tail validation"));
    }
    return {};
}

auto preflight_untrusted_tail(wal::WalReader& reader, const base::Lsn redo_lsn) -> base::Status {
    auto scan = reader.scan_valid_end(redo_lsn);
    if (!scan) {
        return base::fail(
            std::move(scan).error().with_context("recovery.wal_stream", "preflight WAL tail")
        );
    }
    if (scan->incomplete_tail) {
        return base::fail(corrupt(
            "recovery refuses an incomplete WAL tail; reopen through WalManager before replay"
        ));
    }
    if (scan->valid_end_lsn != reader.logical_end()) {
        return base::fail(corrupt("WAL preflight did not finish at its logical end"));
    }
    if (!scan->requested_boundary_found) {
        return base::fail(corrupt("recovery redo LSN does not name a WAL record boundary"));
    }
    return {};
}

auto replay_start(const wal::WalReader& reader, const base::Lsn configured_redo_lsn)
    -> base::Result<base::Lsn> {
    const auto stream_start = reader.stream_start();
    const auto logical_end = reader.logical_end();
    const auto replay_start =
        configured_redo_lsn == base::kInvalidLsn ? stream_start : configured_redo_lsn;
    if (replay_start.value() < stream_start.value() || replay_start.value() > logical_end.value()) {
        return base::fail(corrupt("recovery redo LSN lies outside the retained WAL stream"));
    }
    return replay_start;
}

}  // namespace

auto RecoveryWalStream::open(const RecoveryWalStreamOptions& options)
    -> base::Result<RecoveryWalStream> {
    auto reader = wal::WalReader::open(options.directory, options.reader_options);
    if (!reader) {
        return base::fail(
            std::move(reader).error().with_context("recovery.wal_stream", "open WAL")
        );
    }

    bool repaired_incomplete_tail = false;
    if (options.trusted_startup_validation.has_value()) {
        if (auto status =
                validate_trusted_tail(reader.value(), *options.trusted_startup_validation);
            !status) {
            return base::fail(std::move(status).error());
        }
        if (options.trusted_startup_validation->redo_lsn() != options.redo_lsn) {
            return base::fail(corrupt("WAL redo LSN changed after startup tail validation"));
        }
        repaired_incomplete_tail = options.trusted_startup_validation->repaired_incomplete_tail();
    } else if (auto status = preflight_untrusted_tail(reader.value(), options.redo_lsn); !status) {
        return base::fail(std::move(status).error());
    }

    auto start = replay_start(reader.value(), options.redo_lsn);
    if (!start) {
        return base::fail(std::move(start).error());
    }
    if (auto status = reader->seek(start.value()); !status) {
        return base::fail(
            std::move(status).error().with_context("recovery.wal_stream", "seek replay start")
        );
    }
    return RecoveryWalStream{
        std::move(reader).value(),
        start.value(),
        repaired_incomplete_tail,
    };
}

RecoveryWalStream::RecoveryWalStream(
    wal::WalReader reader,
    const base::Lsn replay_start_lsn,
    const bool repaired_incomplete_tail
) noexcept
    : reader_{std::move(reader)},
      replay_start_lsn_{replay_start_lsn},
      repaired_incomplete_tail_{repaired_incomplete_tail} {}

auto RecoveryWalStream::valid() const noexcept -> bool {
    return reader_.valid();
}

auto RecoveryWalStream::replay_start_lsn() const noexcept -> base::Lsn {
    return valid() ? replay_start_lsn_ : base::kInvalidLsn;
}

auto RecoveryWalStream::position() const noexcept -> base::Lsn {
    return valid() ? reader_.position() : base::kInvalidLsn;
}

auto RecoveryWalStream::logical_end_lsn() const noexcept -> base::Lsn {
    return valid() ? reader_.logical_end() : base::kInvalidLsn;
}

auto RecoveryWalStream::repaired_incomplete_tail() const noexcept -> bool {
    return valid() && repaired_incomplete_tail_;
}

auto RecoveryWalStream::read_next() -> base::Result<std::optional<wal::DecodedWalRecord>> {
    if (!valid()) {
        return base::fail(unavailable("recovery WAL stream has been moved from"));
    }
    if (terminal_error_.has_value()) {
        return base::fail(*terminal_error_);
    }
    auto decoded = reader_.read_next();
    if (!decoded) {
        auto error =
            std::move(decoded).error().with_context("recovery.wal_stream", "read recovery record");
        terminal_error_ = error;
        return base::fail(std::move(error));
    }
    return decoded;
}

}  // namespace db::recovery
