#include <type_traits>

#include "db/wal/ring_buffer.hpp"

template <typename Type>
concept ExposesImplementation = requires { typename Type::Inner; };

static_assert(!std::is_copy_constructible_v<db::wal::WalRingBuffer>);
static_assert(std::is_nothrow_move_constructible_v<db::wal::WalRingBuffer>);
static_assert(std::is_nothrow_move_assignable_v<db::wal::WalRingBuffer>);
static_assert(!ExposesImplementation<db::wal::WalRingBuffer>);
static_assert(std::variant_size_v<db::wal::WalReserveOutcome> == 2);
static_assert(db::wal::kInitialWalRingLsn.value() == db::wal::kWalPageSize);
