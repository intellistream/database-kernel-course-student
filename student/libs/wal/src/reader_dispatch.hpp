#pragma once

#include "db/base/result.hpp"
#include "db/wal/record.hpp"

namespace db::wal::reader_detail {

auto validate_dispatch_shape(const WalRecord& record) -> base::Status;

}  // namespace db::wal::reader_detail
