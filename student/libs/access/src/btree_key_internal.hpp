#pragma once
#include "db/base/lab_missing.hpp"

#include <compare>
#include <cstddef>
#include <span>

#include "db/access/btree_key.hpp"
#include "db/base/bytes.hpp"

namespace db::access::detail {

// Scan bounds are logical comparison values rather than stored index tuples.
// They must share each layout column's base TypeId, but may retain a different
// typmod so CHAR and TIMESTAMP comparisons do not truncate or round a bound.
// Stored-key encoders continue to require the complete DataType.
auto encode_btree_search_prefix(const BtreeKeyLayout& layout, std::span<const types::Value> values)
    -> base::Result<EncodedBtreeKey>;

// Internal comparator for canonical bytes produced by BtreeKeyLayout encoders
// or access-owned page mutation. Public or otherwise untrusted bytes must use
// the checked BtreeKeyLayout API. Resident-page callers trust the encoded-key
// contract and do not repeat component-envelope checks.
auto compare_encoded_btree_keys(
    const BtreeKeyLayout& layout,
    base::Bytes left,
    base::Bytes right
) noexcept -> std::strong_ordering;

// Canonical bytes produced by BtreeKeyLayout already carry exactly one
// component for every layout column. Internal uniqueness paths inspect only
// the NULL tags and do not reopen the checked public decoder.
auto encoded_btree_key_contains_null(const BtreeKeyLayout& layout, base::Bytes encoded) noexcept
    -> bool;

auto compare_encoded_btree_key_prefix(
    const BtreeKeyLayout& layout,
    base::Bytes left,
    base::Bytes right,
    std::size_t columns
) noexcept -> std::strong_ordering;

}  // namespace db::access::detail
