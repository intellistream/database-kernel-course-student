#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/format.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"
#include "db/catalog/store.hpp"
#include "db/io/durable.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "format_internal.hpp"

namespace db::catalog {
namespace {

constexpr RowSizeLimit kRowLimit{.maximum_encoded_size = 4'036U};
constexpr std::uint32_t kCrc32cPolynomial = 0x82F6'3B78U;

auto options() -> CatalogStoreOptions {
    return CatalogStoreOptions{
        .decode = CatalogDecodeOptions{.row_size_limit = kRowLimit},
    };
}

auto checksum(const base::Bytes bytes) noexcept -> std::uint32_t {
    auto crc = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const bool checksum_byte = index >= kCatalogFormatChecksumOffset
                                   && index < kCatalogFormatChecksumOffset + sizeof(std::uint32_t);
        crc ^= checksum_byte ? 0U : std::to_integer<std::uint8_t>(bytes[index]);
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (kCrc32cPolynomial & mask);
        }
    }
    return ~crc;
}

void write_u8(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint8_t value) {
    base::LittleEndianWriter writer{base::MutableBytes{bytes}.subspan(offset, sizeof(value))};
    ASSERT_TRUE(writer.write_u8(value));
}

void write_u16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value) {
    base::LittleEndianWriter writer{base::MutableBytes{bytes}.subspan(offset, sizeof(value))};
    ASSERT_TRUE(writer.write_u16(value));
}

void write_u32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value) {
    base::LittleEndianWriter writer{base::MutableBytes{bytes}.subspan(offset, sizeof(value))};
    ASSERT_TRUE(writer.write_u32(value));
}

auto read_u32(const std::vector<std::byte>& bytes, const std::size_t offset) -> std::uint32_t {
    base::LittleEndianReader reader{base::Bytes{bytes}.subspan(offset, sizeof(std::uint32_t))};
    const auto value = reader.read_u32();
    EXPECT_TRUE(value);
    return value.value();
}

void reseal(std::vector<std::byte>& bytes) {
    write_u32(bytes, kCatalogFormatChecksumOffset, 0U);
    write_u32(bytes, kCatalogFormatChecksumOffset, checksum(bytes));
}

void publish_fixture(const std::filesystem::path& path, const base::Bytes bytes) {
    ASSERT_TRUE(io::atomic_replace(path, bytes));
}

void expect_open_error(
    const std::filesystem::path& path,
    const base::Bytes bytes,
    const base::ErrorCode expected,
    const CatalogStoreOptions selected_options = options()
) {
    publish_fixture(path, bytes);
    const auto opened = CatalogStore::open(path, selected_options);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error().code(), expected);
    ASSERT_FALSE(opened.error().contexts().empty());
    EXPECT_EQ(opened.error().contexts().back().component, "catalog.store");
}

auto table_schema() -> Schema {
    auto schema = Schema::create(
        std::vector<Column>{
            Column{.name = "reading", .data_type = types::kFloat32Type},
            Column{
                .name = "recorded_at",
                .data_type =
                    types::DataType{
                        types::kTimestampTypeId,
                        types::TypeModifier{3},
                    },
            },
        },
        kRowLimit
    );
    EXPECT_TRUE(schema) << schema.error().to_string();
    return std::move(schema).value();
}

auto archive_schema() -> Schema {
    auto schema = Schema::create(
        std::vector<Column>{Column{.name = "enabled", .data_type = types::kBoolType}},
        kRowLimit
    );
    EXPECT_TRUE(schema) << schema.error().to_string();
    return std::move(schema).value();
}

