#include <type_traits>

#include "db/wal/segment_storage.hpp"

namespace {

template <typename Type>
concept ExposesImplementation = requires { typename Type::Inner; };

static_assert(!std::is_copy_constructible_v<db::wal::WalSegmentStorage>);
static_assert(std::is_nothrow_move_constructible_v<db::wal::WalSegmentStorage>);
static_assert(std::is_nothrow_move_assignable_v<db::wal::WalSegmentStorage>);
static_assert(!ExposesImplementation<db::wal::WalSegmentStorage>);

}  // namespace
