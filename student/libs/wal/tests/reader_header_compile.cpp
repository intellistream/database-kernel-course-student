#include <type_traits>

#include "db/wal/reader.hpp"

template <typename Type>
concept ExposesImplementation = requires { typename Type::Inner; };

static_assert(db::wal::kWalReadAheadSize >= db::wal::kWalPageSize);
static_assert(std::is_nothrow_move_constructible_v<db::wal::WalReader>);
static_assert(std::is_nothrow_move_assignable_v<db::wal::WalReader>);
static_assert(!ExposesImplementation<db::wal::WalReader>);
