#include "db/recovery/heap_startup_redo.hpp"

static_assert(
    db::recovery::StartupPageLoadMode::kReadExisting
    != db::recovery::StartupPageLoadMode::kSkipExisting
);
