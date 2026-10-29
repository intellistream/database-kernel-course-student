#include <algorithm>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/wal/format.hpp"
#include "db/wal/ring_buffer.hpp"

namespace db::wal {
namespace {

auto patterned_bytes(const std::size_t size, const std::uint8_t seed = 7)
    -> std::vector<std::byte> {
    std::vector<std::byte> bytes(size);
    for (std::size_t index = 0; index < size; ++index) {
        bytes[index] = static_cast<std::byte>((index * 31U + seed) & 0xFFU);
    }
    return bytes;
}

auto reserve(WalRingBuffer& ring, const std::size_t size) -> WalReservation {
    auto outcome = ring.try_reserve(size);
    EXPECT_TRUE(outcome);
    EXPECT_TRUE(std::holds_alternative<WalReservation>(outcome.value()));
    return std::get<WalReservation>(outcome.value());
}

auto page_header_at(const std::vector<std::byte>& bytes, const std::size_t offset)
    -> WalPageHeader {
    const auto encoded = base::Bytes{bytes}.subspan(offset, kWalPageHeaderSize);
    EXPECT_TRUE(base::verify_crc32c(encoded, kWalPageChecksumOffset));
    auto decoded = decode_page_header(encoded);
    EXPECT_TRUE(decoded);
    return decoded.value();
}

TEST(WalRingBufferTest, WritesPageHeadersAndCanonicalTailPadding) {
    constexpr auto initial_lsn = kInitialWalRingLsn.value();
    auto ring = WalRingBuffer::create(2);
    ASSERT_TRUE(ring);
    EXPECT_EQ(ring->page_count(), 2U);
    EXPECT_EQ(ring->payload_capacity(), 2U * (kWalPageSize - kWalPageHeaderSize));

    const auto first_payload = patterned_bytes(kWalPageSize - kWalPageHeaderSize - 8U);
    const auto first = reserve(ring.value(), first_payload.size());
    EXPECT_EQ(first.start_lsn, base::Lsn{initial_lsn + kWalPageHeaderSize});
    EXPECT_EQ(first.end_lsn, base::Lsn{initial_lsn + kWalPageSize - 8U});
    ASSERT_TRUE(ring->copy_in(first, first_payload));

    const auto second_payload = patterned_bytes(kWalRecordHeaderSize, 29);
    const auto second = reserve(ring.value(), second_payload.size());
    EXPECT_EQ(second.start_lsn, base::Lsn{initial_lsn + kWalPageSize + kWalPageHeaderSize});
    ASSERT_TRUE(ring->copy_in(second, second_payload));

    std::vector<std::byte> output;
    ASSERT_TRUE(ring->copy_out(kInitialWalRingLsn, second.end_lsn, output));
    EXPECT_EQ(output.size(), second.end_lsn.value() - initial_lsn);
    const auto first_header = page_header_at(output, 0);
    EXPECT_EQ(first_header.page_start_lsn, kInitialWalRingLsn);
    EXPECT_EQ(first_header.flags, 0U);
    EXPECT_TRUE(
        std::equal(
            first_payload.begin(),
            first_payload.end(),
            output.begin() + static_cast<std::ptrdiff_t>(kWalPageHeaderSize)
        )
    );
    EXPECT_TRUE(
        std::all_of(
            output.begin() + static_cast<std::ptrdiff_t>(kWalPageSize - 8U),
            output.begin() + static_cast<std::ptrdiff_t>(kWalPageSize),
            [](const std::byte byte) {
                return byte == std::byte{0};
            }
        )
    );
    const auto second_header = page_header_at(output, kWalPageSize);
    EXPECT_EQ(second_header.page_start_lsn, base::Lsn{initial_lsn + kWalPageSize});
    EXPECT_EQ(second_header.flags, 0U);
}

TEST(WalRingBufferTest, MarksContinuationHeadersForCrossPageRecords) {
    constexpr auto initial_lsn = kInitialWalRingLsn.value();
    auto ring = WalRingBuffer::create(2);
    ASSERT_TRUE(ring);
    const auto payload = patterned_bytes(kWalPageSize - kWalPageHeaderSize + 16U);
    const auto reservation = reserve(ring.value(), payload.size());
    ASSERT_TRUE(ring->copy_in(reservation, payload));
    EXPECT_EQ(
        reservation.end_lsn,
        base::Lsn{initial_lsn + kWalPageSize + kWalPageHeaderSize + 16U}
    );

    std::vector<std::byte> output;
    ASSERT_TRUE(ring->copy_out(kInitialWalRingLsn, reservation.end_lsn, output));
    EXPECT_EQ(page_header_at(output, 0).flags, 0U);
    EXPECT_EQ(page_header_at(output, kWalPageSize).flags, kWalPageContinuation);
    EXPECT_TRUE(
        std::equal(
            payload.end() - 16,
            payload.end(),
            output.begin() + static_cast<std::ptrdiff_t>(kWalPageSize + kWalPageHeaderSize)
        )
    );
}

TEST(WalRingBufferTest, PublishesOnlyTheContinuousCompletedPrefix) {
    constexpr auto initial_lsn = kInitialWalRingLsn.value();
    auto ring = WalRingBuffer::create(2);
    ASSERT_TRUE(ring);
    const auto first_payload = patterned_bytes(40, 1);
    const auto second_payload = patterned_bytes(40, 2);
    const auto first = reserve(ring.value(), first_payload.size());
    const auto second = reserve(ring.value(), second_payload.size());
    EXPECT_EQ(ring->reserved_lsn(), second.end_lsn);
    ASSERT_TRUE(ring->copy_in(second, second_payload));
    EXPECT_EQ(ring->inserted_lsn(), kInitialWalRingLsn);
    ASSERT_TRUE(ring->copy_in(first, first_payload));
    EXPECT_EQ(ring->inserted_lsn(), second.end_lsn);

    std::vector<std::byte> output;
    ASSERT_TRUE(ring->copy_out(kInitialWalRingLsn, second.end_lsn, output));
    EXPECT_TRUE(
        std::equal(
            first_payload.begin(),
            first_payload.end(),
            output.begin() + static_cast<std::ptrdiff_t>(first.start_lsn.value() - initial_lsn)
        )
    );
    EXPECT_TRUE(
        std::equal(
            second_payload.begin(),
            second_payload.end(),
            output.begin() + static_cast<std::ptrdiff_t>(second.start_lsn.value() - initial_lsn)
        )
    );
}

TEST(WalRingBufferTest, AppliesBackpressureUntilWrittenPagesAreReleased) {
    constexpr auto initial_lsn = kInitialWalRingLsn.value();
    auto ring = WalRingBuffer::create(2);
    ASSERT_TRUE(ring);
    const auto first_payload = patterned_bytes(kWalPageSize + 8U);
    const auto first = reserve(ring.value(), first_payload.size());
    ASSERT_TRUE(ring->copy_in(first, first_payload));

    constexpr std::size_t next_size = kWalPageSize - 56U;
    const auto blocked = ring->try_reserve(next_size);
    ASSERT_TRUE(blocked);
    ASSERT_TRUE(std::holds_alternative<WalBackpressure>(blocked.value()));
    EXPECT_EQ(std::get<WalBackpressure>(blocked.value()).write_target, first.end_lsn);
    ASSERT_TRUE(ring->release_before(base::Lsn{initial_lsn + kWalPageSize}));
    const auto next = reserve(ring.value(), next_size);
    const auto next_payload = patterned_bytes(next_size, 41);
    ASSERT_TRUE(ring->copy_in(next, next_payload));
    EXPECT_GT(next.end_lsn.value(), initial_lsn + std::uint64_t{2} * kWalPageSize);
}

TEST(WalRingBufferTest, UsesOneBoundedOversizedBatchWhenTheRingIsEmpty) {
    constexpr auto initial_lsn = kInitialWalRingLsn.value();
    auto ring = WalRingBuffer::create(1);
    ASSERT_TRUE(ring);
    const auto payload = patterned_bytes(kWalPageSize + 8U);
    const auto reservation = reserve(ring.value(), payload.size());
    ASSERT_TRUE(reservation.oversized);
    ASSERT_TRUE(ring->copy_in(reservation, payload));
    EXPECT_EQ(ring->inserted_lsn(), reservation.end_lsn);
    const auto blocked = ring->try_reserve(kWalRecordHeaderSize);
    ASSERT_TRUE(blocked);
    EXPECT_TRUE(std::holds_alternative<WalBackpressure>(blocked.value()));

    std::vector<std::byte> output;
    ASSERT_TRUE(ring->copy_out(kInitialWalRingLsn, reservation.end_lsn, output));
    EXPECT_EQ(page_header_at(output, kWalPageSize).flags, kWalPageContinuation);
    EXPECT_FALSE(ring->release_before(base::Lsn{initial_lsn + kWalPageSize}));
    ASSERT_TRUE(ring->release_before(reservation.end_lsn));
    EXPECT_EQ(ring->released_lsn(), reservation.end_lsn);
    EXPECT_FALSE(reserve(ring.value(), kWalRecordHeaderSize).oversized);
}

TEST(WalRingBufferTest, CopiesDisjointReservationsConcurrently) {
    constexpr auto initial_lsn = kInitialWalRingLsn.value();
    auto ring = WalRingBuffer::create(2);
    ASSERT_TRUE(ring);
    const auto first_payload = patterned_bytes(256, 11);
    const auto second_payload = patterned_bytes(256, 23);
    const auto first = reserve(ring.value(), first_payload.size());
    const auto second = reserve(ring.value(), second_payload.size());
    std::barrier ready{3};
    std::thread first_thread{[&] {
        ready.arrive_and_wait();
        EXPECT_TRUE(ring->copy_in(first, first_payload));
    }};
    std::thread second_thread{[&] {
        ready.arrive_and_wait();
        EXPECT_TRUE(ring->copy_in(second, second_payload));
    }};
    ready.arrive_and_wait();
    first_thread.join();
    second_thread.join();
    EXPECT_EQ(ring->inserted_lsn(), second.end_lsn);

    std::vector<std::byte> output;
    ASSERT_TRUE(ring->copy_out(kInitialWalRingLsn, second.end_lsn, output));
    EXPECT_TRUE(
        std::equal(
            second_payload.begin(),
            second_payload.end(),
            output.begin() + static_cast<std::ptrdiff_t>(second.start_lsn.value() - initial_lsn)
        )
    );
}

TEST(WalRingBufferTest, RejectsInvalidConfigurationRangesAndReservationTokens) {
    EXPECT_FALSE(WalRingBuffer::create(0));
    EXPECT_FALSE(WalRingBuffer::create(kMaximumWalRingPages + 1U));
    EXPECT_FALSE(WalRingBuffer::create(1, base::Lsn{0}));
    EXPECT_FALSE(WalRingBuffer::create(1, base::Lsn{kWalPageSize + 8U}));
    EXPECT_FALSE(WalRingBuffer::create(1, base::Lsn{kWalPageSize + kWalPageHeaderSize + 1U}));
    const base::Lsn partial_start{kWalPageSize + 64U};
    auto ring = WalRingBuffer::create(1, partial_start);
    ASSERT_TRUE(ring);
    EXPECT_FALSE(ring->try_reserve(0));
    EXPECT_FALSE(ring->try_reserve(kWalRecordHeaderSize - 1U));
    EXPECT_FALSE(ring->try_reserve(kWalRecordHeaderSize + 4U));
    EXPECT_FALSE(ring->try_reserve(static_cast<std::size_t>(kMaximumWalRecordSize) + 8U));

    const auto payload = patterned_bytes(kWalRecordHeaderSize);
    const auto reservation = reserve(ring.value(), payload.size());
    auto forged = reservation;
    ++forged.identifier;
    EXPECT_FALSE(ring->copy_in(forged, payload));
    EXPECT_FALSE(ring->copy_in(reservation, base::Bytes{payload}.first(payload.size() - 1U)));
    ASSERT_TRUE(ring->copy_in(reservation, payload));
    EXPECT_FALSE(ring->copy_in(reservation, payload));
    std::vector<std::byte> output;
    EXPECT_FALSE(ring->copy_out(reservation.end_lsn, reservation.start_lsn, output));
    EXPECT_FALSE(ring->copy_out(base::Lsn{0}, reservation.end_lsn, output));
    EXPECT_FALSE(ring->release_before(base::Lsn{reservation.end_lsn.value() + 8U}));
    ASSERT_TRUE(ring->copy_out(partial_start, reservation.end_lsn, output));
    EXPECT_EQ(output, payload);
}

}  // namespace
}  // namespace db::wal
