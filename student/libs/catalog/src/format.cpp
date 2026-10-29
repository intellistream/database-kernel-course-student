#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"
#include "db/types/codec.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "format_internal.hpp"
#include "format_validation.hpp"

namespace db::catalog {
namespace {

auto corruption(const std::string_view message) -> base::Error {
    return base::Error{
        base::ErrorCode::kCorruption,
        std::string{message}
    }.with_context("catalog.format", "decode");
}

auto unsupported_version() -> base::Error {
    return base::Error{
        base::ErrorCode::kUnsupported,
        "catalog format version is unsupported",
    }
        .with_context("catalog.format", "decode");
}

auto read_string(base::LittleEndianReader& reader, const std::size_t size)
    -> base::Result<std::string> {
    auto bytes = reader.read_bytes(size);
    if (!bytes) {
        return base::fail(corruption("catalog string is truncated"));
    }
    std::string value;
    value.reserve(size);
    for (const auto byte : bytes.value()) {
        value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return value;
}

auto read_record(
    base::LittleEndianReader& reader,
    const std::size_t minimum_size,
    const std::string_view subject
) -> base::Result<base::Bytes> {
    auto size = reader.read_u32();
    if (!size) {
        return base::fail(corruption(std::string{subject} + " record length is truncated"));
    }
    if (size.value() < minimum_size) {
        return base::fail(corruption(std::string{subject} + " record is shorter than its header"));
    }
    auto body = reader.read_bytes(static_cast<std::size_t>(size.value()) - sizeof(std::uint32_t));
    if (!body) {
        return base::fail(corruption(std::string{subject} + " record is truncated"));
    }
    return body.value();
}

auto decode_column(base::LittleEndianReader& table_reader) -> base::Result<Column> {
    auto body = read_record(table_reader, kColumnRecordFixedSize + 1U, "column");
    if (!body) {
        return base::fail(std::move(body).error());
    }
    base::LittleEndianReader reader{body.value()};
    auto type_id = reader.read_u32();
    auto modifier = reader.read_i32();
    auto name_size = reader.read_u16();
    auto flags = reader.read_u16();
    if (!type_id || !modifier || !name_size || !flags) {
        return base::fail(corruption("column record header is truncated"));
    }
    if (flags.value() != 0U) {
        return base::fail(corruption("column record flags are nonzero"));
    }
    auto name = read_string(reader, name_size.value());
    if (!name) {
        return base::fail(std::move(name).error());
    }
    if (auto status = reader.finish(); !status) {
        return base::fail(corruption("column record has trailing data"));
    }
    return Column{
        .name = std::move(name).value(),
        .data_type = types::DataType{
            base::TypeId{type_id.value()},
            types::TypeModifier{modifier.value()},
        },
    };
}

auto decode_table(base::LittleEndianReader& image_reader, const CatalogDecodeOptions options)
    -> base::Result<TableMetadata> {
    auto body = read_record(image_reader, kTableRecordFixedSize + 1U, "table");
    if (!body) {
        return base::fail(std::move(body).error());
    }
    base::LittleEndianReader reader{body.value()};
    auto oid = reader.read_u32();
    auto relation_id = reader.read_u32();
    auto column_count = reader.read_u32();
    auto name_size = reader.read_u16();
    auto fillfactor = reader.read_u8();
    auto flags = reader.read_u8();
    auto reserved = reader.read_u32();
    if (!oid || !relation_id || !column_count || !name_size || !fillfactor || !flags || !reserved) {
        return base::fail(corruption("table record header is truncated"));
    }
    if (flags.value() != 0U || reserved.value() != 0U) {
        return base::fail(corruption("table record flags or reserved bytes are nonzero"));
    }
    if (column_count.value() > options.maximum_columns_per_table
        || column_count.value() > kMaximumColumnsPerTable
        || column_count.value() > reader.remaining() / (kColumnRecordFixedSize + 1U)) {
        return base::fail(
            corruption("table column count exceeds its configured or encoded bounds")
        );
    }
    auto name = read_string(reader, name_size.value());
    if (!name) {
        return base::fail(std::move(name).error());
    }
    std::vector<Column> columns;
    columns.reserve(column_count.value());
    for (std::uint32_t index = 0; index < column_count.value(); ++index) {
        auto column = decode_column(reader);
        if (!column) {
            return base::fail(std::move(column).error());
        }
        columns.push_back(std::move(column).value());
    }
    if (auto status = reader.finish(); !status) {
        return base::fail(corruption("table record has trailing data"));
    }
    auto schema = Schema::create(std::move(columns), options.row_size_limit);
    if (!schema) {
        return base::fail(corruption(
            "persisted table schema is invalid: " + std::string{schema.error().message()}
        ));
    }
    auto table = TableMetadata::create(
        TableMetadataDefinition{
            .oid = base::Oid{oid.value()},
            .relation_id = base::RelationId{relation_id.value()},
            .name = std::move(name).value(),
            .schema = std::move(schema).value(),
            .fillfactor = fillfactor.value(),
        }
    );
    if (!table) {
        return base::fail(corruption(
            "persisted table metadata is invalid: " + std::string{table.error().message()}
        ));
    }
    return table;
}

auto decode_index(base::LittleEndianReader& image_reader) -> base::Result<IndexMetadata> {
    auto body = read_record(image_reader, kIndexRecordFixedSize + 4U, "index");
    if (!body) {
        return base::fail(std::move(body).error());
    }
    base::LittleEndianReader reader{body.value()};
    auto oid = reader.read_u32();
    auto index_id = reader.read_u32();
    auto relation_id = reader.read_u32();
    auto table_oid = reader.read_u32();
    auto schema_signature = reader.read_u64();
    auto column_count = reader.read_u16();
    auto name_size = reader.read_u16();
    auto flags = reader.read_u8();
    auto reserved_byte = reader.read_u8();
    auto reserved = reader.read_u16();
    if (!oid || !index_id || !relation_id || !table_oid || !schema_signature || !column_count
        || !name_size || !flags || !reserved_byte || !reserved) {
        return base::fail(corruption("index record header is truncated"));
    }
    if ((flags.value() & 0xfeU) != 0U || reserved_byte.value() != 0U || reserved.value() != 0U) {
        return base::fail(corruption("index record flags or reserved bytes are invalid"));
    }
    if (column_count.value() == 0U
        || column_count.value() > reader.remaining() / (sizeof(std::uint16_t) + 1U)) {
        return base::fail(corruption("index key count exceeds its encoded bounds"));
    }
    auto name = read_string(reader, name_size.value());
    if (!name) {
        return base::fail(std::move(name).error());
    }
    std::vector<std::string> column_names;
    column_names.reserve(column_count.value());
    for (std::uint16_t index = 0; index < column_count.value(); ++index) {
        auto column_size = reader.read_u16();
        if (!column_size) {
            return base::fail(corruption("index column name length is truncated"));
        }
        auto column_name = read_string(reader, column_size.value());
        if (!column_name) {
            return base::fail(std::move(column_name).error());
        }
        column_names.push_back(std::move(column_name).value());
    }
    if (auto status = reader.finish(); !status) {
        return base::fail(corruption("index record has trailing data"));
    }
    auto metadata = IndexMetadata::create(
        IndexMetadataDefinition{
            .oid = base::Oid{oid.value()},
            .index_id = base::IndexId{index_id.value()},
            .relation_id = base::RelationId{relation_id.value()},
            .table_oid = base::Oid{table_oid.value()},
            .name = std::move(name).value(),
            .column_names = std::move(column_names),
            .schema_signature = IndexSchemaSignature{schema_signature.value()},
            .unique = flags.value() == 1U,
        }
    );
    if (!metadata) {
        return base::fail(corruption(
            "persisted index metadata is invalid: " + std::string{metadata.error().message()}
        ));
    }
    return metadata;
}

auto decode_statistics_bound(
    base::LittleEndianReader& reader,
    const std::uint32_t size,
    const types::DataType data_type,
    const std::string_view name
) -> base::Result<std::optional<types::Value>> {
    if (size == 0U) {
        return std::optional<types::Value>{};
    }
    auto bytes = reader.read_bytes(size);
    if (!bytes) {
        return base::fail(corruption("statistics " + std::string{name} + " is truncated"));
    }
    auto decoded = types::heap_decode(bytes.value(), data_type);
    if (!decoded || decoded->bytes_consumed != size) {
        return base::fail(
            corruption("statistics " + std::string{name} + " is not a canonical typed value")
        );
    }
    return std::optional<types::Value>{std::move(decoded->value)};
}

auto decode_column_statistics(base::LittleEndianReader& statistics_reader)
    -> base::Result<ColumnStatistics> {
    auto body =
        read_record(statistics_reader, kColumnStatisticsRecordFixedSize, "column statistics");
    if (!body) {
        return base::fail(std::move(body).error());
    }
    base::LittleEndianReader reader{body.value()};
    auto type_id = reader.read_u32();
    auto modifier = reader.read_i32();
    auto null_fraction = reader.read_u64();
    auto average_width = reader.read_u32();
    auto sample_distinct = reader.read_u64();
    auto minimum_size = reader.read_u32();
    auto maximum_size = reader.read_u32();
    auto flags = reader.read_u32();
    if (!type_id || !modifier || !null_fraction || !average_width || !sample_distinct
        || !minimum_size || !maximum_size || !flags) {
        return base::fail(corruption("column statistics record header is truncated"));
    }
    if (flags.value() != 0U) {
        return base::fail(corruption("column statistics flags are nonzero"));
    }
    if (minimum_size.value() > reader.remaining()
        || maximum_size.value() > reader.remaining() - minimum_size.value()) {
        return base::fail(corruption("column statistics bounds exceed their encoded record"));
    }
    const types::DataType data_type{
        base::TypeId{type_id.value()},
        types::TypeModifier{modifier.value()},
    };
    auto minimum = decode_statistics_bound(reader, minimum_size.value(), data_type, "minimum");
    if (!minimum) {
        return base::fail(std::move(minimum).error());
    }
    auto maximum = decode_statistics_bound(reader, maximum_size.value(), data_type, "maximum");
    if (!maximum) {
        return base::fail(std::move(maximum).error());
    }
    if (auto status = reader.finish(); !status) {
        return base::fail(corruption("column statistics record has trailing data"));
    }
    return ColumnStatistics{
        .data_type = data_type,
        .null_fraction = std::bit_cast<double>(null_fraction.value()),
        .average_width = average_width.value(),
        .sample_distinct = sample_distinct.value(),
        .minimum = std::move(minimum).value(),
        .maximum = std::move(maximum).value(),
    };
}

auto decode_statistics(base::LittleEndianReader& image_reader, const CatalogDecodeOptions options)
    -> base::Result<TableStatistics> {
    auto body = read_record(image_reader, kStatisticsRecordFixedSize, "table statistics");
    if (!body) {
        return base::fail(std::move(body).error());
    }
    base::LittleEndianReader reader{body.value()};
    auto table_oid = reader.read_u32();
    auto relation_pages = reader.read_u32();
    auto column_count = reader.read_u32();
    auto live_rows = reader.read_u64();
    auto dead_rows = reader.read_u64();
    auto average_row_width = reader.read_u32();
    auto sample_rows = reader.read_u32();
    auto analyzed_xid = reader.read_u32();
    auto flags = reader.read_u32();
    if (!table_oid || !relation_pages || !column_count || !live_rows || !dead_rows
        || !average_row_width || !sample_rows || !analyzed_xid || !flags) {
        return base::fail(corruption("table statistics record header is truncated"));
    }
    if (flags.value() != 0U) {
        return base::fail(corruption("table statistics flags are nonzero"));
    }
    if (column_count.value() > options.maximum_columns_per_table
        || column_count.value() > kMaximumColumnsPerTable
        || column_count.value() > reader.remaining() / kColumnStatisticsRecordFixedSize) {
        return base::fail(corruption("statistics column count exceeds its encoded bounds"));
    }
    std::vector<ColumnStatistics> columns;
    columns.reserve(column_count.value());
    for (std::uint32_t index = 0U; index < column_count.value(); ++index) {
        auto column = decode_column_statistics(reader);
        if (!column) {
            return base::fail(std::move(column).error());
        }
        columns.push_back(std::move(column).value());
    }
    if (auto status = reader.finish(); !status) {
        return base::fail(corruption("table statistics record has trailing data"));
    }
    return TableStatistics{
        .table_oid = base::Oid{table_oid.value()},
        .relation_pages = relation_pages.value(),
        .live_rows = live_rows.value(),
        .dead_rows = dead_rows.value(),
        .average_row_width = average_row_width.value(),
        .sample_rows = sample_rows.value(),
        .analyzed_xid = base::Xid{analyzed_xid.value()},
        .columns = std::move(columns),
    };
}

auto validate_decoded_model(const CatalogImage& image) -> base::Status {
    auto status = validate_catalog_image_model(image);
    if (!status) {
        return base::fail(corruption(
            "persisted catalog identity state is invalid: " + std::string{status.error().message()}
        ));
    }
    return {};
}

struct DecodedHeader final {
    std::uint64_t generation;
    CatalogIdState id_state;
    std::uint32_t table_count;
    std::uint32_t index_count;
    std::uint32_t statistics_count;
};

auto decode_header(const base::Bytes bytes, const CatalogDecodeOptions options)
    -> base::Result<DecodedHeader> {
    base::LittleEndianReader reader{bytes.first(kCatalogFormatHeaderSize)};
    auto magic = reader.read_u32();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto total_size = reader.read_u32();
    auto checksum = reader.read_u32();
    auto generation = reader.read_u64();
    auto next_oid = reader.read_u64();
    auto next_relation_id = reader.read_u64();
    auto next_index_id = reader.read_u64();
    auto table_count = reader.read_u32();
    auto index_count = reader.read_u32();
    auto statistics_count = reader.read_u32();
    auto flags = reader.read_u32();
    if (!magic || !version || !header_size || !total_size || !checksum || !generation || !next_oid
        || !next_relation_id || !next_index_id || !table_count || !index_count || !statistics_count
        || !flags) {
        return base::fail(corruption("catalog header is truncated"));
    }
    if (magic.value() != kCatalogFormatMagic) {
        return base::fail(corruption("catalog magic does not match"));
    }
    if (header_size.value() != kCatalogFormatHeaderSize) {
        return base::fail(corruption("catalog header size is unsupported"));
    }
    if (total_size.value() != bytes.size()) {
        return base::fail(corruption("catalog total length does not match the input size"));
    }
    if (auto status = base::verify_crc32c(bytes, kCatalogFormatChecksumOffset); !status) {
        return base::fail(corruption("catalog checksum does not match"));
    }
    if (version.value() != kCatalogFormatVersion) {
        return base::fail(unsupported_version());
    }
    if (flags.value() != 0U) {
        return base::fail(corruption("catalog flags are nonzero"));
    }
    if (generation.value() == 0U) {
        return base::fail(corruption("catalog generation is zero"));
    }
    const auto payload_size = bytes.size() - kCatalogFormatHeaderSize;
    if (table_count.value() > options.maximum_tables || table_count.value() > kMaximumCatalogObjects
        || table_count.value() > payload_size / (kTableRecordFixedSize + 1U)
        || index_count.value() > options.maximum_indexes
        || index_count.value() > kMaximumCatalogObjects
        || index_count.value() > payload_size / (kIndexRecordFixedSize + 4U)
        || statistics_count.value() > options.maximum_statistics
        || statistics_count.value() > kMaximumCatalogObjects
        || statistics_count.value() > payload_size / kStatisticsRecordFixedSize) {
        return base::fail(
            corruption("catalog object count exceeds its configured or encoded bounds")
        );
    }
    return DecodedHeader{
        .generation = generation.value(),
        .id_state =
            CatalogIdState{
                .next_oid = next_oid.value(),
                .next_relation_id = next_relation_id.value(),
                .next_index_id = next_index_id.value(),
            },
        .table_count = table_count.value(),
        .index_count = index_count.value(),
        .statistics_count = statistics_count.value(),
    };
}

auto decode_records(
    const base::Bytes payload,
    const DecodedHeader header,
    const CatalogDecodeOptions options
) -> base::Result<CatalogImage> {
    base::LittleEndianReader reader{payload};
    CatalogImage image{
        .generation = header.generation,
        .id_state = header.id_state,
        .tables = {},
        .indexes = {},
        .statistics = {},
    };
    image.tables.reserve(header.table_count);
    base::Oid previous_table_oid{0U};
    for (std::uint32_t index = 0; index < header.table_count; ++index) {
        auto table = decode_table(reader, options);
        if (!table) {
            return base::fail(std::move(table).error());
        }
        if (table->oid() <= previous_table_oid) {
            return base::fail(corruption("table records are not in strict canonical OID order"));
        }
        previous_table_oid = table->oid();
        image.tables.push_back(std::move(table).value());
    }
    if (header.index_count > reader.remaining() / (kIndexRecordFixedSize + 4U)) {
        return base::fail(corruption("index count exceeds its encoded bounds"));
    }
    image.indexes.reserve(header.index_count);
    base::Oid previous_index_oid{0U};
    for (std::uint32_t index = 0; index < header.index_count; ++index) {
        auto metadata = decode_index(reader);
        if (!metadata) {
            return base::fail(std::move(metadata).error());
        }
        if (metadata->oid() <= previous_index_oid) {
            return base::fail(corruption("index records are not in strict canonical OID order"));
        }
        previous_index_oid = metadata->oid();
        image.indexes.push_back(std::move(metadata).value());
    }
    if (header.statistics_count > reader.remaining() / kStatisticsRecordFixedSize) {
        return base::fail(corruption("statistics count exceeds its encoded bounds"));
    }
    image.statistics.reserve(header.statistics_count);
    base::Oid previous_statistics_oid{0U};
    for (std::uint32_t index = 0U; index < header.statistics_count; ++index) {
        auto statistics = decode_statistics(reader, options);
        if (!statistics) {
            return base::fail(std::move(statistics).error());
        }
        if (statistics->table_oid <= previous_statistics_oid) {
            return base::fail(
                corruption("statistics records are not in strict canonical table OID order")
            );
        }
        previous_statistics_oid = statistics->table_oid;
        image.statistics.push_back(std::move(statistics).value());
    }
    if (auto status = reader.finish(); !status) {
        return base::fail(corruption("catalog image has trailing data"));
    }
    if (auto status = validate_decoded_model(image); !status) {
        return base::fail(std::move(status).error());
    }
    return image;
}

}  // namespace

auto decode_catalog_image(const base::Bytes bytes, const CatalogDecodeOptions options)
    -> base::Result<CatalogImage> {
    if (bytes.size() > options.maximum_image_size) {
        return base::fail(
            base::Error{
                base::ErrorCode::kResourceExhausted,
                "catalog image exceeds the configured decode limit",
            }
                .with_context("catalog.format", "decode")
        );
    }
    if (bytes.size() < kCatalogFormatHeaderSize) {
        return base::fail(corruption("catalog image is shorter than its fixed header"));
    }
    auto header = decode_header(bytes, options);
    if (!header) {
        return base::fail(std::move(header).error());
    }
    return decode_records(bytes.subspan(kCatalogFormatHeaderSize), header.value(), options);
}

auto upgrade_catalog_image(
    const base::Bytes bytes,
    const std::uint16_t target_version,
    const CatalogDecodeOptions options
) -> base::Result<std::vector<std::byte>> {
    if (target_version != kCatalogFormatVersion) {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnsupported,
                "target catalog format version is unsupported",
            }
                .with_context("catalog.format", "upgrade")
        );
    }
    auto decoded = decode_catalog_image(bytes, options);
    if (!decoded) {
        return base::fail(std::move(decoded).error().with_context("catalog.format", "upgrade"));
    }
    return encode_catalog_image(decoded.value());
}

}  // namespace db::catalog
