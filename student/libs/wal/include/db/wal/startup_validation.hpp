#pragma once

#include "db/base/identifier.hpp"

namespace db::wal {

class WalManager;

// Immutable proof produced only after WalManager::open has either accepted a
// complete physical WAL tail or durably removed its physically incomplete
// suffix. A recovery reader compares a fresh snapshot with this proof before
// it replays any record; it must not use the proof to truncate WAL itself.
class WalStartupValidation final {
public:
    // False only for the private sentinel returned by a moved-from manager.
    // Recovery rejects that sentinel before comparing any LSN boundary.
    auto valid() const noexcept -> bool {
        return valid_;
    }

    auto stream_start_lsn() const noexcept -> base::Lsn {
        return stream_start_lsn_;
    }

    auto end_lsn() const noexcept -> base::Lsn {
        return end_lsn_;
    }

    auto redo_lsn() const noexcept -> base::Lsn {
        return redo_lsn_;
    }

    auto repaired_incomplete_tail() const noexcept -> bool {
        return repaired_incomplete_tail_;
    }

    auto operator==(const WalStartupValidation&) const noexcept -> bool = default;

private:
    friend class WalManager;

    struct ValidatedState final {
        base::Lsn stream_start_lsn{base::kInvalidLsn};
        base::Lsn end_lsn{base::kInvalidLsn};
        base::Lsn redo_lsn{base::kInvalidLsn};
        bool repaired_incomplete_tail{false};
    };

    WalStartupValidation() noexcept = default;

    explicit WalStartupValidation(const ValidatedState state) noexcept
        : stream_start_lsn_{state.stream_start_lsn},
          end_lsn_{state.end_lsn},
          redo_lsn_{state.redo_lsn},
          repaired_incomplete_tail_{state.repaired_incomplete_tail},
          valid_{true} {}

    base::Lsn stream_start_lsn_{base::kInvalidLsn};
    base::Lsn end_lsn_{base::kInvalidLsn};
    base::Lsn redo_lsn_{base::kInvalidLsn};
    bool repaired_incomplete_tail_{false};
    bool valid_{false};
};

}  // namespace db::wal
