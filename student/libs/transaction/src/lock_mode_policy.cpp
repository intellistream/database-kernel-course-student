#include "lock_mode_policy.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include "db/base/lab_missing.hpp"

namespace db::transaction {
namespace detail {
namespace {

inline constexpr std::array<std::uint8_t, 8> kTableConflictMasks{
    0b1000'0000U,
    0b1100'0000U,
    0b1111'0000U,
    0b1111'1000U,
    0b1110'1100U,
    0b1111'1100U,
    0b1111'1110U,
    0b1111'1111U,
};

inline constexpr std::array<std::uint8_t, 4> kRowConflictMasks{
    0b1000U,
    0b1100U,
    0b1110U,
    0b1111U,
};

inline constexpr std::array<std::uint8_t, 2> kTransactionConflictMasks{
    0b10U,
    0b11U,
};

[[maybe_unused]] auto
conflict_mask([[maybe_unused]] LockTargetKind kind, [[maybe_unused]] std::uint8_t mode) noexcept
    -> std::uint8_t {
    // LAB-S08.Q04:R01-BEGIN
    base::lab::missing_algorithm("S08.Q04:R01");
    // LAB-S08.Q04:R01-END
}

[[maybe_unused]] auto
valid_mode_number([[maybe_unused]] LockTargetKind kind, [[maybe_unused]] std::uint8_t mode) noexcept
    -> bool {
    // LAB-S08.Q04:R02-BEGIN
    base::lab::missing_algorithm("S08.Q04:R02");
    // LAB-S08.Q04:R02-END
}

}  // namespace

[[maybe_unused]] auto valid_table_mode([[maybe_unused]] TableLockMode mode) noexcept -> bool {
    // LAB-S08.Q04:R03-BEGIN
    base::lab::missing_algorithm("S08.Q04:R03");
    // LAB-S08.Q04:R03-END
}

[[maybe_unused]] auto valid_row_mode([[maybe_unused]] RowLockMode mode) noexcept -> bool {
    // LAB-S08.Q04:R04-BEGIN
    base::lab::missing_algorithm("S08.Q04:R04");
    // LAB-S08.Q04:R04-END
}

[[maybe_unused]] auto lock_modes_compatible(
    [[maybe_unused]] LockTargetKind kind,
    [[maybe_unused]] std::uint8_t left,
    [[maybe_unused]] std::uint8_t right
) noexcept -> bool {
    // LAB-S08.Q04:R05-BEGIN
    base::lab::missing_algorithm("S08.Q04:R05");
    // LAB-S08.Q04:R05-END
}

[[maybe_unused]] auto lock_mode_subsumes(
    [[maybe_unused]] LockTargetKind kind,
    [[maybe_unused]] std::uint8_t stronger,
    [[maybe_unused]] std::uint8_t weaker
) noexcept -> bool {
    // LAB-S08.Q04:R06-BEGIN
    base::lab::missing_algorithm("S08.Q04:R06");
    // LAB-S08.Q04:R06-END
}

}  // namespace detail

auto table_lock_mode_name(TableLockMode mode) noexcept -> std::string_view {
    switch (mode) {
        case TableLockMode::kAccessShare:
            return "access_share";
        case TableLockMode::kRowShare:
            return "row_share";
        case TableLockMode::kRowExclusive:
            return "row_exclusive";
        case TableLockMode::kShareUpdateExclusive:
            return "share_update_exclusive";
        case TableLockMode::kShare:
            return "share";
        case TableLockMode::kShareRowExclusive:
            return "share_row_exclusive";
        case TableLockMode::kExclusive:
            return "exclusive";
        case TableLockMode::kAccessExclusive:
            return "access_exclusive";
    }
    return "unknown";
}

auto row_lock_mode_name(RowLockMode mode) noexcept -> std::string_view {
    switch (mode) {
        case RowLockMode::kKeyShare:
            return "key_share";
        case RowLockMode::kShare:
            return "share";
        case RowLockMode::kNoKeyExclusive:
            return "no_key_exclusive";
        case RowLockMode::kExclusive:
            return "exclusive";
    }
    return "unknown";
}

[[maybe_unused]] auto table_lock_modes_compatible(
    [[maybe_unused]] TableLockMode left,
    [[maybe_unused]] TableLockMode right
) noexcept -> bool {
    // LAB-S08.Q04:R07-BEGIN
    base::lab::missing_algorithm("S08.Q04:R07");
    // LAB-S08.Q04:R07-END
}

[[maybe_unused]] auto row_lock_modes_compatible(
    [[maybe_unused]] RowLockMode left,
    [[maybe_unused]] RowLockMode right
) noexcept -> bool {
    // LAB-S08.Q04:R08-BEGIN
    base::lab::missing_algorithm("S08.Q04:R08");
    // LAB-S08.Q04:R08-END
}

[[maybe_unused]] auto table_lock_mode_subsumes(
    [[maybe_unused]] TableLockMode stronger,
    [[maybe_unused]] TableLockMode weaker
) noexcept -> bool {
    // LAB-S08.Q04:R09-BEGIN
    base::lab::missing_algorithm("S08.Q04:R09");
    // LAB-S08.Q04:R09-END
}

[[maybe_unused]] auto row_lock_mode_subsumes(
    [[maybe_unused]] RowLockMode stronger,
    [[maybe_unused]] RowLockMode weaker
) noexcept -> bool {
    // LAB-S08.Q04:R10-BEGIN
    base::lab::missing_algorithm("S08.Q04:R10");
    // LAB-S08.Q04:R10-END
}

}  // namespace db::transaction
