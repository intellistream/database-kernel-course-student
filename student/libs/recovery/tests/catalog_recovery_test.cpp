#include <cstdint>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/catalog/store.hpp"
#include "db/recovery/catalog_recovery.hpp"
#include "db/test_support/test_directory.hpp"

namespace db::recovery {
namespace {

constexpr catalog::RowSizeLimit kRowLimit{.maximum_encoded_size = 4'036U};

auto catalog_options() -> catalog::CatalogStoreOptions {
    return catalog::CatalogStoreOptions{
        .decode = catalog::CatalogDecodeOptions{.row_size_limit = kRowLimit},
    };
}

TEST(CatalogStoreRecoveryAdapterTest, ReconcilesOnlyTheDurableOidFrontier) {
    auto root = test_support::TestDirectory::create("recovery-catalog-oid-frontier");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto path = root->join("catalog/main.cat");
    auto store = catalog::CatalogStore::create(path, catalog_options());
    ASSERT_TRUE(store) << store.error().to_string();
    CatalogStoreRecoveryAdapter adapter{store.value()};

    ASSERT_TRUE(adapter.ensure_next_object_id(41U));
    EXPECT_EQ(store->generation().value(), 2U);
    EXPECT_EQ(store->id_state()->next_oid, 41U);
    EXPECT_EQ(store->id_state()->next_relation_id, 1U);
    EXPECT_EQ(store->id_state()->next_index_id, 1U);

    ASSERT_TRUE(adapter.ensure_next_object_id(9U));
    EXPECT_EQ(store->generation().value(), 2U);
    auto reopened = catalog::CatalogStore::open(path, catalog_options());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->generation().value(), 2U);
    EXPECT_EQ(reopened->id_state()->next_oid, 41U);

    auto invalid = adapter.ensure_next_object_id(0U);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace db::recovery
