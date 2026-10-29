#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::access {

class BtreeRelation;

// The B-tree v3 metapage stores a u16 maximum key length. This is the maximum
// composite-key payload before page-local tuple metadata is added.
inline constexpr std::size_t kMaximumBtreeEncodedKeySize = 65'535U;
inline constexpr std::uint8_t kBtreeKeyNullTag = 0x00U;
inline constexpr std::uint8_t kBtreeKeyValueTag = 0x01U;

using EncodedBtreeKey = std::vector<std::byte>;

// Immutable composite-key contract for one B-tree relation. Encoded bytes do
// not contain a schema header: page metadata binds them to this layout by its
// stable type signature.
class BtreeKeyLayout final {
public:
    static auto create(std::vector<types::DataType> column_types) -> base::Result<BtreeKeyLayout>;

    BtreeKeyLayout(const BtreeKeyLayout&) = default;
    auto operator=(const BtreeKeyLayout&) -> BtreeKeyLayout& = default;
    BtreeKeyLayout(BtreeKeyLayout&& other) noexcept;
    auto operator=(BtreeKeyLayout&& other) noexcept -> BtreeKeyLayout&;
    ~BtreeKeyLayout() = default;

    auto valid() const noexcept -> bool;
    auto column_types() const noexcept -> std::span<const types::DataType>;
    auto column_count() const noexcept -> std::size_t;
    auto maximum_encoded_size() const noexcept -> std::size_t;
    auto type_signature() const noexcept -> std::uint64_t;
    // True when canonical complete keys have the same order as their bytes.
    auto bytewise_comparable() const noexcept -> bool;

    auto encode(std::span<const types::Value> values) const -> base::Result<EncodedBtreeKey>;
    auto encode(std::initializer_list<types::Value> values) const -> base::Result<EncodedBtreeKey>;
    auto encode_into(std::span<const types::Value> values, EncodedBtreeKey& output) const
        -> base::Status;

    // Encodes a scan prefix: at least one value and at most column_count
    // values, in column order. A partial prefix supports range scans whose
    // lower bound covers only leading key columns (reference serialize_key).
    auto encode_prefix(std::span<const types::Value> values) const -> base::Result<EncodedBtreeKey>;

    auto encode_projected(
        std::span<const types::Value> source_values,
        std::span<const std::size_t> positions
    ) const -> base::Result<EncodedBtreeKey>;
    auto encode_projected_into(
        std::span<const types::Value> source_values,
        std::span<const std::size_t> positions,
        EncodedBtreeKey& output
    ) const -> base::Status;
    // LOAD/COPY fast path for values already canonicalized by
    // PreparedTypeInput or apply_type_modifier. Structural/type bounds remain
    // checked, but CHAR UTF-8 is not rescanned here.
    auto encode_projected_canonical_into(
        std::span<const types::Value> source_values,
        std::span<const std::size_t> positions,
        EncodedBtreeKey& output
    ) const -> base::Status;

    // Full-key operations reject malformed tags, truncated/corrupt payloads,
    // noncanonical FLOAT NaNs and CHAR values, and trailing bytes.
    auto validate(base::Bytes encoded) const -> base::Status;
    auto contains_null(base::Bytes encoded) const -> base::Result<bool>;
    auto compare(base::Bytes left, base::Bytes right) const -> base::Result<std::strong_ordering>;

    // Compares only the first requested columns. Both inputs must contain
    // those components, but may contain a suffix; this supports future range
    // and prefix scans without allowing the full-key API to accept trailing
    // bytes.
    auto compare_prefix(base::Bytes left, base::Bytes right, std::size_t columns) const
        -> base::Result<std::strong_ordering>;

private:
    BtreeKeyLayout() = default;
    BtreeKeyLayout(
        std::vector<types::DataType> column_types,
        std::vector<std::size_t> fixed_payload_widths,
        std::size_t maximum_encoded_size,
        std::uint64_t type_signature
    );

    friend class BtreeRelation;

    std::vector<types::DataType> column_types_;
    std::vector<std::size_t> fixed_payload_widths_;
    std::size_t maximum_encoded_size_{0};
    std::uint64_t type_signature_{0};
    bool fixed_width_only_{false};
    bool bytewise_comparable_{false};
};

}  // namespace db::access
