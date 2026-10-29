#include <type_traits>

#include "db/wal/writer.hpp"

namespace {

template <typename Type>
concept ExposesImplementation = requires { typename Type::Inner; };

static_assert(!std::is_copy_constructible_v<db::wal::WalWriter>);
static_assert(std::is_nothrow_move_constructible_v<db::wal::WalWriter>);
static_assert(std::is_nothrow_move_assignable_v<db::wal::WalWriter>);
static_assert(!ExposesImplementation<db::wal::WalWriter>);

}  // namespace
