#include <concepts>
#include <functional>

#include "db/types/value.hpp"

static_assert(std::same_as<
              decltype(db::types::sql_equal(
                  std::declval<const db::types::Value&>(),
                  std::declval<const db::types::Value&>()
              )),
              db::base::Result<db::types::SqlTruth>>);
static_assert(std::regular_invocable<std::hash<db::types::Value>, const db::types::Value&>);
