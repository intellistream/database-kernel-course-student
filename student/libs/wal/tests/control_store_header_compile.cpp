#include <type_traits>

#include "db/wal/control_store.hpp"

template <typename Type>
concept ExposesImplementation = requires { typename Type::Inner; };

static_assert(!std::is_copy_constructible_v<db::wal::WalControlStore>);
static_assert(std::is_nothrow_move_constructible_v<db::wal::WalControlStore>);
static_assert(std::is_nothrow_move_assignable_v<db::wal::WalControlStore>);
static_assert(db::wal::kInitialWalControlGeneration == 1);
static_assert(!ExposesImplementation<db::wal::WalControlStore>);
