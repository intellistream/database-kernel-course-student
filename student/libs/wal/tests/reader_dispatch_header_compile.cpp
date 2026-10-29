#include <type_traits>

#include "reader_dispatch.hpp"

static_assert(std::is_same_v<
              decltype(&db::wal::reader_detail::validate_dispatch_shape),
              db::base::Status (*)(const db::wal::WalRecord&)>);
