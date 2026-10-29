#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/format.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "format_internal.hpp"

namespace db::catalog {
namespace {

constexpr RowSizeLimit kRowLimit{.maximum_encoded_size = 4'036U};

auto oracle_crc32c(const base::Bytes bytes) noexcept -> std::uint32_t {
    constexpr std::uint32_t k_polynomial = 0x82F6'3B78U;
    auto crc = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const bool checksum_byte =
            index >= kCatalogFormatChecksumOffset && index < kCatalogFormatChecksumOffset + 4U;
        crc ^= checksum_byte ? 0U : std::to_integer<std::uint8_t>(bytes[index]);
        for (std::uint8_t bit = 0; bit < 8U; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (k_polynomial & mask);
        }
    }
    return ~crc;
}

void write_u16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value) {
    base::LittleEndianWriter writer{base::MutableBytes{bytes}.subspan(offset, 2U)};
    ASSERT_TRUE(writer.write_u16(value));
}

void write_u32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value) {
    base::LittleEndianWriter writer{base::MutableBytes{bytes}.subspan(offset, 4U)};
    ASSERT_TRUE(writer.write_u32(value));
}

void write_u64(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint64_t value) {
    base::LittleEndianWriter writer{base::MutableBytes{bytes}.subspan(offset, 8U)};
    ASSERT_TRUE(writer.write_u64(value));
}

auto read_u32(const std::vector<std::byte>& bytes, const std::size_t offset) -> std::uint32_t {
    base::LittleEndianReader reader{base::Bytes{bytes}.subspan(offset, sizeof(std::uint32_t))};
    const auto value = reader.read_u32();
    EXPECT_TRUE(value);
    return value.value();
}

void reseal(std::vector<std::byte>& bytes) {
    write_u32(bytes, kCatalogFormatChecksumOffset, 0U);
    write_u32(bytes, kCatalogFormatChecksumOffset, oracle_crc32c(bytes));
}

auto schema(std::vector<Column> columns) -> Schema {
    auto result = Schema::create(std::move(columns), kRowLimit);
    EXPECT_TRUE(result) << result.error().to_string();
    return std::move(result).value();
}

auto table(
    const std::uint32_t oid,
    const std::uint32_t relation_id,
    std::string name,
    std::vector<Column> columns,
    const std::uint8_t fillfactor = kDefaultHeapFillfactor
) -> TableMetadata {
    auto result = TableMetadata::create(
        TableMetadataDefinition{
            .oid = base::Oid{oid},
            .relation_id = base::RelationId{relation_id},
            .name = std::move(name),
            .schema = schema(std::move(columns)),
            .fillfactor = fillfactor,
        }
    );
    EXPECT_TRUE(result) << result.error().to_string();
    return std::move(result).value();
}

auto index(IndexMetadataDefinition definition) -> IndexMetadata {
    auto result = IndexMetadata::create(std::move(definition));
    EXPECT_TRUE(result) << result.error().to_string();
    return std::move(result).value();
}

