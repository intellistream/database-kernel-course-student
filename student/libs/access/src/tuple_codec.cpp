#include "db/access/tuple_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "db/access/heap_page.hpp"
#include "db/base/error.hpp"
#include "db/types/codec.hpp"
#include "db/types/type_registry.hpp"

namespace db::access {
namespace {

auto tuple_codec_error(const base::ErrorCode code, const std::string_view message) -> base::Error {
    return base::Error{code, std::string{message}}.with_context("access", "heap row codec");
}

constexpr auto bitmap_size(const std::size_t count) noexcept -> std::size_t {
    constexpr std::size_t k_bits_per_byte = 8;
    return (count + k_bits_per_byte - 1U) / k_bits_per_byte;
}

auto null_bit(const base::Bytes bitmap, const std::size_t index) noexcept -> bool {
    constexpr std::size_t k_bits_per_byte = 8;
    const auto mask = static_cast<std::uint8_t>(1U << (index % k_bits_per_byte));
    return (std::to_integer<std::uint8_t>(bitmap[index / k_bits_per_byte]) & mask) != 0U;
}

void set_null_bit(base::MutableBytes bitmap, const std::size_t index) noexcept {
    constexpr std::size_t k_bits_per_byte = 8;
    const auto byte_index = index / k_bits_per_byte;
    const auto mask = static_cast<std::uint8_t>(1U << (index % k_bits_per_byte));
    bitmap[byte_index] |= static_cast<std::byte>(mask);
}

auto unused_bitmap_bits_are_zero(const base::Bytes bitmap, const std::size_t column_count) noexcept
    -> bool {
    constexpr std::size_t k_bits_per_byte = 8;
    if (bitmap.empty() || column_count % k_bits_per_byte == 0U) {
        return true;
    }
    const auto used_bits = column_count % k_bits_per_byte;
    const auto unused_mask = static_cast<std::uint8_t>(0xffU << used_bits);
    return (std::to_integer<std::uint8_t>(bitmap.back()) & unused_mask) == 0U;
}

}  // namespace

TupleSchema::TupleSchema(
    std::vector<TupleColumn> columns,
    std::vector<types::PreparedHeapEncoder> encoders,
    std::vector<types::PreparedHeapDecoder> decoders,
    const std::size_t encoded_size
)
    : columns_{std::move(columns)},
      encoders_{std::move(encoders)},
      decoders_{std::move(decoders)},
      encoded_size_{encoded_size} {}

auto TupleSchema::create(std::vector<TupleColumn> columns) -> base::Result<TupleSchema> {
    std::unordered_set<std::string> names;
    std::vector<types::PreparedHeapEncoder> encoders;
    std::vector<types::PreparedHeapDecoder> decoders;
    encoders.reserve(columns.size());
    decoders.reserve(columns.size());
    std::size_t encoded_size = bitmap_size(columns.size());

    for (const auto& column : columns) {
        if (column.name.empty()) {
            return base::fail(tuple_codec_error(
                base::ErrorCode::kInvalidArgument,
                "tuple column name must not be empty"
            ));
        }
        if (!names.insert(column.name).second) {
            return base::fail(tuple_codec_error(
                base::ErrorCode::kInvalidArgument,
                "tuple column names must be unique"
            ));
        }
        auto encoder = types::PreparedHeapEncoder::create(column.data_type);
        if (!encoder) {
            return base::fail(
                std::move(encoder).error().with_context("access", "create tuple schema")
            );
        }
        auto decoder = types::PreparedHeapDecoder::create(column.data_type);
        if (!decoder) {
            return base::fail(
                std::move(decoder).error().with_context("access", "create tuple schema")
            );
        }
        if (encoder->width() > std::numeric_limits<std::size_t>::max() - encoded_size) {
            return base::fail(tuple_codec_error(
                base::ErrorCode::kOutOfRange,
                "tuple schema encoded width overflows size_t"
            ));
        }
        encoded_size += encoder->width();
        encoders.push_back(std::move(encoder).value());
        decoders.push_back(std::move(decoder).value());
    }
    if (encoded_size > kMaximumHeapRowSize) {
        return base::fail(tuple_codec_error(
            base::ErrorCode::kResourceExhausted,
            "tuple schema cannot fit one row on a heap page"
        ));
    }
    return TupleSchema{
        std::move(columns),
        std::move(encoders),
        std::move(decoders),
        encoded_size,
    };
}

auto TupleSchema::columns() const noexcept -> std::span<const TupleColumn> {
    return columns_;
}

auto TupleSchema::size() const noexcept -> std::size_t {
    return columns_.size();
}

auto TupleSchema::empty() const noexcept -> bool {
    return columns_.empty();
}

auto TupleSchema::null_bitmap_size() const noexcept -> std::size_t {
    return bitmap_size(columns_.size());
}

auto TupleSchema::encoded_size() const noexcept -> std::size_t {
    return encoded_size_;
}

auto TupleSchema::position(const std::string_view name) const -> base::Result<std::size_t> {
    const auto found = std::find_if(columns_.begin(), columns_.end(), [name](const auto& column) {
        return column.name == name;
    });
    if (found == columns_.end()) {
        return base::fail(
            tuple_codec_error(base::ErrorCode::kNotFound, "tuple column was not found")
        );
    }
    return static_cast<std::size_t>(found - columns_.begin());
}

auto encode_row_impl(
    const TupleSchema& schema,
    const std::span<const types::Value> values,
    std::vector<std::byte>& output,
    const bool values_are_canonical
) -> base::Status {
    if (values.size() != schema.columns_.size()) {
        return base::fail(tuple_codec_error(
            base::ErrorCode::kInvalidArgument,
            "row value count does not match tuple schema"
        ));
    }

    output.resize(schema.encoded_size_);
    std::fill(output.begin(), output.end(), std::byte{0});
    const auto bitmap_length = schema.null_bitmap_size();
    auto bitmap = base::MutableBytes{output}.first(bitmap_length);
    std::size_t offset = bitmap_length;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto& value = values[index];
        const auto width = schema.encoders_[index].width();
        if (value.is_null()) {
            if (value.data_type() != schema.columns_[index].data_type) {
                return base::fail(tuple_codec_error(
                    base::ErrorCode::kInvalidArgument,
                    "row value type does not match tuple column"
                ));
            }
            set_null_bit(bitmap, index);
            offset += width;
            continue;
        }
        auto encoded = values_are_canonical ? schema.encoders_[index].encode_canonical_into(
                                                  value,
                                                  base::MutableBytes{output}.subspan(offset, width)
                                              )
                                            : schema.encoders_[index].encode_into(
                                                  value,
                                                  base::MutableBytes{output}.subspan(offset, width)
                                              );
        if (!encoded) {
            return base::fail(std::move(encoded).error().with_context("access", "encode heap row"));
        }
        offset += width;
    }
    return {};
}

