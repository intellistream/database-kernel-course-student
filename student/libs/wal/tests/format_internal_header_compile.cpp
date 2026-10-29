#include "format_internal.hpp"

static_assert(noexcept(db::wal::format_internal::all_zero(db::base::Bytes{})));
