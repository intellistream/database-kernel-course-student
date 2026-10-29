#include <type_traits>

#include "commit_log_segment.hpp"

static_assert(!std::is_copy_constructible_v<db::transaction::CommitLogSegment>);
