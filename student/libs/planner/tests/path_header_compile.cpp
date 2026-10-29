#include <type_traits>
#include <utility>

#include "db/planner/path.hpp"

static_assert(std::is_same_v<
              decltype(db::planner::base_relation_path_kind(
                  std::declval<const db::planner::BaseRelationPath&>()
              )),
              db::planner::BaseRelationPathKind>);
static_assert(!std::is_copy_constructible_v<db::planner::MetadataPort>);
static_assert(std::is_copy_constructible_v<db::planner::BaseRelationPathList>);
