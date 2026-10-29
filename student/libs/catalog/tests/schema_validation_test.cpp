#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/schema.hpp"
#include "db/types/data_type.hpp"

namespace db::catalog {
namespace {

TEST(SchemaValidationTest, ComputesNullBitmapAndExactEncodedRowWidth) {
    auto schema = Schema::create(
        std::vector<Column>{
            Column{.name = "enabled", .data_type = types::kBoolType},
            Column{.name = "id", .data_type = types::kInt64Type},
            Column{.name = "ratio", .data_type = types::kFloat32Type},
            Column{
                .name = "tag",
                .data_type =
                    types::DataType{
                        types::kCharTypeId,
                        types::TypeModifier{5},
                    }
            },
            Column{.name = "created_at", .data_type = types::kTimestampType},
        },
        RowSizeLimit{.maximum_encoded_size = 27}
    );
    ASSERT_TRUE(schema) << schema.error().to_string();

    EXPECT_EQ(schema->null_bitmap_size(), 1U);
    EXPECT_EQ(schema->encoded_row_size(), 1U + 1U + 8U + 4U + 5U + 8U);
    EXPECT_TRUE(schema->table_constraints().empty());
}

TEST(SchemaValidationTest, CountsEveryEightColumnsInTheBitmap) {
    std::vector<Column> eight_columns;
    std::vector<Column> nine_columns;
    for (std::size_t index = 0; index < 9U; ++index) {
        auto column = Column{
            .name = "c" + std::to_string(index),
            .data_type = types::kBoolType,
        };
        nine_columns.push_back(column);
        if (index < 8U) {
            eight_columns.push_back(std::move(column));
        }
    }

    auto eight = Schema::create(std::move(eight_columns), RowSizeLimit{.maximum_encoded_size = 9});
    ASSERT_TRUE(eight) << eight.error().to_string();
    EXPECT_EQ(eight->null_bitmap_size(), 1U);
    EXPECT_EQ(eight->encoded_row_size(), 9U);

    const auto nine =
        Schema::create(std::move(nine_columns), RowSizeLimit{.maximum_encoded_size = 10});
    ASSERT_FALSE(nine);
    EXPECT_EQ(nine.error().code(), base::ErrorCode::kResourceExhausted);
}

TEST(SchemaValidationTest, AcceptsTheExactRowLimitAndRejectsOneByteMore) {
    auto exact = Schema::create(
        std::vector<Column>{Column{
            .name = "payload",
            .data_type =
                types::DataType{
                    types::kCharTypeId,
                    types::TypeModifier{9},
                }
        }},
        RowSizeLimit{.maximum_encoded_size = 10}
    );
    ASSERT_TRUE(exact) << exact.error().to_string();
    EXPECT_EQ(exact->encoded_row_size(), 10U);

    const auto oversized = Schema::create(
        std::vector<Column>{Column{
            .name = "payload",
            .data_type =
                types::DataType{
                    types::kCharTypeId,
                    types::TypeModifier{10},
                }
        }},
        RowSizeLimit{.maximum_encoded_size = 10}
    );
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error().code(), base::ErrorCode::kResourceExhausted);

    EXPECT_TRUE(Schema::create({}, RowSizeLimit{.maximum_encoded_size = 0}));
    EXPECT_EQ(
        Schema::create(
            std::vector<Column>{
                Column{.name = "enabled", .data_type = types::kBoolType},
            },
            RowSizeLimit{.maximum_encoded_size = 0}
        )
            .error()
            .code(),
        base::ErrorCode::kResourceExhausted
    );
}

TEST(SchemaValidationTest, RejectsEmptyDuplicateOversizedAndInvalidUtf8Names) {
    EXPECT_EQ(
        Schema::create(
            std::vector<Column>{Column{.name = "", .data_type = types::kInt64Type}},
            RowSizeLimit{.maximum_encoded_size = 64}
        )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        Schema::create(
            std::vector<Column>{
                Column{.name = "same", .data_type = types::kInt64Type},
                Column{.name = "same", .data_type = types::kBoolType},
            },
            RowSizeLimit{.maximum_encoded_size = 64}
        )
            .error()
            .code(),
        base::ErrorCode::kAlreadyExists
    );

    std::string exact_name(kMaximumCatalogNameBytes, 'n');
    auto exact = Schema::create(
        std::vector<Column>{Column{.name = exact_name, .data_type = types::kBoolType}},
        RowSizeLimit{.maximum_encoded_size = 2}
    );
    ASSERT_TRUE(exact) << exact.error().to_string();
    EXPECT_EQ(exact->columns()[0].name.size(), kMaximumCatalogNameBytes);

    exact_name.push_back('x');
    EXPECT_EQ(
        Schema::create(
            std::vector<Column>{Column{
                .name = std::move(exact_name),
                .data_type = types::kBoolType,
            }},
            RowSizeLimit{.maximum_encoded_size = 2}
        )
            .error()
            .code(),
        base::ErrorCode::kOutOfRange
    );

    constexpr std::array<std::string_view, 5> invalid_utf8_names{
        std::string_view{"\xc0\xaf", 2U},
        std::string_view{"\xed\xa0\x80", 3U},
        std::string_view{"\xf4\x90\x80\x80", 4U},
        std::string_view{"\x80", 1U},
        std::string_view{"\xe2\x82", 2U},
    };
    for (const auto invalid_utf8 : invalid_utf8_names) {
        auto invalid = Schema::create(
            std::vector<Column>{Column{
                .name = std::string{invalid_utf8},
                .data_type = types::kBoolType,
            }},
            RowSizeLimit{.maximum_encoded_size = 2}
        );
        ASSERT_FALSE(invalid);
        EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);
    }

