#include <type_traits>

#include "db/transaction/lock_manager.hpp"
#include "db/transaction/xid_waiter.hpp"

template <typename Type>
concept ExposesXidWaiterImplementation = requires { typename Type::Inner; };

static_assert(!std::is_copy_constructible_v<db::transaction::XidWaiter>);
static_assert(!std::is_move_constructible_v<db::transaction::XidWaiter>);
static_assert(!std::is_constructible_v<db::transaction::XidWaiter, db::transaction::LockManager&>);
static_assert(!ExposesXidWaiterImplementation<db::transaction::XidWaiter>);
