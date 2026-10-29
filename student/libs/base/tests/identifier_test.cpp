#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"

namespace db::base {
namespace {

template <typename Left, typename Right>
concept EqualityComparableWith = requires(const Left left, const Right right) {
    { left == right } -> std::same_as<bool>;
};

template <typename Left, typename Right>
concept AddableWith = requires(const Left left, const Right right) { left + right; };

template <typename Value>
concept LessThanComparable = requires(const Value left, const Value right) {
    { left < right } -> std::same_as<bool>;
};

TEST(IdentifierTest, KeepsDomainsAndRawRepresentationsDistinct) {
    static_assert(!std::is_convertible_v<std::uint32_t, PageId>);
    static_assert(!std::is_convertible_v<PageId, std::uint32_t>);
    static_assert(!std::is_constructible_v<PageId, RelationId>);
    static_assert(!EqualityComparableWith<PageId, RelationId>);
    static_assert(!EqualityComparableWith<Oid, RelationId>);
    static_assert(!EqualityComparableWith<RelationId, IndexId>);
    static_assert(!AddableWith<PageId, std::uint32_t>);

    constexpr PageId page_id{7};
    constexpr FrameId frame_id{8};
    constexpr Oid oid{9};
    constexpr RelationId relation_id{10};
    constexpr IndexId index_id{11};
    constexpr SlotId slot_id{12};
    constexpr Lsn lsn{13};
    constexpr Xid xid{14};
    constexpr TypeId type_id{15};

    EXPECT_EQ(page_id.value(), 7U);
    EXPECT_EQ(frame_id.value(), 8U);
    EXPECT_EQ(oid.value(), 9U);
    EXPECT_EQ(relation_id.value(), 10U);
    EXPECT_EQ(index_id.value(), 11U);
    EXPECT_EQ(slot_id.value(), 12U);
    EXPECT_EQ(lsn.value(), 13U);
    EXPECT_EQ(xid.value(), 14U);
    EXPECT_EQ(type_id.value(), 15U);
}

TEST(IdentifierTest, OrdersOnlyTypesWithOrdinaryNumericOrder) {
    static_assert(std::three_way_comparable<PageId>);
    static_assert(std::three_way_comparable<Lsn>);
    static_assert(!LessThanComparable<Xid>);

    EXPECT_LT(PageId{2}, PageId{3});
    EXPECT_GT(FrameId{5}, FrameId{4});
    EXPECT_LT(Oid{5}, Oid{6});
    EXPECT_EQ(RelationId{6}, RelationId{6});
    EXPECT_LT(Lsn{99}, Lsn{100});
    EXPECT_EQ(Xid{42}, Xid{42});
    EXPECT_NE(Xid{42}, Xid{43});
}

TEST(IdentifierTest, ExposesExplicitInvalidSentinels) {
    EXPECT_EQ(kInvalidPageId.value(), std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(kInvalidFrameId.value(), std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(kInvalidOid.value(), 0U);
    EXPECT_EQ(kInvalidRelationId.value(), 0U);
    EXPECT_EQ(kInvalidIndexId.value(), 0U);
    EXPECT_EQ(kInvalidSlotId.value(), std::numeric_limits<std::uint16_t>::max());
    EXPECT_EQ(kInvalidLsn.value(), 0U);
    EXPECT_EQ(kInvalidXid.value(), 0U);
    EXPECT_EQ(kInvalidRid.page_id(), kInvalidPageId);
    EXPECT_EQ(kInvalidRid.slot_id(), kInvalidSlotId);
}

TEST(IdentifierTest, ComposesAndOrdersRecordIdentifiers) {
    constexpr Rid first{PageId{2}, SlotId{3}};
    constexpr Rid same{PageId{2}, SlotId{3}};
    constexpr Rid later_slot{PageId{2}, SlotId{4}};
    constexpr Rid later_page{PageId{3}, SlotId{0}};

    EXPECT_EQ(first.page_id(), PageId{2});
    EXPECT_EQ(first.slot_id(), SlotId{3});
    EXPECT_EQ(first, same);
    EXPECT_LT(first, later_slot);
    EXPECT_LT(later_slot, later_page);
}

TEST(IdentifierTest, SupportsStandardHashContainers) {
    const std::unordered_set<PageId> pages{PageId{3}, PageId{7}};
    const std::unordered_set<Oid> objects{Oid{2}, Oid{5}};
    const std::unordered_set<Rid> records{
        Rid{PageId{3}, SlotId{1}},
        Rid{PageId{7}, SlotId{2}},
    };

    EXPECT_TRUE(pages.contains(PageId{3}));
    EXPECT_FALSE(pages.contains(PageId{4}));
    EXPECT_TRUE(objects.contains(Oid{5}));
    EXPECT_FALSE(objects.contains(Oid{6}));
    EXPECT_TRUE(records.contains(Rid{PageId{7}, SlotId{2}}));
    EXPECT_FALSE(records.contains(Rid{PageId{7}, SlotId{3}}));
}

}  // namespace
}  // namespace db::base
