#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"

#include "id_allocator.hpp"

namespace db::catalog {
namespace {

TEST(CatalogIdAllocatorTest, AllocatesIndependentMonotonicTypedDomains) {
    static_assert(!std::is_copy_constructible_v<CatalogIdAllocator>);
    static_assert(!std::is_copy_assignable_v<CatalogIdAllocator>);

    CatalogIdAllocator allocator;

    EXPECT_EQ(allocator.allocate_oid().value(), base::Oid{1});
    EXPECT_EQ(allocator.allocate_relation_id().value(), base::RelationId{1});
    EXPECT_EQ(allocator.allocate_index_id().value(), base::IndexId{1});
    EXPECT_EQ(allocator.allocate_oid().value(), base::Oid{2});
    EXPECT_EQ(allocator.allocate_relation_id().value(), base::RelationId{2});
    EXPECT_EQ(allocator.allocate_index_id().value(), base::IndexId{2});
    EXPECT_EQ(
        allocator.state(),
        (CatalogIdState{.next_oid = 3, .next_relation_id = 3, .next_index_id = 3})
    );
}

TEST(CatalogIdAllocatorTest, MoveTransfersTheOnlyLiveAllocationStream) {
    CatalogIdAllocator source;
    EXPECT_EQ(source.allocate_oid().value(), base::Oid{1});

    CatalogIdAllocator destination = std::move(source);
    EXPECT_EQ(destination.allocate_oid().value(), base::Oid{2});
    // CatalogIdAllocator documents its moved-from state as exhausted so it cannot fork IDs.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    const auto source_after_move = source.allocate_oid();
    ASSERT_FALSE(source_after_move);
    EXPECT_EQ(source_after_move.error().code(), base::ErrorCode::kResourceExhausted);
}

TEST(CatalogIdAllocatorTest, RestoresExactDurableFrontiers) {
    const auto state = CatalogIdState{
        .next_oid = 41,
        .next_relation_id = 101,
        .next_index_id = 17,
    };
    auto allocator = CatalogIdAllocator::restore(state);
    ASSERT_TRUE(allocator) << allocator.error().to_string();

    EXPECT_EQ(allocator->state(), state);
    EXPECT_EQ(allocator->allocate_oid().value(), base::Oid{41});
    EXPECT_EQ(allocator->allocate_relation_id().value(), base::RelationId{101});
    EXPECT_EQ(allocator->allocate_index_id().value(), base::IndexId{17});
    EXPECT_EQ(
        allocator->state(),
        (CatalogIdState{.next_oid = 42, .next_relation_id = 102, .next_index_id = 18})
    );
}

TEST(CatalogIdAllocatorTest, RejectsInvalidRestoredFrontiers) {
    EXPECT_EQ(
        CatalogIdAllocator::restore(CatalogIdState{.next_oid = 0}).error().code(),
        base::ErrorCode::kInvalidArgument
    );

    constexpr auto beyond_exhausted =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 2U;
    EXPECT_EQ(
        CatalogIdAllocator::restore(CatalogIdState{.next_index_id = beyond_exhausted})
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
}

TEST(CatalogIdAllocatorTest, ExhaustsWithoutWrappingOrChangingOtherDomains) {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    constexpr auto exhausted = static_cast<std::uint64_t>(maximum) + 1U;
    auto allocator = CatalogIdAllocator::restore(
        CatalogIdState{
            .next_oid = maximum,
            .next_relation_id = 9,
            .next_index_id = 12,
        }
    );
    ASSERT_TRUE(allocator) << allocator.error().to_string();

    EXPECT_EQ(allocator->allocate_oid().value(), base::Oid{maximum});
    const auto failed = allocator->allocate_oid();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ(
        allocator->state(),
        (CatalogIdState{
            .next_oid = exhausted,
            .next_relation_id = 9,
            .next_index_id = 12,
        })
    );

    EXPECT_EQ(allocator->allocate_relation_id().value(), base::RelationId{9});
    EXPECT_EQ(allocator->allocate_index_id().value(), base::IndexId{12});
}

}  // namespace
}  // namespace db::catalog
