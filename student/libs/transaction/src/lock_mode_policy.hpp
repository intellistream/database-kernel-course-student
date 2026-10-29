#pragma once

#include <cstdint>

#include "db/transaction/lock_manager.hpp"

namespace db::transaction::detail {

enum class LockTargetKind : std::uint8_t {
    kTable,
    kRow,
    kTransaction,
};

auto valid_table_mode(TableLockMode mode) noexcept -> bool;
auto valid_row_mode(RowLockMode mode) noexcept -> bool;
auto lock_modes_compatible(LockTargetKind kind, std::uint8_t left, std::uint8_t right) noexcept
    -> bool;
auto lock_mode_subsumes(LockTargetKind kind, std::uint8_t stronger, std::uint8_t weaker) noexcept
    -> bool;

}  // namespace db::transaction::detail