void add_example_statistics(CatalogImage& image) {
    const auto label_type = image.tables[1].schema().columns()[1].data_type;
    image.statistics.push_back(
        TableStatistics{
            .table_oid = base::Oid{11U},
            .relation_pages = 3U,
            .live_rows = 5U,
            .dead_rows = 1U,
            .average_row_width = 8U,
            .sample_rows = 5U,
            .analyzed_xid = base::Xid{22U},
            .columns = {ColumnStatistics{
                .data_type = image.tables[0].schema().columns()[0].data_type,
                .average_width = 8U,
                .sample_distinct = 5U,
                .minimum =
                    types::Value::timestamp(types::TimestampValue{1'000U}, types::TypeModifier{3}),
                .maximum =
                    types::Value::timestamp(types::TimestampValue{9'000U}, types::TypeModifier{3}),
            }},
        }
    );
    image.statistics.push_back(
        TableStatistics{
            .table_oid = base::Oid{4U},
            .relation_pages = 9U,
            .live_rows = 42U,
            .dead_rows = 2U,
            .average_row_width = 29U,
            .sample_rows = 10U,
            .analyzed_xid = base::Xid{17U},
            .columns = {
                ColumnStatistics{
                    .data_type = types::kInt64Type,
                    .null_fraction = 0.1,
                    .average_width = 8U,
                    .sample_distinct = 9U,
                    .minimum = types::Value::integer(-1),
                    .maximum = types::Value::integer(99),
                },
                ColumnStatistics{
                    .data_type = label_type,
                    .average_width = 4U,
                    .sample_distinct = 3U,
                    .minimum = types::Value::character("a", label_type.modifier()),
                    .maximum = types::Value::character("z", label_type.modifier()),
                },
                ColumnStatistics{
                    .data_type = types::kBoolType,
                    .average_width = 1U,
                    .sample_distinct = 2U,
                    .minimum = types::Value::boolean(false),
                    .maximum = types::Value::boolean(true),
                },
                ColumnStatistics{
                    .data_type = types::kFloat32Type,
                    .average_width = 4U,
                    .sample_distinct = 4U,
                    .minimum = types::Value::floating(1.25F),
                    .maximum = types::Value::floating(9.5F),
                },
            },
        }
    );
}

auto example_image() -> CatalogImage {
    CatalogImage image{
        .generation = 17U,
        .id_state =
            CatalogIdState{
                .next_oid = 13U,
                .next_relation_id = 43U,
                .next_index_id = 6U,
            },
        .tables = {},
        .indexes = {},
    };
    image.tables.push_back(table(
        11U,
        41U,
        "events",
        std::vector<Column>{Column{
            .name = "occurred_at",
            .data_type = types::DataType{types::kTimestampTypeId, types::TypeModifier{3}},
        }},
        90U
    ));
    image.tables.push_back(table(
        4U,
        7U,
        "accounts",
        std::vector<Column>{
            Column{.name = "id", .data_type = types::kInt64Type},
            Column{
                .name = "label",
                .data_type = types::DataType{types::kCharTypeId, types::TypeModifier{12}},
            },
            Column{.name = "active", .data_type = types::kBoolType},
            Column{.name = "balance", .data_type = types::kFloat32Type},
        }
    ));
    image.indexes.push_back(index(
        IndexMetadataDefinition{
            .oid = base::Oid{12U},
            .index_id = base::IndexId{5U},
            .relation_id = base::RelationId{42U},
            .table_oid = base::Oid{11U},
            .name = "events_time_idx",
            .column_names = {"occurred_at"},
            .schema_signature = index_schema_signature(
                                    std::vector<types::DataType>{types::DataType{
                                        types::kTimestampTypeId,
                                        types::TypeModifier{3},
                                    }}
            ).value(),
            .unique = false,
        }
    ));
    image.indexes.push_back(index(
        IndexMetadataDefinition{
            .oid = base::Oid{6U},
            .index_id = base::IndexId{2U},
            .relation_id = base::RelationId{8U},
            .table_oid = base::Oid{4U},
            .name = "accounts_id_key",
            .column_names = {"id"},
            .schema_signature =
                index_schema_signature(std::vector<types::DataType>{types::kInt64Type}).value(),
            .unique = true,
        }
    ));
    add_example_statistics(image);
    return image;
}

auto statistics_offset(const std::vector<std::byte>& encoded) -> std::size_t {
    std::size_t offset = kCatalogFormatHeaderSize;
    for (std::uint32_t table_index = 0U; table_index < read_u32(encoded, 48U); ++table_index) {
        offset += read_u32(encoded, offset);
    }
    for (std::uint32_t index = 0U; index < read_u32(encoded, 52U); ++index) {
        offset += read_u32(encoded, offset);
    }
    return offset;
}

auto decode_options() -> CatalogDecodeOptions {
    return CatalogDecodeOptions{.row_size_limit = kRowLimit};
}

TEST(CatalogFormatTest, EmptyImageHasStableVersionOneEnvelope) {
    const auto encoded = encode_catalog_image(CatalogImage{});
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    ASSERT_EQ(encoded->size(), kCatalogFormatHeaderSize);

    base::LittleEndianReader reader{*encoded};
    EXPECT_EQ(reader.read_u32().value(), kCatalogFormatMagic);
    EXPECT_EQ(reader.read_u16().value(), kCatalogFormatVersion);
    EXPECT_EQ(reader.read_u16().value(), kCatalogFormatHeaderSize);
    EXPECT_EQ(reader.read_u32().value(), kCatalogFormatHeaderSize);
    EXPECT_EQ(reader.read_u32().value(), oracle_crc32c(*encoded));
    EXPECT_EQ(reader.read_u64().value(), 1U);
    EXPECT_EQ(reader.read_u64().value(), 1U);
    EXPECT_EQ(reader.read_u64().value(), 1U);
    EXPECT_EQ(reader.read_u64().value(), 1U);
    EXPECT_EQ(reader.read_u32().value(), 0U);
    EXPECT_EQ(reader.read_u32().value(), 0U);
    EXPECT_EQ(reader.read_u32().value(), 0U);
    EXPECT_EQ(reader.read_u32().value(), 0U);
    EXPECT_TRUE(reader.finish());
}

TEST(CatalogFormatTest, CanonicalOrderRoundTripsEveryMetadataField) {
    auto image = example_image();
    const auto first = encode_catalog_image(image);
    ASSERT_TRUE(first) << first.error().to_string();

    std::reverse(image.tables.begin(), image.tables.end());
    std::reverse(image.indexes.begin(), image.indexes.end());
    std::reverse(image.statistics.begin(), image.statistics.end());
    const auto reordered = encode_catalog_image(image);
    ASSERT_TRUE(reordered);
    EXPECT_EQ(*first, *reordered);

    const auto decoded = decode_catalog_image(*first, decode_options());
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(decoded->generation, 17U);
    EXPECT_EQ(decoded->id_state, image.id_state);
    ASSERT_EQ(decoded->tables.size(), 2U);
    EXPECT_EQ(decoded->tables[0].oid(), base::Oid{4U});
    EXPECT_EQ(decoded->tables[1].oid(), base::Oid{11U});
    EXPECT_EQ(
        decoded->tables[0].schema().columns()[1].data_type.modifier(),
        types::TypeModifier{12}
    );
    EXPECT_EQ(decoded->tables[1].fillfactor(), 90U);
    ASSERT_EQ(decoded->indexes.size(), 2U);
    EXPECT_EQ(decoded->indexes[0].oid(), base::Oid{6U});
    EXPECT_TRUE(decoded->indexes[0].unique());
    EXPECT_EQ(decoded->indexes[0].schema_signature(), image.indexes[0].schema_signature());
    EXPECT_EQ(decoded->indexes[1].column_names()[0], "occurred_at");
    ASSERT_EQ(decoded->statistics.size(), 2U);
    EXPECT_EQ(decoded->statistics[0].table_oid, base::Oid{4U});
    EXPECT_EQ(decoded->statistics[0].relation_pages, 9U);
    EXPECT_DOUBLE_EQ(decoded->statistics[0].columns[0].null_fraction, 0.1);
    EXPECT_EQ(decoded->statistics[0].columns[0].minimum, types::Value::integer(-1));
    EXPECT_EQ(
        decoded->statistics[0].columns[1].minimum,
        types::Value::character("a", types::TypeModifier{12})
    );
    EXPECT_EQ(decoded->statistics[0].columns[2].minimum, types::Value::boolean(false));
    EXPECT_EQ(decoded->statistics[0].columns[3].maximum, types::Value::floating(9.5F));
    EXPECT_EQ(
        decoded->statistics[1].columns[0].maximum,
        types::Value::timestamp(types::TimestampValue{9'000U}, types::TypeModifier{3})
    );
}

TEST(CatalogFormatTest, RejectsTruncationTrailingBytesChecksumAndReservedFields) {
    const auto canonical = encode_catalog_image(example_image());
    ASSERT_TRUE(canonical);

    for (std::size_t size = 0; size < canonical->size(); ++size) {
        auto prefix = std::vector<std::byte>{
            canonical->begin(),
            canonical->begin() + static_cast<std::ptrdiff_t>(size),
        };
        if (size >= kCatalogFormatHeaderSize) {
            write_u32(prefix, 8U, static_cast<std::uint32_t>(size));
            reseal(prefix);
        }
        const auto decoded = decode_catalog_image(prefix, decode_options());
        ASSERT_FALSE(decoded) << "accepted prefix size " << size;
        EXPECT_EQ(decoded.error().code(), base::ErrorCode::kCorruption);
    }

    auto trailing = *canonical;
    trailing.push_back(std::byte{0});
    EXPECT_EQ(
        decode_catalog_image(trailing, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto checksum = *canonical;
    checksum.back() ^= std::byte{1};
    EXPECT_EQ(
        decode_catalog_image(checksum, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto reserved = *canonical;
    write_u32(reserved, 60U, 1U);
    reseal(reserved);
    EXPECT_EQ(
        decode_catalog_image(reserved, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto out_of_order = *canonical;
    // The first table OID is the second u32 in the record at byte 64.
    write_u32(out_of_order, kCatalogFormatHeaderSize + 4U, 12U);
    reseal(out_of_order);
    EXPECT_EQ(
        decode_catalog_image(out_of_order, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto invalid_utf8 = *canonical;
    // The first table name begins after its fixed 24-byte record header.
    invalid_utf8[kCatalogFormatHeaderSize + 24U] = std::byte{0xff};
    reseal(invalid_utf8);
    EXPECT_EQ(
        decode_catalog_image(invalid_utf8, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );
}

TEST(CatalogFormatTest, RejectsUnsupportedVersionsThroughDecodeAndUpgradeEntry) {
    const auto canonical = encode_catalog_image(CatalogImage{});
    ASSERT_TRUE(canonical);

    for (const auto version : std::array<std::uint16_t, 2>{0U, 2U}) {
        auto unsupported = *canonical;
        write_u16(unsupported, 4U, version);
        reseal(unsupported);
        EXPECT_EQ(
            decode_catalog_image(unsupported, decode_options()).error().code(),
            base::ErrorCode::kUnsupported
        );
        EXPECT_EQ(
            upgrade_catalog_image(unsupported, kCatalogFormatVersion, decode_options())
                .error()
                .code(),
            base::ErrorCode::kUnsupported
        );
    }

    const auto upgraded =
        upgrade_catalog_image(*canonical, kCatalogFormatVersion, decode_options());
    ASSERT_TRUE(upgraded);
    EXPECT_EQ(*upgraded, *canonical);
    EXPECT_EQ(
        upgrade_catalog_image(*canonical, 2U, decode_options()).error().code(),
        base::ErrorCode::kUnsupported
    );
}

TEST(CatalogFormatTest, RevalidatesPersistedSemanticsAndFrontiers) {
    const auto single = encode_catalog_image(
        CatalogImage{
            .generation = 3U,
            .id_state =
                CatalogIdState{.next_oid = 8U, .next_relation_id = 10U, .next_index_id = 1U},
            .tables = {table(
                7U,
                9U,
                "t",
                std::vector<Column>{Column{.name = "x", .data_type = types::kInt64Type}}
            )},
            .indexes = {},
        }
    );
    ASSERT_TRUE(single);

    // Header frontiers must remain strictly above every allocated ID.
    auto stale_oid = *single;
    write_u64(stale_oid, 24U, 7U);
    reseal(stale_oid);
    EXPECT_EQ(
        decode_catalog_image(stale_oid, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto zero_generation = *single;
    write_u64(zero_generation, 16U, 0U);
    reseal(zero_generation);
    EXPECT_EQ(
        decode_catalog_image(zero_generation, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    // First table starts at byte 64; its fixed header is 24 bytes and name is one byte.
    // The first column TypeId is at 64 + 24 + 1 + 4.
    constexpr std::size_t k_type_id_offset = 93U;
    auto unknown_type = *single;
    write_u32(unknown_type, k_type_id_offset, 999U);
    reseal(unknown_type);
    EXPECT_EQ(
        decode_catalog_image(unknown_type, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto invalid_bool = *single;
    // A table record flag is byte 83 in the fixed record header.
    invalid_bool[83U] = std::byte{1};
    reseal(invalid_bool);
    EXPECT_EQ(
        decode_catalog_image(invalid_bool, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );
}

TEST(CatalogFormatTest, RejectsOrphanMissingColumnAndStaleIndexSignature) {
    const auto canonical = encode_catalog_image(example_image());
    ASSERT_TRUE(canonical);
    std::size_t first_index = kCatalogFormatHeaderSize;
    const auto table_count = read_u32(*canonical, 48U);
    for (std::uint32_t table = 0U; table < table_count; ++table) {
        first_index += read_u32(*canonical, first_index);
    }

    auto orphan = *canonical;
    write_u32(orphan, first_index + 16U, 999U);
    reseal(orphan);
    const auto orphan_result = decode_catalog_image(orphan, decode_options());
    ASSERT_FALSE(orphan_result);
    EXPECT_EQ(orphan_result.error().code(), base::ErrorCode::kCorruption);

    auto missing_column = *canonical;
    // The first canonical index belongs to accounts (OID 4). Rebind it to events (OID 11), whose
    // schema does not contain the persisted key name "id".
    write_u32(missing_column, first_index + 16U, 11U);
    reseal(missing_column);
    const auto missing_result = decode_catalog_image(missing_column, decode_options());
    ASSERT_FALSE(missing_result);
    EXPECT_EQ(missing_result.error().code(), base::ErrorCode::kCorruption);

    auto stale = *canonical;
    write_u64(stale, first_index + 20U, 1U);
    reseal(stale);
    const auto stale_result = decode_catalog_image(stale, decode_options());
    ASSERT_FALSE(stale_result);
    EXPECT_EQ(stale_result.error().code(), base::ErrorCode::kCorruption);
}

TEST(CatalogFormatTest, RejectsDamagedOrSchemaStaleStatistics) {
    const auto canonical = encode_catalog_image(example_image());
    ASSERT_TRUE(canonical);
    const auto statistics = statistics_offset(*canonical);
    const auto column = statistics + 48U;

    auto orphan = *canonical;
    write_u32(orphan, statistics + 4U, 999U);
    reseal(orphan);
    EXPECT_EQ(
        decode_catalog_image(orphan, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto stale_type = *canonical;
    write_u32(stale_type, column + 4U, types::kBoolTypeId.value());
    reseal(stale_type);
    EXPECT_EQ(
        decode_catalog_image(stale_type, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto nan_fraction = *canonical;
    write_u64(
        nan_fraction,
        column + 12U,
        std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN())
    );
    reseal(nan_fraction);
    EXPECT_EQ(
        decode_catalog_image(nan_fraction, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto impossible_distinct = *canonical;
    write_u64(impossible_distinct, column + 24U, 11U);
    reseal(impossible_distinct);
    EXPECT_EQ(
        decode_catalog_image(impossible_distinct, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );

    auto oversized_bound = *canonical;
    write_u32(oversized_bound, column + 32U, std::numeric_limits<std::uint32_t>::max());
    reseal(oversized_bound);
    EXPECT_EQ(
        decode_catalog_image(oversized_bound, decode_options()).error().code(),
        base::ErrorCode::kCorruption
    );
}

TEST(CatalogFormatTest, BoundsCountsAndImageSizeBeforeAllocation) {
    const auto canonical = encode_catalog_image(CatalogImage{});
    ASSERT_TRUE(canonical);

    auto huge_count = *canonical;
    write_u32(huge_count, 48U, std::numeric_limits<std::uint32_t>::max());
    reseal(huge_count);
    auto options = decode_options();
    options.maximum_tables = 4U;
    EXPECT_EQ(
        decode_catalog_image(huge_count, options).error().code(),
        base::ErrorCode::kCorruption
    );

    huge_count = *canonical;
    write_u32(huge_count, 56U, std::numeric_limits<std::uint32_t>::max());
    reseal(huge_count);
    options = decode_options();
    options.maximum_statistics = 4U;
    EXPECT_EQ(
        decode_catalog_image(huge_count, options).error().code(),
        base::ErrorCode::kCorruption
    );

    options = decode_options();
    options.maximum_image_size = canonical->size() - 1U;
    EXPECT_EQ(
        decode_catalog_image(*canonical, options).error().code(),
        base::ErrorCode::kResourceExhausted
    );
}

TEST(CatalogFormatTest, EncoderRejectsNoncanonicalIdentityState) {
    auto invalid = example_image();
    invalid.generation = 0U;
    EXPECT_EQ(encode_catalog_image(invalid).error().code(), base::ErrorCode::kInvalidArgument);

    invalid = example_image();
    invalid.id_state.next_relation_id = invalid.tables.front().relation_id().value();
    EXPECT_EQ(encode_catalog_image(invalid).error().code(), base::ErrorCode::kInvalidArgument);

    invalid = example_image();
    invalid.indexes.push_back(index(
        IndexMetadataDefinition{
            .oid = base::Oid{6U},
            .index_id = base::IndexId{3U},
            .relation_id = base::RelationId{99U},
            .table_oid = base::Oid{4U},
            .name = "duplicate_oid",
            .column_names = {"id"},
            .schema_signature =
                index_schema_signature(std::vector<types::DataType>{types::kInt64Type}).value(),
            .unique = false,
        }
    ));
    EXPECT_EQ(encode_catalog_image(invalid).error().code(), base::ErrorCode::kAlreadyExists);

    auto source = table(
        20U,
        50U,
        "moved_table",
        std::vector<Column>{Column{.name = "id", .data_type = types::kInt64Type}}
    );
    const auto destination = std::move(source);
    invalid = CatalogImage{
        .generation = 1U,
        .id_state = CatalogIdState{.next_oid = 21U, .next_relation_id = 51U, .next_index_id = 1U},
        // libstdc++ leaves this source name empty. Revalidate current object state instead of
        // trusting that it once came from a factory.
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move) - test input.
        .tables = {source},
        .indexes = {},
    };
    (void)destination;
    EXPECT_EQ(encode_catalog_image(invalid).error().code(), base::ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace db::catalog
