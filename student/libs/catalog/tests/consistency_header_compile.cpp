#include <type_traits>

#include "db/catalog/consistency.hpp"

namespace db::catalog::test {

static_assert(std::is_abstract_v<IndexRelationValidationPort>);

}  // namespace db::catalog::test
