#include <array>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/consistency.hpp"
#include "db/catalog/format.hpp"
#include "db/catalog/metadata.hpp"
#include "db/types/data_type.hpp"

#include "consistency_internal.hpp"
#include "format_internal.hpp"

namespace db::catalog {
namespace {

constexpr RowSizeLimit kRowLimit{.maximum_encoded_size = 4'036U};

auto table_schema() -> Schema {
    auto result = Schema::create(
        std::vector<Column>{
            Column{.name = "id", .data_type = types::kInt64Type},
            Column{
                .name = "label",
                .data_type = types::DataType{types::kCharTypeId, types::TypeModifier{8}},
            },
        },
        kRowLimit
    );
    EXPECT_TRUE(result) << result.error().to_string();
    return std::move(result).value();
}

auto make_index(
    const std::uint32_t oid,
    const std::uint32_t index_id,
    const std::uint32_t relation_id,
    std::string name,
    std::vector<std::string> columns,
    const Schema& schema
) -> IndexMetadata {
    const auto resolved = resolve_index_schema(schema, columns);
    EXPECT_TRUE(resolved) << resolved.error().to_string();
    auto result = IndexMetadata::create(
        IndexMetadataDefinition{
            .oid = base::Oid{oid},
            .index_id = base::IndexId{index_id},
            .relation_id = base::RelationId{relation_id},
            .table_oid = base::Oid{1U},
            .name = std::move(name),
            .column_names = std::move(columns),
            .schema_signature = resolved->signature,
            .unique = oid % 2U == 0U,
        }
    );
    EXPECT_TRUE(result) << result.error().to_string();
    return std::move(result).value();
}

auto image_with_indexes(const std::size_t count) -> CatalogImage {
    const auto schema = table_schema();
    auto table = TableMetadata::create(
        TableMetadataDefinition{
            .oid = base::Oid{1U},
            .relation_id = base::RelationId{1U},
            .name = "items",
            .schema = schema,
        }
    );
    EXPECT_TRUE(table);
    CatalogImage image{
        .generation = 9U,
        .id_state =
            CatalogIdState{
                .next_oid = count + 2U,
                .next_relation_id = count + 11U,
                .next_index_id = count + 1U,
            },
        .tables = {std::move(table).value()},
        .indexes = {},
    };
    for (std::size_t offset = count; offset > 0U; --offset) {
        image.indexes.push_back(make_index(
            static_cast<std::uint32_t>(offset + 1U),
            static_cast<std::uint32_t>(offset),
            static_cast<std::uint32_t>(offset + 10U),
            "items_" + std::to_string(offset) + "_idx",
            offset % 2U == 0U ? std::vector<std::string>{"label", "id"}
                              : std::vector<std::string>{"id"},
            schema
        ));
    }
    return image;
}

struct ObservedValidation final {
    base::RelationId relation_id;
    std::vector<types::DataType> column_types;
    IndexSchemaSignature signature;
    bool unique;
};

class ScriptedValidation final : public IndexRelationValidationPort {
public:
    auto validate(const IndexRelationValidationRequest request) -> base::Status override {
        observations.push_back(
            ObservedValidation{
                .relation_id = request.relation_id,
                .column_types =
                    std::vector<types::DataType>{
                        request.column_types.begin(),
                        request.column_types.end()
                    },
                .signature = request.schema_signature,
                .unique = request.unique,
            }
        );
        const auto scripted = errors.find(request.relation_id.value());
        if (scripted == errors.end()) {
            return {};
        }
        return base::fail(base::Error{scripted->second, "scripted physical open failure"});
    }

    std::unordered_map<std::uint32_t, base::ErrorCode> errors;
    std::vector<ObservedValidation> observations;
};

enum class ValidationException : std::uint8_t {
    kAllocation,
    kLength,
    kStandard,
    kUnknown,
};

class ThrowingValidation final : public IndexRelationValidationPort {
public:
    explicit ThrowingValidation(const ValidationException exception) noexcept
        : exception_{exception} {}

    auto validate(const IndexRelationValidationRequest request) -> base::Status override {
        static_cast<void>(request);
        switch (exception_) {
            case ValidationException::kAllocation:
                throw std::bad_alloc{};
            case ValidationException::kLength:
                throw std::length_error{"injected index validation length failure"};
            case ValidationException::kStandard:
                throw std::runtime_error{"injected index validation exception"};
            case ValidationException::kUnknown:
                throw 7;
        }
        std::terminate();
    }

private:
    ValidationException exception_;
};

TEST(CatalogConsistencyTest, AuditsInOidOrderAndClassifiesEveryRebuildCondition) {
    auto image = image_with_indexes(5U);
    ScriptedValidation validation;
    validation.errors = {
        {11U, base::ErrorCode::kNotFound},
        {12U, base::ErrorCode::kUnsupported},
        {13U, base::ErrorCode::kInvalidArgument},
        {14U, base::ErrorCode::kCorruption},
    };

    const auto diagnostics = audit_catalog_indexes(image, validation);
    ASSERT_TRUE(diagnostics) << diagnostics.error().to_string();
    ASSERT_EQ(validation.observations.size(), 5U);
    EXPECT_EQ(validation.observations[0].relation_id, base::RelationId{11U});
    EXPECT_EQ(validation.observations[1].column_types.size(), 2U);
    EXPECT_EQ(validation.observations[0].signature, image.indexes.back().schema_signature());
    ASSERT_EQ(diagnostics->size(), 4U);
    EXPECT_EQ((*diagnostics)[0].index.oid(), base::Oid{2U});
    EXPECT_EQ((*diagnostics)[0].reason, IndexRebuildReason::kMissingRelation);
    EXPECT_EQ((*diagnostics)[1].reason, IndexRebuildReason::kUnsupportedFormat);
    EXPECT_EQ((*diagnostics)[2].reason, IndexRebuildReason::kSchemaMismatch);
    EXPECT_EQ((*diagnostics)[3].reason, IndexRebuildReason::kCorruptRelation);
    EXPECT_EQ(index_rebuild_reason_name((*diagnostics)[3].reason), "corrupt_relation");

    const auto required = require_catalog_index_consistency(image, validation);
    ASSERT_FALSE(required);
    EXPECT_EQ(required.error().code(), base::ErrorCode::kRebuildRequired);
    EXPECT_NE(required.error().message().find("items_1_idx"), std::string::npos);
    EXPECT_NE(required.error().message().find("missing_relation"), std::string::npos);
}

TEST(CatalogConsistencyTest, PropagatesTransientPhysicalFailuresWithoutClaimingRebuild) {
    auto image = image_with_indexes(1U);
    ScriptedValidation validation;
    validation.errors.emplace(11U, base::ErrorCode::kIo);

    const auto diagnostics = audit_catalog_indexes(image, validation);
    ASSERT_FALSE(diagnostics);
    EXPECT_EQ(diagnostics.error().code(), base::ErrorCode::kIo);
    const auto required = require_catalog_index_consistency(image, validation);
    ASSERT_FALSE(required);
    EXPECT_EQ(required.error().code(), base::ErrorCode::kIo);
}

TEST(CatalogConsistencyTest, ConvertsValidationExceptionsWithoutClaimingRebuild) {
    constexpr std::array cases{
        std::pair{ValidationException::kAllocation, base::ErrorCode::kResourceExhausted},
        std::pair{ValidationException::kLength, base::ErrorCode::kResourceExhausted},
        std::pair{ValidationException::kStandard, base::ErrorCode::kInternal},
        std::pair{ValidationException::kUnknown, base::ErrorCode::kInternal},
    };
    const auto image = image_with_indexes(1U);

    for (const auto& [exception, expected_code] : cases) {
        ThrowingValidation validation{exception};
        auto diagnostics = audit_catalog_indexes(image, validation);
        ASSERT_FALSE(diagnostics);
        EXPECT_EQ(diagnostics.error().code(), expected_code);

        auto required = require_catalog_index_consistency(image, validation);
        ASSERT_FALSE(required);
        EXPECT_EQ(required.error().code(), expected_code);
        EXPECT_NE(required.error().code(), base::ErrorCode::kRebuildRequired);
    }
}

TEST(CatalogConsistencyTest, RejectsLogicalDamageBeforeOpeningPhysicalRelations) {
    auto image = image_with_indexes(1U);
    auto stale = IndexMetadata::create(
        IndexMetadataDefinition{
            .oid = image.indexes[0].oid(),
            .index_id = image.indexes[0].index_id(),
            .relation_id = image.indexes[0].relation_id(),
            .table_oid = image.indexes[0].table_oid(),
            .name = std::string{image.indexes[0].name()},
            .column_names = {"id"},
            .schema_signature = IndexSchemaSignature{1U},
            .unique = image.indexes[0].unique(),
        }
    );
    ASSERT_TRUE(stale);
    image.indexes[0] = std::move(stale).value();
    ScriptedValidation validation;

    const auto diagnostics = audit_catalog_indexes(image, validation);
    ASSERT_FALSE(diagnostics);
    EXPECT_EQ(diagnostics.error().code(), base::ErrorCode::kCorruption);
    EXPECT_TRUE(validation.observations.empty());
}

}  // namespace
}  // namespace db::catalog
