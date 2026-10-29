#include "db/transaction/lock_manager.hpp"

namespace {

template <typename Type>
concept ExposesLockManagerImplementation = requires { typename Type::Inner; };

template <typename Type>
concept ExposesTransactionTagRegistration =
    requires(Type& manager, db::base::Xid xid) { manager.register_transaction(xid); };

template <typename Type>
concept ExposesTransactionTagWait =
    requires(Type& manager, db::base::Xid waiter, db::base::Xid blocker) {
        manager.wait_for_transaction(waiter, blocker);
    };

template <typename Type>
concept ExposesTransactionTagCompletion =
    requires(Type& manager, db::base::Xid xid) { manager.finish_transaction(xid); };

static_assert(sizeof(db::transaction::TableLockMode) == sizeof(std::uint8_t));
static_assert(sizeof(db::transaction::RowLockMode) == sizeof(std::uint8_t));
static_assert(!ExposesLockManagerImplementation<db::transaction::LockManager>);
static_assert(!ExposesTransactionTagRegistration<db::transaction::LockManager>);
static_assert(!ExposesTransactionTagWait<db::transaction::LockManager>);
static_assert(!ExposesTransactionTagCompletion<db::transaction::LockManager>);

}  // namespace
