#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_key.hpp"
#include "db/access/heap_page.hpp"
#include "db/base/error.hpp"
#include "db/catalog/index_schema.hpp"
#include "db/catalog/schema.hpp"
#include "db/types/data_type.hpp"

namespace db::catalog {
namespace {

TEST(SchemaAccessContractTest, UsesTheIndependentHeapPayloadLimitAtComposition) {
    constexpr auto row_limit = RowSizeLimit{
        .maximum_encoded_size = access::kMaximumHeapRowSize,
    };
    constexpr auto maximum_single_char =
        static_cast<std::int32_t>(access::kMaximumHeapRowSize - 1U);

    auto exact = Schema::create(
        std::vector<Column>{Column{
            .name = "payload",
            .data_type =
                types::DataType{
                    types::kCharTypeId,
                    types::TypeModifier{maximum_single_char},
                }
        }},
        row_limit
    );
    ASSERT_TRUE(exact) << exact.error().to_string();
    EXPECT_EQ(exact->null_bitmap_size(), 1U);
    EXPECT_EQ(exact->encoded_row_size(), access::kMaximumHeapRowSize);

    const auto too_large = Schema::create(
        std::vector<Column>{Column{
            .name = "payload",
            .data_type =
                types::DataType{
                    types::kCharTypeId,
                    types::TypeModifier{maximum_single_char + 1},
                }
        }},
        row_limit
    );
    ASSERT_FALSE(too_large);
    EXPECT_EQ(too_large.error().code(), base::ErrorCode::kResourceExhausted);
}

TEST(SchemaAccessContractTest, CatalogAndBtreeAgreeOnBklOneSignature) {
    const auto schema = Schema::create(
        std::vector<Column>{
            Column{.name = "id", .data_type = types::kInt64Type},
            Column{
                .name = "code",
                .data_type = types::DataType{types::kCharTypeId, types::TypeModifier{12}},
            },
            Column{
                .name = "created_at",
                .data_type =
                    types::DataType{
                        types::kTimestampTypeId,
                        types::TypeModifier{3},
                    },
            },
        },
        RowSizeLimit{.maximum_encoded_size = access::kMaximumHeapRowSize}
    );
    ASSERT_TRUE(schema) << schema.error().to_string();
    const std::vector<std::string> key_names{"code", "id", "created_at"};
    const auto resolved = resolve_index_schema(schema.value(), key_names);
    ASSERT_TRUE(resolved) << resolved.error().to_string();
    const auto layout = access::BtreeKeyLayout::create(resolved->column_types);
    ASSERT_TRUE(layout) << layout.error().to_string();
    EXPECT_EQ(resolved->signature.value(), layout->type_signature());

    const std::vector<std::string> reordered_names{"id", "code", "created_at"};
    const auto reordered = resolve_index_schema(schema.value(), reordered_names);
    ASSERT_TRUE(reordered);
    EXPECT_NE(reordered->signature, resolved->signature);
}

}  // namespace
}  // namespace db::catalog
