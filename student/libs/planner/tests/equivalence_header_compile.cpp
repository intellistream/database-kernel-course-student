#include <type_traits>
#include <utility>

#include "db/planner/equivalence.hpp"

static_assert(std::is_copy_constructible_v<db::planner::EquivalenceEquality>);
static_assert(std::is_copy_constructible_v<db::planner::InnerDomainEquivalenceResult>);
static_assert(std::is_same_v<
              decltype(db::planner::equivalence_member_data_type(
                  std::declval<const db::planner::EquivalenceMember&>()
              )),
              db::types::DataType>);
