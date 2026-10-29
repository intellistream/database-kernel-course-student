#include <type_traits>

#include "db/planner/metadata_port.hpp"

static_assert(std::has_virtual_destructor_v<db::planner::MetadataPort>);
static_assert(!std::is_copy_constructible_v<db::planner::MetadataPort>);
