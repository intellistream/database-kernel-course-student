#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/storage/page.hpp"

namespace db::storage {
namespace {

constexpr PageAddress kAddress{base::RelationId{17}, base::PageId{3}};

TEST(PageAddressTest, OwnsValidityOrderingAndHashSemantics) {
    constexpr PageAddress next_page{base::RelationId{17}, base::PageId{4}};
    constexpr PageAddress next_relation{base::RelationId{18}, base::PageId{0}};
    EXPECT_TRUE(kAddress.valid());
    EXPECT_FALSE((PageAddress{base::kInvalidRelationId, base::PageId{0}}.valid()));
    EXPECT_FALSE((PageAddress{base::RelationId{1}, base::kInvalidPageId}.valid()));
    EXPECT_LT(kAddress, next_page);
    EXPECT_LT(next_page, next_relation);

    std::unordered_set<PageAddress> addresses;
    EXPECT_TRUE(addresses.insert(kAddress).second);
    EXPECT_FALSE(addresses.insert(kAddress).second);
    EXPECT_TRUE(addresses.insert(next_page).second);
}

TEST(PageHeaderTest, UsesExactVersionOneLittleEndianLayout) {
    const PageHeader header{
        .magic = kPageMagic,
        .version = kPageFormatVersion,
        .header_size = kPageHeaderSize,
        .checksum = 0x44332211U,
        .flags = 0x6655U,
        .kind = PageKind::kBtree,
        .lower = 0x8877U,
        .upper = 0xAA99U,
        .special = 0xCCBBU,
        .reserved = 0,
        .lsn = base::Lsn{0x0807060504030201ULL},
    };
    std::array<std::byte, kPageHeaderSize> encoded{};
    ASSERT_TRUE(header.encode(encoded));
    const std::array<std::byte, kPageHeaderSize> expected{
        std::byte{0x46}, std::byte{0x52}, std::byte{0x50}, std::byte{0x47}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x20}, std::byte{0x00}, std::byte{0x11}, std::byte{0x22},
        std::byte{0x33}, std::byte{0x44}, std::byte{0x55}, std::byte{0x66}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x77}, std::byte{0x88}, std::byte{0x99}, std::byte{0xAA},
        std::byte{0xBB}, std::byte{0xCC}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
        std::byte{0x07}, std::byte{0x08},
    };
    EXPECT_EQ(encoded, expected);
    EXPECT_EQ(PageHeader::decode(encoded).value(), header);
}

TEST(PageViewTest, AliasesWholePageStorageWithoutTakingOwnership) {
    auto page = Page::create(PageKind::kHeap, kAddress, 8U);
    ASSERT_TRUE(page);

    const PageView read = page.value().view();
    auto write = page.value().mutable_view();
    EXPECT_EQ(read.bytes().data(), page.value().bytes().data());
    EXPECT_EQ(write.bytes().data(), page.value().bytes().data());
    EXPECT_EQ(read.bytes().size(), kPageSize);

    auto marker = write.mutable_view(100U, 1U);
    ASSERT_TRUE(marker);
    marker.value().front() = std::byte{0xA5};
    EXPECT_EQ(read.view(100U, 1U).value().front(), std::byte{0xA5});
    EXPECT_EQ(write.read_only().view(100U, 1U).value().front(), std::byte{0xA5});
    EXPECT_EQ(read.free_space_view().value().size(), kPageSize - kPageHeaderSize - 8U);
    EXPECT_EQ(read.special_view().value().size(), 8U);
}

TEST(PageViewTest, HeaderFastPathsUseExactLittleEndianFieldOffsets) {
    auto page = Page::create(PageKind::kHeap, kAddress);
    ASSERT_TRUE(page);
    const Page::Storage before = page.value().bytes();

    auto write = page.value().mutable_view();
    write.set_flags(0xB2A1U);
    write.set_lsn(base::Lsn{0x8877665544332211ULL});

    const PageView read = page.value().view();
    EXPECT_EQ(read.flags(), 0xB2A1U);
    EXPECT_EQ(read.lsn(), base::Lsn{0x8877665544332211ULL});
    EXPECT_EQ(page.value().flags().value(), 0xB2A1U);
    EXPECT_EQ(page.value().lsn(), base::Lsn{0x8877665544332211ULL});

    EXPECT_EQ(page.value().bytes()[12U], std::byte{0xA1});
    EXPECT_EQ(page.value().bytes()[13U], std::byte{0xB2});
    const std::array<std::byte, 8U> expected_lsn{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0x44},
        std::byte{0x55},
        std::byte{0x66},
        std::byte{0x77},
        std::byte{0x88},
    };
    for (std::size_t index = 0U; index < expected_lsn.size(); ++index) {
        EXPECT_EQ(page.value().view(24U + index, 1U).value().front(), expected_lsn.at(index));
    }
    for (std::size_t index = 0U; index < kPageSize; ++index) {
        const bool is_flags = index == 12U || index == 13U;
        const bool is_lsn = index >= 24U && index < 32U;
        if (!is_flags && !is_lsn) {
            EXPECT_EQ(
                page.value().view(index, 1U).value().front(),
                base::Bytes{before}.subspan(index, 1U).front()
            );
        }
    }
}

