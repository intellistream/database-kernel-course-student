#include <type_traits>

#include "db/test_support/deterministic_random.hpp"
#include "db/test_support/fault_injector.hpp"
#include "db/test_support/manual_clock.hpp"
#include "db/test_support/sql_regress.hpp"
#include "db/test_support/test_directory.hpp"

static_assert(!std::is_copy_constructible_v<db::test_support::TestDirectory>);
