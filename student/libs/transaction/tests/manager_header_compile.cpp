#include <type_traits>
#include <utility>

#include "db/transaction/manager.hpp"

template <typename Type>
concept ExposesTransactionImplementation = requires { typename Type::Inner; };

static_assert(!std::is_copy_constructible_v<db::transaction::TransactionManager>);
static_assert(!std::is_move_constructible_v<db::transaction::TransactionManager>);
static_assert(!ExposesTransactionImplementation<db::transaction::TransactionManager>);
static_assert(std::is_same_v<
              decltype(std::declval<db::transaction::TransactionManager&>().commit_log()),
              const db::transaction::CommitLog&>);
static_assert(std::is_same_v<
              decltype(std::declval<db::transaction::TransactionManager&>().proc_array()),
              const db::transaction::ProcArray&>);
static_assert(!std::is_default_constructible_v<db::transaction::TransactionStartupState>);
static_assert(!std::is_copy_constructible_v<db::transaction::TransactionStartupState>);
static_assert(std::is_nothrow_move_constructible_v<db::transaction::TransactionStartupState>);
static_assert(std::is_nothrow_move_assignable_v<db::transaction::TransactionStartupState>);
static_assert(!std::is_copy_constructible_v<db::transaction::CheckpointCompletionBarrier>);
static_assert(std::is_nothrow_move_constructible_v<db::transaction::CheckpointCompletionBarrier>);
static_assert(!ExposesTransactionImplementation<db::transaction::CheckpointCompletionBarrier>);
