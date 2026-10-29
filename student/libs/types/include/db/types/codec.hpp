#pragma once

#include <compare>
#include <cstddef>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::types {

struct HeapDecodedValue final {
    Value value;
    std::size_t bytes_consumed{0};
};

// Binds a validated fixed-width heap codec once for repeated row encoding.
// encode_into still rejects NULL, mismatched/forged Value payloads, noncanonical
// CHAR, and incorrectly sized output; only DataType/width registry work is cached.
class PreparedHeapEncoder final {
public:
    static auto create(DataType data_type) -> base::Result<PreparedHeapEncoder>;

    auto encode_into(const Value& value, base::MutableBytes output) const -> base::Status;
    // Encodes a value already produced by PreparedTypeInput or
    // apply_type_modifier. It retains shape, type, width, and payload bounds
    // checks but does not rescan a canonical CHAR payload as UTF-8.
    auto encode_canonical_into(const Value& value, base::MutableBytes output) const -> base::Status;
    auto data_type() const noexcept -> DataType;
    auto width() const noexcept -> std::size_t;

private:
    PreparedHeapEncoder(DataType data_type, std::size_t width) noexcept
        : data_type_{data_type},
          width_{width} {}

    DataType data_type_;
    std::size_t width_;
};

// Binds a validated fixed-width heap decoder once for repeated row decoding.
// decode_exact() requires exactly width() bytes, retains scalar and CHAR
// canonicality checks, and performs no type-registry or storage-width lookup.
class PreparedHeapDecoder final {
public:
    static auto create(DataType data_type) -> base::Result<PreparedHeapDecoder>;

    auto decode_exact(base::Bytes input) const -> base::Result<Value>;
    auto data_type() const noexcept -> DataType;
    auto width() const noexcept -> std::size_t;

private:
    using DecodeFunction = base::Result<Value> (*)(base::Bytes, DataType);

    PreparedHeapDecoder(DataType data_type, std::size_t width, DecodeFunction decode) noexcept
        : data_type_{data_type},
          width_{width},
          decode_{decode} {}

    DataType data_type_;
    std::size_t width_;
    DecodeFunction decode_;
};

// Scalar codecs never encode NULL markers. Heap tuples and composite B+Tree
// keys own NULL presence and ordering at their respective layer.
auto heap_encode(const Value& value, DataType data_type) -> base::Result<std::vector<std::byte>>;

// Writes one canonical non-NULL scalar heap payload into an exactly
// storage_width(data_type)-sized caller-owned buffer. This avoids a transient
// allocation for every field while encoding a batch of heap tuples.
auto heap_encode_into(const Value& value, DataType data_type, base::MutableBytes output)
    -> base::Status;

auto heap_decode(base::Bytes input, DataType data_type) -> base::Result<HeapDecodedValue>;

auto key_encode(const Value& value) -> base::Result<std::vector<std::byte>>;
// Appends one canonical non-NULL key payload to caller-owned storage. It avoids
// a transient allocation for every scalar component of a composite B+Tree key.
auto key_encode_into(const Value& value, std::vector<std::byte>& output) -> base::Status;
// Appends a value already produced by PreparedTypeInput or
// apply_type_modifier. CHAR bounds remain checked; canonical UTF-8 is not
// rescanned.
auto key_encode_canonical_into(const Value& value, std::vector<std::byte>& output) -> base::Status;

auto key_payload_size(const Value& value) -> base::Result<std::size_t>;
auto key_encoded_size(base::Bytes input, DataType data_type) -> base::Result<std::size_t>;
auto key_compare(base::Bytes left, base::Bytes right, DataType data_type)
    -> base::Result<std::strong_ordering>;
auto key_max_size(DataType data_type) -> base::Result<std::size_t>;

}  // namespace db::types
