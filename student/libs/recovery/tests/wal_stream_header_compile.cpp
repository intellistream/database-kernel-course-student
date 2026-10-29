#include <type_traits>

#include "db/recovery/wal_stream.hpp"

static_assert(std::is_move_constructible_v<db::recovery::RecoveryWalStream>);
static_assert(std::is_move_assignable_v<db::recovery::RecoveryWalStream>);
static_assert(std::is_nothrow_move_constructible_v<db::recovery::RecoveryWalStream>);
static_assert(std::is_nothrow_move_assignable_v<db::recovery::RecoveryWalStream>);
static_assert(!std::is_copy_constructible_v<db::recovery::RecoveryWalStream>);
static_assert(!std::is_default_constructible_v<db::wal::WalStartupValidation>);
static_assert(std::is_copy_constructible_v<db::wal::WalStartupValidation>);
