#include <type_traits>

#include "commit_log_format.hpp"

static_assert(std::is_move_constructible_v<db::transaction::CommitLogSegmentImage>);
