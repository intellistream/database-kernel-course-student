#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/metadata.hpp"
#include "db/types/data_type.hpp"

namespace db::catalog {
namespace {

auto example_schema() -> Schema {
    auto schema = Schema::create(
        std::vector<Column>{
            Column{.name = "id", .data_type = types::kInt64Type},
            Column{
                .name = "name",
                .data_type =
                    types::DataType{
                        types::kCharTypeId,
                        types::TypeModifier{64},
                    }
            },
        },
        RowSizeLimit{.maximum_encoded_size = 128}
    );
    EXPECT_TRUE(schema) << schema.error().to_string();
    return std::move(schema).value();
}

TEST(SchemaTest, SharesImmutableColumnsAndResolvesFirstPosition) {
    const Schema schema = example_schema();
    Schema copy;
    copy = schema;

    ASSERT_EQ(schema.size(), 2U);
    EXPECT_FALSE(schema.empty());
    EXPECT_EQ(schema.columns().data(), copy.columns().data());
    EXPECT_EQ(schema.columns()[0].name, "id");
    EXPECT_EQ(schema.columns()[1].data_type.modifier(), types::TypeModifier{64});
    EXPECT_EQ(schema.position("id"), 0U);
    EXPECT_EQ(schema.position("name"), 1U);
    EXPECT_FALSE(schema.position("missing"));
    EXPECT_EQ(schema, copy);

    Schema moved = std::move(copy);
    EXPECT_EQ(moved.columns().data(), schema.columns().data());
    // Schema's documented move contract restores shared immutable backing in the source.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(copy.columns().data(), schema.columns().data());

    const Schema empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.size(), 0U);
}

TEST(TableMetadataTest, KeepsLogicalAndPhysicalIdentityDistinct) {
    const Schema schema = example_schema();
    auto metadata = TableMetadata::create(
        TableMetadataDefinition{
            .oid = base::Oid{7},
            .relation_id = base::RelationId{31},
            .name = "accounts",
            .schema = schema,
            .fillfactor = 75,
        }
    );
    ASSERT_TRUE(metadata) << metadata.error().to_string();

    EXPECT_EQ(metadata->oid(), base::Oid{7});
    EXPECT_EQ(metadata->relation_id(), base::RelationId{31});
    EXPECT_EQ(metadata->name(), "accounts");
    EXPECT_EQ(metadata->schema(), schema);
    EXPECT_EQ(metadata->schema().columns().data(), schema.columns().data());
    EXPECT_EQ(metadata->fillfactor(), 75U);
}

TEST(TableMetadataTest, RejectsInvalidIdentityDomains) {
    auto invalid_oid = TableMetadata::create(
        TableMetadataDefinition{
            .oid = base::kInvalidOid,
            .relation_id = base::RelationId{1},
            .name = "bad_oid",
            .schema = Schema{},
        }
    );
    ASSERT_FALSE(invalid_oid);
    EXPECT_EQ(invalid_oid.error().code(), base::ErrorCode::kInvalidArgument);

    auto invalid_relation = TableMetadata::create(
        TableMetadataDefinition{
            .oid = base::Oid{1},
            .relation_id = base::kInvalidRelationId,
            .name = "bad_relation",
            .schema = Schema{},
        }
    );
    ASSERT_FALSE(invalid_relation);
    EXPECT_EQ(invalid_relation.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(IndexMetadataTest, PreservesOwnerOrderedColumnsAndUniqueness) {
    const auto signature = index_schema_signature(
        std::vector<types::DataType>{
            types::DataType{types::kCharTypeId, types::TypeModifier{64}},
            types::kInt64Type,
        }
    );
    ASSERT_TRUE(signature);
    auto metadata = IndexMetadata::create(
        IndexMetadataDefinition{
            .oid = base::Oid{9},
            .index_id = base::IndexId{4},
            .relation_id = base::RelationId{32},
            .table_oid = base::Oid{7},
            .name = "accounts_name_id_idx",
            .column_names = {"name", "id"},
            .schema_signature = signature.value(),
            .unique = true,
        }
    );
    ASSERT_TRUE(metadata) << metadata.error().to_string();

    EXPECT_EQ(metadata->oid(), base::Oid{9});
    EXPECT_EQ(metadata->index_id(), base::IndexId{4});
    EXPECT_EQ(metadata->relation_id(), base::RelationId{32});
    EXPECT_EQ(metadata->table_oid(), base::Oid{7});
    EXPECT_EQ(metadata->name(), "accounts_name_id_idx");
    ASSERT_EQ(metadata->column_names().size(), 2U);
    EXPECT_EQ(metadata->column_names()[0], "name");
    EXPECT_EQ(metadata->column_names()[1], "id");
    EXPECT_EQ(metadata->schema_signature(), signature.value());
    EXPECT_TRUE(metadata->unique());
}

TEST(IndexMetadataTest, ResolvesOrderedTypesAndStableBklOneSignature) {
    const auto schema = example_schema();
    const std::vector<std::string> names{"name", "id", "name"};
    const auto resolved = resolve_index_schema(schema, names);
    ASSERT_TRUE(resolved) << resolved.error().to_string();
    EXPECT_EQ(resolved->column_positions, (std::vector<std::size_t>{1U, 0U, 1U}));
    EXPECT_EQ(
        resolved->column_types,
        (std::vector<types::DataType>{
            types::DataType{types::kCharTypeId, types::TypeModifier{64}},
            types::kInt64Type,
            types::DataType{types::kCharTypeId, types::TypeModifier{64}},
        })
    );
    EXPECT_EQ(resolved->signature.value(), 0x15e3'adf8'c659'660cULL);

    const std::vector<std::string> missing{"missing"};
    const auto rejected = resolve_index_schema(schema, missing);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kNotFound);
    EXPECT_EQ(
        index_schema_signature(std::span<const types::DataType>{}).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    const std::vector<types::DataType> unbounded{types::kCharType};
    EXPECT_EQ(index_schema_signature(unbounded).error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(IndexMetadataTest, RejectsEveryInvalidIdentityDomain) {
    auto definition = IndexMetadataDefinition{
        .oid = base::Oid{9},
        .index_id = base::IndexId{4},
        .relation_id = base::RelationId{32},
        .table_oid = base::Oid{7},
        .name = "idx",
        .column_names = {"id"},
        .schema_signature = IndexSchemaSignature{1U},
    };

    definition.oid = base::kInvalidOid;
    EXPECT_EQ(IndexMetadata::create(definition).error().code(), base::ErrorCode::kInvalidArgument);
    definition.oid = base::Oid{9};
    definition.index_id = base::kInvalidIndexId;
    EXPECT_EQ(IndexMetadata::create(definition).error().code(), base::ErrorCode::kInvalidArgument);
    definition.index_id = base::IndexId{4};
    definition.relation_id = base::kInvalidRelationId;
    EXPECT_EQ(IndexMetadata::create(definition).error().code(), base::ErrorCode::kInvalidArgument);
    definition.relation_id = base::RelationId{32};
    definition.table_oid = base::kInvalidOid;
    EXPECT_EQ(IndexMetadata::create(definition).error().code(), base::ErrorCode::kInvalidArgument);

    definition.table_oid = base::Oid{7};
    definition.schema_signature = kInvalidIndexSchemaSignature;
    EXPECT_EQ(IndexMetadata::create(definition).error().code(), base::ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace db::catalog
