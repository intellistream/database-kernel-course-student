#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>

#include <gtest/gtest.h>

#include "rmdb_v3_fixture_corpus.hpp"

namespace db::test::rmdb_v3_fixture {
namespace {

auto byte_value(const std::byte value) noexcept -> std::uint8_t {
    return std::to_integer<std::uint8_t>(value);
}

auto declared_payload_size(const std::span<const std::byte> bytes) -> std::uint32_t {
    if (bytes.size() < 4U) {
        return 0U;
    }
    return (static_cast<std::uint32_t>(byte_value(bytes[0])) << 24U)
           | (static_cast<std::uint32_t>(byte_value(bytes[1])) << 16U)
           | (static_cast<std::uint32_t>(byte_value(bytes[2])) << 8U)
           | static_cast<std::uint32_t>(byte_value(bytes[3]));
}

}  // namespace

TEST(RmdbV3FixtureCorpusTest, HasUniqueNamedNonemptyByteSequences) {
    std::set<std::string_view> fixture_names;
    for (const auto& fixture : kCorpus) {
        EXPECT_FALSE(fixture.name.empty());
        EXPECT_FALSE(fixture.bytes.empty());
        EXPECT_TRUE(fixture_names.insert(fixture.name).second) << fixture.name;
    }
}

TEST(RmdbV3FixtureCorpusTest, ValidFramesHaveExactCanonicalEnvelopeLengths) {
    for (const auto& fixture : kCorpus) {
        if (fixture.name == "preface_v3"
            || fixture.expected_outcome != ExpectedOutcome::kAccepted) {
            continue;
        }
        ASSERT_GE(fixture.bytes.size(), 8U) << fixture.name;
        EXPECT_EQ(declared_payload_size(fixture.bytes), fixture.bytes.size() - 8U) << fixture.name;
        EXPECT_EQ(byte_value(fixture.bytes[4]), fixture.frame_tag) << fixture.name;
        const auto expected_flags = fixture.frame_tag == 0x22U ? 0x01U : 0U;
        EXPECT_EQ(byte_value(fixture.bytes[5]), expected_flags) << fixture.name;
        EXPECT_EQ(byte_value(fixture.bytes[6]), 0U) << fixture.name;
        EXPECT_EQ(byte_value(fixture.bytes[7]), 0U) << fixture.name;
    }
}

TEST(RmdbV3FixtureCorpusTest, PinsVersionAndMalformedBoundaryBytes) {
    EXPECT_EQ(kPrefaceV3.size(), 8U);
    EXPECT_EQ(byte_value(kPrefaceV3[0]), static_cast<std::uint8_t>('R'));
    EXPECT_EQ(byte_value(kPrefaceV3[4]), 0U);
    EXPECT_EQ(byte_value(kPrefaceV3[5]), 3U);
    EXPECT_EQ(declared_payload_size(kOversizedRequestHeader), 1'048'577U);
    EXPECT_EQ(byte_value(kRequestReservedNonzero[7]), 1U);
    EXPECT_EQ(byte_value(kRequestAutoAbortOnStream[5]), 1U);
    EXPECT_EQ(byte_value(kRowInvalidNullMarker.back()), 2U);
    EXPECT_EQ(byte_value(kErrorInvalidUtf8.back()), 0xFFU);
    EXPECT_EQ(byte_value(kResponseReservedNonzero[7]), 1U);
}

TEST(RmdbV3FixtureCorpusTest, PinsPreparedAndBatchSemanticFields) {
    EXPECT_EQ(declared_payload_size(kPrepareSetTwoParameters), 27U);
    EXPECT_EQ(byte_value(kPrepareSetTwoParameters[9]), 1U);
    EXPECT_EQ(byte_value(kPrepareSetTwoParameters[11]), 7U);
    EXPECT_EQ(byte_value(kPrepareSetTwoParameters[12]), 1U);
    EXPECT_EQ(byte_value(kPrepareSetTwoParameters[14]), 2U);
    EXPECT_EQ(byte_value(kPrepareSetTwoParameters[15]), 1U);
    EXPECT_EQ(byte_value(kPrepareSetTwoParameters[16]), 3U);
    EXPECT_EQ(byte_value(kPrepareSetTwoParameters[20]), 14U);
    EXPECT_EQ(byte_value(kPrepareSetTwoParameters.back()), static_cast<std::uint8_t>(';'));

    EXPECT_EQ(byte_value(kExecBatchTwoParameters[5]), 1U);
    EXPECT_EQ(byte_value(kExecBatchTwoParameters[9]), 1U);
    EXPECT_EQ(byte_value(kExecBatchTwoParameters[11]), 7U);
    EXPECT_EQ(byte_value(kExecBatchTwoParameters[12]), 1U);
    EXPECT_EQ(byte_value(kExecBatchTwoParameters[17]), 1U);
    EXPECT_EQ(byte_value(kExecBatchTwoParameters.back()), static_cast<std::uint8_t>('i'));

    EXPECT_EQ(declared_payload_size(kBatchResultSuccess), 29U);
    EXPECT_EQ(byte_value(kBatchResultSuccess[9]), 1U);
    EXPECT_EQ(byte_value(kBatchResultSuccess[10]), 0U);
    EXPECT_EQ(byte_value(kBatchResultSuccess[11]), 0xFFU);
    EXPECT_EQ(byte_value(kBatchResultSuccess[12]), 0xFFU);
    EXPECT_EQ(byte_value(kBatchResultSuccess[18]), 1U);
    EXPECT_EQ(byte_value(kBatchResultSuccess[24]), 1U);
    EXPECT_EQ(byte_value(kBatchResultSuccess[25]), 1U);
    EXPECT_EQ(byte_value(kBatchResultSuccess.back()), static_cast<std::uint8_t>('i'));

    EXPECT_EQ(declared_payload_size(kBatchResultTransactionAbort), 14U);
    EXPECT_EQ(byte_value(kBatchResultTransactionAbort[10]), 1U);
    EXPECT_EQ(byte_value(kBatchResultTransactionAbort[16]), 3U);
    EXPECT_EQ(byte_value(kBatchResultTransactionAbort[17]), static_cast<std::uint8_t>('s'));
    EXPECT_EQ(byte_value(kBatchResultTransactionAbort[20]), 0U);
}

}  // namespace db::test::rmdb_v3_fixture
