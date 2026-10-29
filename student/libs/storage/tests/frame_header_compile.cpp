#include <type_traits>

#include "db/storage/frame.hpp"

namespace {

template <typename Type>
concept ExposesPinLease = requires { typename Type::PinLease; };

static_assert(!std::is_copy_constructible_v<db::storage::Frame>);
static_assert(!std::is_move_constructible_v<db::storage::Frame>);
static_assert(!ExposesPinLease<db::storage::Frame>);
static_assert(!std::is_copy_constructible_v<db::storage::PinGuard>);
static_assert(std::is_nothrow_move_constructible_v<db::storage::PinGuard>);
static_assert(!std::is_copy_constructible_v<db::storage::PageReadGuard>);
static_assert(std::is_nothrow_move_constructible_v<db::storage::PageReadGuard>);
static_assert(std::is_nothrow_move_assignable_v<db::storage::PageReadGuard>);
static_assert(!std::is_copy_constructible_v<db::storage::PageWriteGuard>);
static_assert(std::is_nothrow_move_constructible_v<db::storage::PageWriteGuard>);

}  // namespace
