#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/format.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"

#include "format_internal.hpp"
#include "format_validation.hpp"

namespace db::catalog {
namespace {

class ImageWriter final {
public:
    explicit ImageWriter(const std::size_t reserve)
        : appender_{bytes_} {
        bytes_.reserve(reserve);
    }

    auto position() const noexcept -> std::size_t {
        return bytes_.size();
    }

    void write_u8(const std::uint8_t value) {
        appender_.append_u8(value);
    }

    void write_u16(const std::uint16_t value) {
        appender_.append_u16(value);
    }

    void write_u32(const std::uint32_t value) {
        appender_.append_u32(value);
    }

    void write_u64(const std::uint64_t value) {
        appender_.append_u64(value);
    }

    void write_i32(const std::int32_t value) {
        appender_.append_i32(value);
    }

    void write_string(const std::string_view value) {
        appender_.append_bytes(std::as_bytes(std::span{value.data(), value.size()}));
    }

    void write_bytes(const base::Bytes value) {
        appender_.append_bytes(value);
    }

    auto patch_u32(const std::size_t offset, const std::uint32_t value) -> base::Status {
        if (offset > bytes_.size() || sizeof(value) > bytes_.size() - offset) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "catalog encoder patch is outside its output image",
                }
            );
        }
        base::LittleEndianWriter writer{base::MutableBytes{bytes_}.subspan(offset, sizeof(value))};
        return writer.write_u32(value);
    }

    auto finish() && -> std::vector<std::byte> {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
    base::LittleEndianAppender appender_;
};

template <typename Metadata, typename Projection>
auto sorted_pointers(const std::vector<Metadata>& values, Projection projection)
    -> std::vector<const Metadata*> {
    std::vector<const Metadata*> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(&value);
    }
    std::sort(result.begin(), result.end(), [projection](const auto* left, const auto* right) {
        return projection(*left) < projection(*right);
    });
    return result;
}

auto record_size(const std::size_t start, const std::size_t end) -> base::Result<std::uint32_t> {
    if (end < start || end - start > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kOutOfRange,
                "catalog record exceeds its 32-bit length field",
            }
        );
    }
    return static_cast<std::uint32_t>(end - start);
}

auto encode_column(ImageWriter& writer, const Column& column) -> base::Status {
    const auto start = writer.position();
    writer.write_u32(0U);
    writer.write_u32(column.data_type.id().value());
    writer.write_i32(column.data_type.modifier().value());
    writer.write_u16(static_cast<std::uint16_t>(column.name.size()));
    writer.write_u16(0U);
    writer.write_string(column.name);
    auto size = record_size(start, writer.position());
    if (!size) {
        return base::fail(std::move(size).error());
    }
    return writer.patch_u32(start, size.value());
}

auto encode_table(ImageWriter& writer, const TableMetadata& table) -> base::Status {
    const auto start = writer.position();
    writer.write_u32(0U);
    writer.write_u32(table.oid().value());
    writer.write_u32(table.relation_id().value());
    writer.write_u32(static_cast<std::uint32_t>(table.schema().size()));
    writer.write_u16(static_cast<std::uint16_t>(table.name().size()));
    writer.write_u8(table.fillfactor());
    writer.write_u8(0U);
    writer.write_u32(0U);
    writer.write_string(table.name());
    for (const auto& column : table.schema().columns()) {
        if (auto status = encode_column(writer, column); !status) {
            return status;
        }
    }
    auto size = record_size(start, writer.position());
    if (!size) {
        return base::fail(std::move(size).error());
    }
    return writer.patch_u32(start, size.value());
}

auto encode_index(ImageWriter& writer, const IndexMetadata& index) -> base::Status {
    const auto start = writer.position();
    writer.write_u32(0U);
    writer.write_u32(index.oid().value());
    writer.write_u32(index.index_id().value());
    writer.write_u32(index.relation_id().value());
    writer.write_u32(index.table_oid().value());
    writer.write_u64(index.schema_signature().value());
    writer.write_u16(static_cast<std::uint16_t>(index.column_names().size()));
    writer.write_u16(static_cast<std::uint16_t>(index.name().size()));
    writer.write_u8(index.unique() ? 1U : 0U);
    writer.write_u8(0U);
    writer.write_u16(0U);
    writer.write_string(index.name());
    for (const auto& column_name : index.column_names()) {
        writer.write_u16(static_cast<std::uint16_t>(column_name.size()));
        writer.write_string(column_name);
    }
    auto size = record_size(start, writer.position());
    if (!size) {
        return base::fail(std::move(size).error());
    }
    return writer.patch_u32(start, size.value());
}

