#include "db/base/text.hpp"

static_assert(db::base::is_ascii_space('\n'));
static_assert(db::base::trim_ascii_space("  db\t") == "db");
static_assert(db::base::ascii_lower('F') == 'f');
static_assert(db::base::ascii_equal_ignoring_case("Db", "dB"));
static_assert(db::base::valid_utf8_prefix_size("database", 3U) == 3U);
static_assert(db::base::is_valid_utf8("Db"));
