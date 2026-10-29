#include <cstddef>
#include <type_traits>

#include "reader_read_ahead.hpp"

static_assert(std::is_constructible_v<db::wal::reader_detail::ReadAhead, std::size_t>);
