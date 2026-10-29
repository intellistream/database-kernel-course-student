#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/types/codec.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::access {

struct TupleColumn final {
    std::string name;
    types::DataType data_type;

    auto operator==(const TupleColumn&) const noexcept -> bool = default;
};

enum class TupleColumnUsage : std::uint8_t {
    kSkipped,
    kMaterialized,
};

class TupleSchema final {
public:
    static auto create(std::vector<TupleColumn> columns) -> base::Result<TupleSchema>;

    auto columns() const noexcept -> std::span<const TupleColumn>;
    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;
    auto null_bitmap_size() const noexcept -> std::size_t;
    auto encoded_size() const noexcept -> std::size_t;
    auto position(std::string_view name) const -> base::Result<std::size_t>;

private:
    TupleSchema(
        std::vector<TupleColumn> columns,
        std::vector<types::PreparedHeapEncoder> encoders,
        std::vector<types::PreparedHeapDecoder> decoders,
        std::size_t encoded_size
    );

    std::vector<TupleColumn> columns_;
    std::vector<types::PreparedHeapEncoder> encoders_;
    std::vector<types::PreparedHeapDecoder> decoders_;
    std::size_t encoded_size_;

    friend auto encode_row(const TupleSchema& schema, std::span<const types::Value> values)
        -> base::Result<std::vector<std::byte>>;
    friend auto encode_row_into(
        const TupleSchema& schema,
        std::span<const types::Value> values,
        std::vector<std::byte>& output
    ) -> base::Status;
    friend auto encode_row_impl(
        const TupleSchema& schema,
        std::span<const types::Value> values,
        std::vector<std::byte>& output,
        bool values_are_canonical
    ) -> base::Status;
    friend auto encode_canonical_row_into(
        const TupleSchema& schema,
        std::span<const types::Value> values,
        std::vector<std::byte>& output
    ) -> base::Status;
    friend auto decode_row(const TupleSchema& schema, base::Bytes encoded)
        -> base::Result<std::vector<types::Value>>;
    friend auto decode_row_into(
        const TupleSchema& schema,
        base::Bytes encoded,
        std::vector<types::Value>& output
    ) -> base::Status;
    friend auto decode_row_projected(
        const TupleSchema& schema,
        base::Bytes encoded,
        std::span<const TupleColumnUsage> usage
    ) -> base::Result<std::vector<types::Value>>;
    friend auto decode_row_projected_into(
        const TupleSchema& schema,
        base::Bytes encoded,
        std::span<const TupleColumnUsage> usage,
        std::vector<types::Value>& output
    ) -> base::Status;
};

auto encode_row(const TupleSchema& schema, std::span<const types::Value> values)
    -> base::Result<std::vector<std::byte>>;

// Reuses caller-owned storage for a fixed-width heap tuple. On success output
// is canonical and exactly schema.encoded_size() bytes long.
auto encode_row_into(
    const TupleSchema& schema,
    std::span<const types::Value> values,
    std::vector<std::byte>& output
) -> base::Status;

// LOAD/COPY fast path for values already canonicalized by PreparedTypeInput
// or apply_type_modifier. Shape and type checks remain; CHAR UTF-8 is not
// rescanned for every physical representation.
auto encode_canonical_row_into(
    const TupleSchema& schema,
    std::span<const types::Value> values,
    std::vector<std::byte>& output
) -> base::Status;

auto decode_row(const TupleSchema& schema, base::Bytes encoded)
    -> base::Result<std::vector<types::Value>>;

// Decodes into caller-owned storage. Existing capacity is reused; on failure
// output may contain a decoded prefix and must not be consumed as a row.
auto decode_row_into(
    const TupleSchema& schema,
    base::Bytes encoded,
    std::vector<types::Value>& output
) -> base::Status;

// Deforms only requested attributes while retaining the table's stable row
// shape. Skipped positions contain typed NULL placeholders and must not be
// referenced by an upper node; planner required-column propagation owns that
// contract. Record length, bitmap, and canonical NULL payloads remain checked.
auto decode_row_projected(
    const TupleSchema& schema,
    base::Bytes encoded,
    std::span<const TupleColumnUsage> usage
) -> base::Result<std::vector<types::Value>>;

auto decode_row_projected_into(
    const TupleSchema& schema,
    base::Bytes encoded,
    std::span<const TupleColumnUsage> usage,
    std::vector<types::Value>& output
) -> base::Status;

}  // namespace db::access
