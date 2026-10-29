#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "db/access/btree_wal.hpp"
#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/recovery/btree_wal.hpp"

namespace db::recovery::detail {

auto btree_wal_info(access::BtreeWalOperation operation) -> base::Result<std::uint8_t>;
auto btree_wal_operation(std::uint8_t info) -> base::Result<access::BtreeWalOperation>;
auto validate_btree_payload(const BtreeWalPayload& payload, bool decoding) -> base::Status;
auto expected_btree_role(
    access::BtreeWalOperation operation,
    std::size_t block_index,
    std::size_t block_count
) -> base::Result<access::BtreeWalPageRole>;
auto expected_btree_will_initialize(
    access::BtreeWalOperation operation,
    access::BtreeWalPageRole role
) noexcept -> bool;
auto encode_btree_wal_page_change(
    access::BtreeWalOperation operation,
    access::BtreeWalPageRole role,
    base::Bytes after_page,
    std::span<const access::BtreePageChangeRange> changed_ranges
) -> base::Result<std::vector<std::byte>>;

}  // namespace db::recovery::detail