auto table_statistics(const base::Oid table_oid) -> TableStatistics {
    return TableStatistics{
        .table_oid = table_oid,
        .relation_pages = 3U,
        .live_rows = 2U,
        .dead_rows = 1U,
        .average_row_width = 12U,
        .sample_rows = 2U,
        .analyzed_xid = base::Xid{17U},
        .columns = {
            ColumnStatistics{
                .data_type = types::kFloat32Type,
                .average_width = 4U,
                .sample_distinct = 2U,
                .minimum = types::Value::floating(1.25F),
                .maximum = types::Value::floating(9.5F),
            },
            ColumnStatistics{
                .data_type =
                    types::DataType{
                        types::kTimestampTypeId,
                        types::TypeModifier{3},
                    },
                .average_width = 8U,
                .sample_distinct = 2U,
                .minimum =
                    types::Value::timestamp(types::TimestampValue{1'000}, types::TypeModifier{3}),
                .maximum =
                    types::Value::timestamp(types::TimestampValue{9'000}, types::TypeModifier{3}),
            },
        },
    };
}

auto fixture_image() -> CatalogImage {
    auto table = TableMetadata::create(
        TableMetadataDefinition{
            .oid = base::Oid{1U},
            .relation_id = base::RelationId{1U},
            .name = "measurements",
            .schema = table_schema(),
            .fillfactor = 90U,
        }
    );
    EXPECT_TRUE(table) << table.error().to_string();
    const auto signature = index_schema_signature(
        std::vector<types::DataType>{
            types::kFloat32Type,
            types::DataType{types::kTimestampTypeId, types::TypeModifier{3}},
        }
    );
    EXPECT_TRUE(signature) << signature.error().to_string();
    auto index = IndexMetadata::create(
        IndexMetadataDefinition{
            .oid = base::Oid{2U},
            .index_id = base::IndexId{1U},
            .relation_id = base::RelationId{2U},
            .table_oid = base::Oid{1U},
            .name = "measurements_key",
            .column_names = {"reading", "recorded_at"},
            .schema_signature = signature.value(),
            .unique = true,
        }
    );
    EXPECT_TRUE(index) << index.error().to_string();
    return CatalogImage{
        .generation = 4U,
        .id_state = CatalogIdState{.next_oid = 3U, .next_relation_id = 3U, .next_index_id = 2U},
        .tables = {std::move(table).value()},
        .indexes = {std::move(index).value()},
        .statistics = {table_statistics(base::Oid{1U})},
    };
}

TEST(CatalogHistoryTest, CurrentVersionReopensAllFieldsAndContinuesEveryIdFrontier) {
    auto root = test_support::TestDirectory::create("catalog-history-current-version");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");

    {
        auto store = CatalogStore::create(path, options());
        ASSERT_TRUE(store) << store.error().to_string();
        const auto table = store->create_table(
            CreateTableRequest{
                .name = "measurements",
                .schema = table_schema(),
                .fillfactor = 90U,
            }
        );
        ASSERT_TRUE(table);
        ASSERT_TRUE(table->has_value());
        const auto index = store->create_index(
            CreateIndexRequest{
                .name = "measurements_key",
                .table_name = "measurements",
                .column_names = {"reading", "recorded_at"},
                .unique = true,
            }
        );
        ASSERT_TRUE(index);
        ASSERT_TRUE(index->has_value());
        ASSERT_TRUE(store->install_statistics({table_statistics(table->value().oid())}));
        EXPECT_EQ(store->generation().value(), 4U);
    }

    {
        auto reopened = CatalogStore::open(path, options());
        ASSERT_TRUE(reopened) << reopened.error().to_string();
        const auto table = reopened->lookup_table("measurements").value();
        ASSERT_TRUE(table);
        EXPECT_EQ(table->fillfactor(), 90U);
        ASSERT_EQ(table->schema().size(), 2U);
        EXPECT_EQ(
            table->schema().columns()[1].data_type,
            (types::DataType{types::kTimestampTypeId, types::TypeModifier{3}})
        );
        const auto index = reopened->lookup_index("measurements_key").value();
        ASSERT_TRUE(index);
        EXPECT_TRUE(index->unique());
        const std::vector<std::string> column_names{
            index->column_names().begin(),
            index->column_names().end(),
        };
        EXPECT_EQ(column_names, (std::vector<std::string>{"reading", "recorded_at"}));
        EXPECT_EQ(reopened->table_statistics(table->oid()).value(), table_statistics(table->oid()));

        const auto archive = reopened->create_table(
            CreateTableRequest{
                .name = "archive",
                .schema = archive_schema(),
            }
        );
        ASSERT_TRUE(archive);
        ASSERT_TRUE(archive->has_value());
        EXPECT_EQ(archive->value().oid(), base::Oid{3U});
        EXPECT_EQ(archive->value().relation_id(), base::RelationId{3U});
        const auto archive_index = reopened->create_index(
            CreateIndexRequest{
                .name = "archive_enabled_idx",
                .table_name = "archive",
                .column_names = {"enabled"},
            }
        );
        ASSERT_TRUE(archive_index);
        ASSERT_TRUE(archive_index->has_value());
        EXPECT_EQ(archive_index->value().oid(), base::Oid{4U});
        EXPECT_EQ(archive_index->value().index_id(), base::IndexId{2U});
        EXPECT_EQ(archive_index->value().relation_id(), base::RelationId{4U});
    }

    const auto reopened = CatalogStore::open(path, options());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->generation().value(), 6U);
    EXPECT_EQ(
        reopened->id_state().value(),
        (CatalogIdState{.next_oid = 5U, .next_relation_id = 5U, .next_index_id = 3U})
    );
    EXPECT_EQ(reopened->list_tables().value().size(), 2U);
    EXPECT_EQ(reopened->list_indexes().value().size(), 2U);
    EXPECT_EQ(reopened->table_statistics(base::Oid{1U}).value(), table_statistics(base::Oid{1U}));
}

TEST(CatalogHistoryTest, UnsupportedPastAndFutureVersionsRemainExplicitAtReopen) {
    auto root = test_support::TestDirectory::create("catalog-history-unsupported-version");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    const auto encoded = encode_catalog_image(CatalogImage{});
    ASSERT_TRUE(encoded);

    for (const auto version : std::array<std::uint16_t, 3>{0U, 2U, 65'535U}) {
        SCOPED_TRACE(version);
        auto unsupported = encoded.value();
        write_u16(unsupported, 4U, version);
        reseal(unsupported);
        expect_open_error(path, unsupported, base::ErrorCode::kUnsupported);
        EXPECT_EQ(
            upgrade_catalog_image(unsupported, kCatalogFormatVersion, options().decode)
                .error()
                .code(),
            base::ErrorCode::kUnsupported
        );
    }

    const auto upgraded = upgrade_catalog_image(*encoded, kCatalogFormatVersion, options().decode);
    ASSERT_TRUE(upgraded);
    EXPECT_EQ(*upgraded, *encoded);
}

TEST(CatalogHistoryTest, RustPrivateCatalogBytesAreNeverAcceptedAsCppHistory) {
    auto root = test_support::TestDirectory::create("catalog-history-rust-private-format");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");

    // Reference empty-catalog files start with a u32 payload size and then CTLG/version. FCAT is
    // deliberately independent and must not guess that those private bytes are one of its versions.
    std::vector<std::byte> rust_private(64U, std::byte{0});
    write_u32(rust_private, 0U, 25U);
    rust_private[4U] = std::byte{'C'};
    rust_private[5U] = std::byte{'T'};
    rust_private[6U] = std::byte{'L'};
    rust_private[7U] = std::byte{'G'};
    rust_private[8U] = std::byte{9U};
    expect_open_error(path, rust_private, base::ErrorCode::kCorruption);
}

TEST(CatalogReopenCorruptionTest, PropagatesPhysicalAndSemanticDamageCategories) {
    auto root = test_support::TestDirectory::create("catalog-reopen-corruption");
    ASSERT_TRUE(root);
    const auto path = root->join("catalog.cat");
    const auto encoded = encode_catalog_image(fixture_image());
    ASSERT_TRUE(encoded) << encoded.error().to_string();

    for (const auto size : std::array<std::size_t, 5>{0U, 1U, 63U, 64U, encoded->size() - 1U}) {
        SCOPED_TRACE(size);
        const std::vector<std::byte> prefix{
            encoded->begin(),
            encoded->begin() + static_cast<std::ptrdiff_t>(size),
        };
        expect_open_error(path, prefix, base::ErrorCode::kCorruption);
    }

    auto trailing = encoded.value();
    trailing.push_back(std::byte{0});
    expect_open_error(path, trailing, base::ErrorCode::kCorruption);

    auto checksum_damage = encoded.value();
    write_u32(
        checksum_damage,
        kCatalogFormatChecksumOffset,
        read_u32(checksum_damage, kCatalogFormatChecksumOffset) ^ 1U
    );
    expect_open_error(path, checksum_damage, base::ErrorCode::kCorruption);

    const std::size_t table_offset = kCatalogFormatHeaderSize;
    const std::size_t first_column_offset =
        table_offset + 24U + std::string_view{"measurements"}.size();
    const std::size_t second_column_offset =
        first_column_offset + read_u32(*encoded, first_column_offset);
    const std::size_t index_offset = table_offset + read_u32(*encoded, table_offset);
    const std::size_t statistics_offset = index_offset + read_u32(*encoded, index_offset);

    auto removed_float64 = encoded.value();
    write_u32(removed_float64, first_column_offset + 4U, types::kReservedFloat64TypeId.value());
    reseal(removed_float64);
    expect_open_error(path, removed_float64, base::ErrorCode::kCorruption);

    auto invalid_typmod = encoded.value();
    write_u32(invalid_typmod, second_column_offset + 8U, 7U);
    reseal(invalid_typmod);
    expect_open_error(path, invalid_typmod, base::ErrorCode::kCorruption);

    auto invalid_fillfactor = encoded.value();
    write_u8(invalid_fillfactor, table_offset + 18U, 9U);
    reseal(invalid_fillfactor);
    expect_open_error(path, invalid_fillfactor, base::ErrorCode::kCorruption);

    auto invalid_index_flags = encoded.value();
    write_u8(invalid_index_flags, index_offset + 32U, 2U);
    reseal(invalid_index_flags);
    expect_open_error(path, invalid_index_flags, base::ErrorCode::kCorruption);

    auto invalid_analyzed_xid = encoded.value();
    write_u32(invalid_analyzed_xid, statistics_offset + 40U, 0U);
    reseal(invalid_analyzed_xid);
    expect_open_error(path, invalid_analyzed_xid, base::ErrorCode::kCorruption);

    auto limited = options();
    limited.decode.maximum_image_size = encoded->size() - 1U;
    expect_open_error(path, *encoded, base::ErrorCode::kResourceExhausted, limited);
}

}  // namespace
}  // namespace db::catalog