    auto multibyte = Schema::create(
        std::vector<Column>{Column{
            .name = std::string{"列名"},
            .data_type = types::kBoolType,
        }},
        RowSizeLimit{.maximum_encoded_size = 2}
    );
    ASSERT_TRUE(multibyte) << multibyte.error().to_string();
}

TEST(SchemaValidationTest, RejectsUnknownUnstorableAndInvalidTypeModifiers) {
    const std::vector<types::DataType> rejected{
        types::kUnknownType,
        types::DataType{base::TypeId{999}},
        types::DataType{types::kBoolTypeId, types::TypeModifier{0}},
        types::kCharType,
        types::DataType{types::kCharTypeId, types::TypeModifier{0}},
        types::DataType{
            types::kCharTypeId,
            types::TypeModifier{static_cast<std::int32_t>(types::kMaxCharLength + 1U)},
        },
        types::DataType{types::kTimestampTypeId, types::TypeModifier{7}},
    };

    for (const auto data_type : rejected) {
        auto result = Schema::create(
            std::vector<Column>{Column{.name = "value", .data_type = data_type}},
            RowSizeLimit{.maximum_encoded_size = 4'096}
        );
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code(), base::ErrorCode::kInvalidArgument);
        EXPECT_EQ(result.error().contexts().size(), 1U);
    }
}

TEST(SchemaValidationTest, RejectsUnsupportedColumnAndTableConstraints) {
    constexpr std::array k_column_constraints{
        ColumnConstraintKind::kNotNull,
        ColumnConstraintKind::kUnique,
        ColumnConstraintKind::kPrimaryKey,
        ColumnConstraintKind::kCheck,
        ColumnConstraintKind::kDefaultValue,
        ColumnConstraintKind::kGenerated,
        ColumnConstraintKind::kForeignKey,
    };
    for (const auto constraint : k_column_constraints) {
        auto result = Schema::create(
            std::vector<Column>{Column{.name = "value", .data_type = types::kInt64Type}},
            RowSizeLimit{.maximum_encoded_size = 64},
            {ColumnConstraint{.column_name = "value", .kind = constraint}}
        );
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code(), base::ErrorCode::kUnsupported);
        EXPECT_FALSE(column_constraint_name(constraint).empty());
    }

    constexpr std::array k_table_constraints{
        TableConstraintKind::kUnique,
        TableConstraintKind::kPrimaryKey,
        TableConstraintKind::kCheck,
        TableConstraintKind::kForeignKey,
    };
    for (const auto constraint : k_table_constraints) {
        auto result = Schema::create(
            std::vector<Column>{Column{.name = "value", .data_type = types::kInt64Type}},
            RowSizeLimit{.maximum_encoded_size = 64},
            {},
            {constraint}
        );
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code(), base::ErrorCode::kUnsupported);
        EXPECT_FALSE(table_constraint_name(constraint).empty());
    }
}

TEST(MetadataValidationTest, ValidatesNamesFillfactorAndIndexShape) {
    const auto schema = Schema::create(
                            std::vector<Column>{
                                Column{.name = "id", .data_type = types::kInt64Type},
                            },
                            RowSizeLimit{.maximum_encoded_size = 64}
    )
                            .value();
    for (const auto fillfactor : {kMinimumHeapFillfactor, std::uint8_t{100}}) {
        auto table = TableMetadata::create(
            TableMetadataDefinition{
                .oid = base::Oid{1},
                .relation_id = base::RelationId{2},
                .name = "items",
                .schema = schema,
                .fillfactor = fillfactor,
            }
        );
        ASSERT_TRUE(table) << table.error().to_string();
    }
    for (const auto fillfactor : {std::uint8_t{9}, std::uint8_t{101}}) {
        EXPECT_EQ(
            TableMetadata::create(
                TableMetadataDefinition{
                    .oid = base::Oid{1},
                    .relation_id = base::RelationId{2},
                    .name = "items",
                    .schema = schema,
                    .fillfactor = fillfactor,
                }
            )
                .error()
                .code(),
            base::ErrorCode::kInvalidArgument
        );
    }

    EXPECT_EQ(
        TableMetadata::create(
            TableMetadataDefinition{
                .oid = base::Oid{1},
                .relation_id = base::RelationId{2},
                .name = "",
                .schema = schema,
            }
        )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        IndexMetadata::create(
            IndexMetadataDefinition{
                .oid = base::Oid{3},
                .index_id = base::IndexId{4},
                .relation_id = base::RelationId{5},
                .table_oid = base::Oid{1},
                .name = "items_idx",
                .column_names = {},
                .schema_signature = IndexSchemaSignature{1U},
            }
        )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        IndexMetadata::create(
            IndexMetadataDefinition{
                .oid = base::Oid{3},
                .index_id = base::IndexId{4},
                .relation_id = base::RelationId{5},
                .table_oid = base::Oid{1},
                .name = "items_idx",
                .column_names = {""},
                .schema_signature = IndexSchemaSignature{1U},
            }
        )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
}

}  // namespace
}  // namespace db::catalog
