#pragma once

#include <cstdint>

#include "db/access/heap_wal.hpp"
#include "db/base/result.hpp"
#include "db/recovery/heap_wal.hpp"

namespace db::recovery::detail {

auto heap_wal_info(access::HeapWalOperation operation) -> base::Result<std::uint8_t>;
auto heap_wal_operation(std::uint8_t info) -> base::Result<access::HeapWalOperation>;
auto validate_heap_wal_payload(const HeapWalPayload& payload, bool decoding) -> base::Status;

}  // namespace db::recovery::detail
