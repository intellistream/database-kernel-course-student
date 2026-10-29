#pragma once

#include <filesystem>
#include <optional>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/startup_validation.hpp"

namespace db::recovery {

// A recovery pass starts at the persisted redo point. A missing redo point
// means the retained stream's first record, matching the initial control
// state before any checkpoint has established a narrower redo horizon.
struct RecoveryWalStreamOptions final {
    std::filesystem::path directory;
    wal::WalReaderOptions reader_options;
    base::Lsn redo_lsn{base::kInvalidLsn};

    // Normal startup receives this immutable proof from WalManager::open.
    // Without it, open() performs a read-only whole-stream preflight and
    // rejects a physically incomplete tail rather than truncating WAL.
    std::optional<wal::WalStartupValidation> trusted_startup_validation;
};

// Single-threaded, move-only recovery input stream. It owns no WAL writer,
// page cache, transaction state, catalog, or engine service. Physical tail
// repair belongs exclusively to WalManager::open; this stream only proves that
// the immutable reader snapshot still matches that repair and then yields
// decoded records from a verified redo boundary.
class RecoveryWalStream final {
public:
    static auto open(const RecoveryWalStreamOptions& options) -> base::Result<RecoveryWalStream>;

    ~RecoveryWalStream() = default;
    RecoveryWalStream(const RecoveryWalStream&) = delete;
    auto operator=(const RecoveryWalStream&) -> RecoveryWalStream& = delete;
    RecoveryWalStream(RecoveryWalStream&&) noexcept = default;
    auto operator=(RecoveryWalStream&&) noexcept -> RecoveryWalStream& = default;

    // A moved-from stream is inert. Observers return invalid/false sentinels
    // and read_next() returns kUnavailable; no stale redo boundary remains
    // observable as an active stream.
    auto valid() const noexcept -> bool;
    auto replay_start_lsn() const noexcept -> base::Lsn;
    auto position() const noexcept -> base::Lsn;
    auto logical_end_lsn() const noexcept -> base::Lsn;
    auto repaired_incomplete_tail() const noexcept -> bool;

    // A physical decode failure is terminal for this stream. The WAL bytes are
    // never modified here; callers must stop recovery and retain the durable
    // control state for a later authoritative reopen.
    auto read_next() -> base::Result<std::optional<wal::DecodedWalRecord>>;

private:
    RecoveryWalStream(
        wal::WalReader reader,
        base::Lsn replay_start_lsn,
        bool repaired_incomplete_tail
    ) noexcept;

    wal::WalReader reader_;
    base::Lsn replay_start_lsn_;
    bool repaired_incomplete_tail_;
    std::optional<base::Error> terminal_error_;
};

}  // namespace db::recovery