auto encode_column_statistics(ImageWriter& writer, const ColumnStatistics& statistics)
    -> base::Status {
    auto minimum = encode_statistics_bound(statistics.minimum, statistics.data_type);
    auto maximum = encode_statistics_bound(statistics.maximum, statistics.data_type);
    if (!minimum || !maximum) {
        return base::fail(!minimum ? std::move(minimum).error() : std::move(maximum).error());
    }
    if (minimum->size() > std::numeric_limits<std::uint32_t>::max()
        || maximum->size() > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kOutOfRange,
                "catalog statistics bound exceeds its 32-bit length field",
            }
        );
    }
    const auto start = writer.position();
    writer.write_u32(0U);
    writer.write_u32(statistics.data_type.id().value());
    writer.write_i32(statistics.data_type.modifier().value());
    writer.write_u64(std::bit_cast<std::uint64_t>(statistics.null_fraction));
    writer.write_u32(statistics.average_width);
    writer.write_u64(statistics.sample_distinct);
    writer.write_u32(static_cast<std::uint32_t>(minimum->size()));
    writer.write_u32(static_cast<std::uint32_t>(maximum->size()));
    writer.write_u32(0U);
    writer.write_bytes(*minimum);
    writer.write_bytes(*maximum);
    auto size = record_size(start, writer.position());
    if (!size) {
        return base::fail(std::move(size).error());
    }
    return writer.patch_u32(start, size.value());
}

auto encode_statistics(ImageWriter& writer, const TableStatistics& statistics) -> base::Status {
    const auto start = writer.position();
    writer.write_u32(0U);
    writer.write_u32(statistics.table_oid.value());
    writer.write_u32(statistics.relation_pages);
    writer.write_u32(static_cast<std::uint32_t>(statistics.columns.size()));
    writer.write_u64(statistics.live_rows);
    writer.write_u64(statistics.dead_rows);
    writer.write_u32(statistics.average_row_width);
    writer.write_u32(statistics.sample_rows);
    writer.write_u32(statistics.analyzed_xid.value());
    writer.write_u32(0U);
    for (const auto& column : statistics.columns) {
        if (auto status = encode_column_statistics(writer, column); !status) {
            return status;
        }
    }
    auto size = record_size(start, writer.position());
    if (!size) {
        return base::fail(std::move(size).error());
    }
    return writer.patch_u32(start, size.value());
}

}  // namespace

auto encode_catalog_image(const CatalogImage& image) -> base::Result<std::vector<std::byte>> {
    if (auto status = validate_catalog_image_model(image); !status) {
        return base::fail(std::move(status).error().with_context("catalog.format", "encode"));
    }

    const auto tables = sorted_pointers(image.tables, [](const auto& table) {
        return table.oid();
    });
    const auto indexes = sorted_pointers(image.indexes, [](const auto& index) {
        return index.oid();
    });
    const auto statistics = sorted_pointers(image.statistics, [](const auto& value) {
        return value.table_oid;
    });

    ImageWriter writer{kCatalogFormatHeaderSize};
    writer.write_u32(kCatalogFormatMagic);
    writer.write_u16(kCatalogFormatVersion);
    writer.write_u16(kCatalogFormatHeaderSize);
    writer.write_u32(0U);
    writer.write_u32(0U);
    writer.write_u64(image.generation);
    writer.write_u64(image.id_state.next_oid);
    writer.write_u64(image.id_state.next_relation_id);
    writer.write_u64(image.id_state.next_index_id);
    writer.write_u32(static_cast<std::uint32_t>(tables.size()));
    writer.write_u32(static_cast<std::uint32_t>(indexes.size()));
    writer.write_u32(static_cast<std::uint32_t>(statistics.size()));
    writer.write_u32(0U);

    for (const auto* table : tables) {
        if (auto status = encode_table(writer, *table); !status) {
            return base::fail(std::move(status).error().with_context("catalog.format", "encode"));
        }
    }
    for (const auto* index : indexes) {
        if (auto status = encode_index(writer, *index); !status) {
            return base::fail(std::move(status).error().with_context("catalog.format", "encode"));
        }
    }
    for (const auto* table_statistics : statistics) {
        if (auto status = encode_statistics(writer, *table_statistics); !status) {
            return base::fail(std::move(status).error().with_context("catalog.format", "encode"));
        }
    }
    auto bytes = std::move(writer).finish();
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kOutOfRange,
                "catalog image exceeds its 32-bit total length field",
            }
        );
    }
    base::LittleEndianWriter total_writer{
        base::MutableBytes{bytes}.subspan(8U, sizeof(std::uint32_t))
    };
    if (auto status = total_writer.write_u32(static_cast<std::uint32_t>(bytes.size())); !status) {
        return base::fail(std::move(status).error());
    }
    if (auto status = base::seal_crc32c(bytes, kCatalogFormatChecksumOffset); !status) {
        return base::fail(std::move(status).error());
    }
    return bytes;
}

}  // namespace db::catalog