TEST(PageTest, CreatesAZeroedFourKilobytePageWithSpecialSpace) {
    auto page = Page::create(PageKind::kHeap, kAddress, 64);
    ASSERT_TRUE(page);
    EXPECT_EQ(sizeof(page.value()), kPageSize);
    const auto header = page.value().header().value();
    EXPECT_EQ(header.magic, kPageMagic);
    EXPECT_EQ(header.version, kPageFormatVersion);
    EXPECT_EQ(header.kind, PageKind::kHeap);
    EXPECT_EQ(header.lower, kPageHeaderSize);
    EXPECT_EQ(header.upper, kPageSize - 64);
    EXPECT_EQ(header.special, kPageSize - 64);
    EXPECT_EQ(header.lsn, base::kInvalidLsn);
    EXPECT_EQ(page.value().free_space_view().value().size(), kPageSize - kPageHeaderSize - 64);
    EXPECT_EQ(page.value().special_view().value().size(), 64U);
}

TEST(PageTest, InitializesTheLegacyChecksumFieldToZero) {
    auto page = Page::create(PageKind::kHeap, kAddress);
    ASSERT_TRUE(page);
    EXPECT_EQ(page.value().header().value().checksum, 0U);
}

TEST(PageTest, AcceptsMutationsAndLegacyChecksumContentsWithoutRecalculation) {
    auto page = Page::create(PageKind::kMetadata, kAddress);
    ASSERT_TRUE(page);

    auto payload = page.value().mutable_view(100, 4);
    ASSERT_TRUE(payload);
    payload.value().front() = std::byte{0xA5};

    auto legacy = page.value().bytes();
    legacy[8] = std::byte{0x11};
    legacy[9] = std::byte{0x22};
    legacy[10] = std::byte{0x33};
    legacy[11] = std::byte{0x44};
    EXPECT_TRUE(Page::load(legacy, kAddress));
}

TEST(PageTest, RoundTripsStructurallyValidImages) {
    auto page = Page::create(PageKind::kBtree, kAddress, 32);
    ASSERT_TRUE(page);
    EXPECT_TRUE(page.value().set_lsn(base::Lsn{9001}));
    EXPECT_TRUE(page.value().set_flags(0x1234U));

    auto loaded = Page::load(page.value().bytes(), kAddress);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value().lsn(), base::Lsn{9001});
    EXPECT_EQ(loaded.value().flags().value(), 0x1234U);

    auto corrupt = page.value().bytes();
    corrupt.front() = std::byte{0xFF};
    EXPECT_TRUE(Page::load(corrupt, kAddress));
}

TEST(PageTest, ProjectsRawAndSpecialViews) {
    auto page = Page::create(PageKind::kHeap, kAddress, 8);
    ASSERT_TRUE(page);
    EXPECT_TRUE(page.value().view(0, kPageSize));
    EXPECT_TRUE(page.value().view(kPageSize, 0));

    auto special = page.value().mutable_special_view();
    ASSERT_TRUE(special);
    EXPECT_EQ(special.value().size(), 8U);
    special.value().back() = std::byte{0x7E};
    EXPECT_EQ(page.value().special_view().value().back(), std::byte{0x7E});
}

TEST(PageTest, CreatesAndLoadsImagesWithoutPageValidation) {
    ASSERT_TRUE(Page::create(PageKind::kUninitialized, kAddress));
    // PageKind has a fixed uint16_t representation; this exercises an unknown image value.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    ASSERT_TRUE(
        Page::create(
            static_cast<PageKind>(std::uint16_t{0xFFFF}),
            PageAddress{base::kInvalidRelationId, base::kInvalidPageId}
        )
    );

    auto page = Page::create(PageKind::kHeap, kAddress);
    ASSERT_TRUE(page);
    auto header_bytes = page.value().mutable_view(0, kPageHeaderSize);
    ASSERT_TRUE(header_bytes);
    auto lower_bytes = header_bytes.value().subspan(16, 2);
    lower_bytes.front() = std::byte{0x00};
    lower_bytes.back() = std::byte{0x00};
    EXPECT_TRUE(Page::load(page.value().bytes(), kAddress));
}

}  // namespace
}  // namespace db::storage
