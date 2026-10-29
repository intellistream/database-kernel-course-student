#include <type_traits>

#include "db/transaction/commit_log.hpp"

template <typename Type>
concept ExposesCommitLogImplementation = requires { typename Type::Inner; };

static_assert(!std::is_copy_constructible_v<db::transaction::CommitLog>);
static_assert(std::is_nothrow_move_constructible_v<db::transaction::CommitLog>);
static_assert(std::is_nothrow_move_assignable_v<db::transaction::CommitLog>);
static_assert(!ExposesCommitLogImplementation<db::transaction::CommitLog>);