auto encode_row_into(
    const TupleSchema& schema,
    const std::span<const types::Value> values,
    std::vector<std::byte>& output
) -> base::Status {
    return encode_row_impl(schema, values, output, false);
}

auto encode_canonical_row_into(
    const TupleSchema& schema,
    const std::span<const types::Value> values,
    std::vector<std::byte>& output
) -> base::Status {
    return encode_row_impl(schema, values, output, true);
}

auto encode_row(const TupleSchema& schema, const std::span<const types::Value> values)
    -> base::Result<std::vector<std::byte>> {
    std::vector<std::byte> output;
    auto encoded = encode_row_into(schema, values, output);
    if (!encoded) {
        return base::fail(std::move(encoded).error());
    }
    return output;
}

auto decode_row_projected_into(
    const TupleSchema& schema,
    const base::Bytes encoded,
    const std::span<const TupleColumnUsage> usage,
    std::vector<types::Value>& output
) -> base::Status {
    output.clear();
    if (encoded.size() != schema.encoded_size_) {
        return base::fail(tuple_codec_error(
            base::ErrorCode::kCorruption,
            "heap row length does not match tuple schema"
        ));
    }
    const auto bitmap_length = schema.null_bitmap_size();
    const auto bitmap = encoded.first(bitmap_length);
    if (!unused_bitmap_bits_are_zero(bitmap, schema.columns_.size())) {
        return base::fail(tuple_codec_error(
            base::ErrorCode::kCorruption,
            "heap row NULL bitmap has nonzero unused bits"
        ));
    }
    if (usage.size() != schema.columns_.size()) {
        return base::fail(tuple_codec_error(
            base::ErrorCode::kInvalidArgument,
            "tuple projection width does not match tuple schema"
        ));
    }

    output.reserve(schema.columns_.size());
    std::size_t offset = bitmap_length;
    for (std::size_t index = 0; index < schema.columns_.size(); ++index) {
        const auto& column = schema.columns_[index];
        const auto& decoder = schema.decoders_[index];
        const auto width = decoder.width();
        const auto payload = encoded.subspan(offset, width);
        if (null_bit(bitmap, index)) {
            if (std::any_of(payload.begin(), payload.end(), [](const std::byte value) {
                    return value != std::byte{0};
                })) {
                return base::fail(tuple_codec_error(
                    base::ErrorCode::kCorruption,
                    "NULL heap attribute has a nonzero canonical payload"
                ));
            }
            output.push_back(types::Value::null(column.data_type));
        } else if (usage[index] == TupleColumnUsage::kSkipped) {
            output.push_back(types::Value::null(column.data_type));
        } else {
            auto decoded = decoder.decode_exact(payload);
            if (!decoded) {
                return base::fail(
                    std::move(decoded).error().with_context("access", "decode heap row")
                );
            }
            output.push_back(std::move(decoded).value());
        }
        offset += width;
    }
    return {};
}

