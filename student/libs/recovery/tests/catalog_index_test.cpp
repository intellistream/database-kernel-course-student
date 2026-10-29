#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_relation.hpp"
#include "db/access/btree_wal.hpp"
#include "db/base/error.hpp"
#include "db/catalog/consistency.hpp"
#include "db/catalog/index_schema.hpp"
#include "db/recovery/catalog_index.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/durability.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"

namespace db::recovery {
namespace {

class TestLog final : public access::BtreeMutationLogPort {
public:
    auto log(const access::BtreeWalMutation& /*mutation*/) -> base::Result<base::Lsn> override {
        return base::Lsn{next_.fetch_add(1U, std::memory_order_relaxed)};
    }

private:
    std::atomic<std::uint64_t> next_{1U};
};

class ImmediateDurability final : public storage::PageDurabilityPort {
public:
    auto flush_through(base::Lsn /*target_lsn*/) -> base::Status override {
        return {};
    }
};

auto make_pool(const test_support::TestDirectory& directory)
    -> base::Result<std::unique_ptr<storage::BufferPool>> {
    auto disk = storage::FileDiskManager::open(directory.join("relations"));
    if (!disk) {
        return base::fail(std::move(disk).error());
    }
    return storage::BufferPool::create(
        8U,
        std::move(disk).value(),
        nullptr,
        std::make_unique<ImmediateDurability>()
    );
}

auto request(const base::RelationId relation_id, const std::vector<types::DataType>& types)
    -> catalog::IndexRelationValidationRequest {
    return catalog::IndexRelationValidationRequest{
        .relation_id = relation_id,
        .column_types = types,
        .schema_signature = catalog::index_schema_signature(types).value(),
        .unique = true,
    };
}

TEST(BtreeCatalogValidationAdapterTest, BindsResidentMetapageToCatalogLayoutOnce) {
    auto directory = test_support::TestDirectory::create("catalog-index-validation");
    ASSERT_TRUE(directory);
    auto pool = make_pool(directory.value());
    ASSERT_TRUE(pool) << pool.error().to_string();
    TestLog log;
    const auto relation_id = base::RelationId{91U};
    const std::vector<types::DataType> key_types{
        types::DataType{types::kCharTypeId, types::TypeModifier{12}},
        types::kInt64Type,
    };
    auto layout = access::BtreeKeyLayout::create(key_types);
    ASSERT_TRUE(layout);
    ASSERT_TRUE(
        access::BtreeRelation::create(
            relation_id,
            *pool.value(),
            std::move(layout).value(),
            true,
            log
        )
    );

    BtreeCatalogValidationAdapter adapter{*pool.value(), log};
    EXPECT_TRUE(adapter.validate(request(relation_id, key_types)));

    const std::vector<types::DataType> incompatible{types::kBoolType, types::kInt64Type};
    const auto mismatch = adapter.validate(request(relation_id, incompatible));
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().code(), base::ErrorCode::kInvalidArgument);

    const auto missing = adapter.validate(request(base::RelationId{92U}, key_types));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kNotFound);
}

TEST(BtreeCatalogValidationAdapterTest, RejectsCrossComponentSignatureDisagreement) {
    auto directory = test_support::TestDirectory::create("catalog-index-signature-disagreement");
    ASSERT_TRUE(directory);
    auto pool = make_pool(directory.value());
    ASSERT_TRUE(pool);
    TestLog log;
    BtreeCatalogValidationAdapter adapter{*pool.value(), log};
    const std::vector<types::DataType> key_types{types::kInt64Type};
    auto invalid = request(base::RelationId{93U}, key_types);
    invalid.schema_signature = catalog::IndexSchemaSignature{1U};

    const auto status = adapter.validate(invalid);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code(), base::ErrorCode::kInvariantViolation);
}

}  // namespace
}  // namespace db::recovery
