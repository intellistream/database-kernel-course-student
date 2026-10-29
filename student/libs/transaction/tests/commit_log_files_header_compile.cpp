#include <type_traits>

#include "commit_log_files.hpp"

static_assert(std::is_default_constructible_v<db::transaction::CommitLogDiscoveredSegments>);