auto decode_row_projected(
    const TupleSchema& schema,
    const base::Bytes encoded,
    const std::span<const TupleColumnUsage> usage
) -> base::Result<std::vector<types::Value>> {
    std::vector<types::Value> output;
    auto decoded = decode_row_projected_into(schema, encoded, usage, output);
    if (!decoded) {
        return base::fail(std::move(decoded).error());
    }
    return output;
}

auto decode_row_into(
    const TupleSchema& schema,
    const base::Bytes encoded,
    std::vector<types::Value>& output
) -> base::Status {
    output.clear();
    if (encoded.size() != schema.encoded_size_) {
        return base::fail(tuple_codec_error(
            base::ErrorCode::kCorruption,
            "heap row length does not match tuple schema"
        ));
    }
    const auto bitmap_length = schema.null_bitmap_size();
    const auto bitmap = encoded.first(bitmap_length);
    if (!unused_bitmap_bits_are_zero(bitmap, schema.columns_.size())) {
        return base::fail(tuple_codec_error(
            base::ErrorCode::kCorruption,
            "heap row NULL bitmap has nonzero unused bits"
        ));
    }

    output.reserve(schema.columns_.size());
    std::size_t offset = bitmap_length;
    for (std::size_t index = 0; index < schema.columns_.size(); ++index) {
        const auto& column = schema.columns_[index];
        const auto& decoder = schema.decoders_[index];
        const auto width = decoder.width();
        const auto payload = encoded.subspan(offset, width);
        if (null_bit(bitmap, index)) {
            if (std::any_of(payload.begin(), payload.end(), [](const std::byte value) {
                    return value != std::byte{0};
                })) {
                return base::fail(tuple_codec_error(
                    base::ErrorCode::kCorruption,
                    "NULL heap attribute has a nonzero canonical payload"
                ));
            }
            output.push_back(types::Value::null(column.data_type));
        } else {
            auto decoded = decoder.decode_exact(payload);
            if (!decoded) {
                return base::fail(
                    std::move(decoded).error().with_context("access", "decode heap row")
                );
            }
            output.push_back(std::move(decoded).value());
        }
        offset += width;
    }
    return {};
}

auto decode_row(const TupleSchema& schema, const base::Bytes encoded)
    -> base::Result<std::vector<types::Value>> {
    std::vector<types::Value> output;
    auto decoded = decode_row_into(schema, encoded, output);
    if (!decoded) {
        return base::fail(std::move(decoded).error());
    }
    return output;
}

}  // namespace db::access
