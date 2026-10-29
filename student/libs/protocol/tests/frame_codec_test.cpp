#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/protocol/frame_codec.hpp"

#include "rmdb_v3_fixture_corpus.hpp"

namespace db::protocol {
namespace {

using test::rmdb_v3_fixture::Direction;
using test::rmdb_v3_fixture::ExpectedOutcome;
using test::rmdb_v3_fixture::Fixture;

auto frame_direction(const Direction direction) -> FrameDirection {
    switch (direction) {
        case Direction::kClientToServer:
            return FrameDirection::kClientToServer;
        case Direction::kServerToClient:
            return FrameDirection::kServerToClient;
        case Direction::kBidirectional:
            break;
    }
    return FrameDirection::kClientToServer;
}

auto frame_header_from(const base::Bytes bytes) -> FrameHeader {
    base::BigEndianReader reader{bytes.first(kRmdbFrameHeaderSize)};
    const auto payload_length = reader.read_u32();
    const auto tag = reader.read_u8();
    const auto flags = reader.read_u8();
    const auto reserved = reader.read_u16();
    EXPECT_TRUE(payload_length);
    EXPECT_TRUE(tag);
    EXPECT_TRUE(flags);
    EXPECT_TRUE(reserved);
    EXPECT_TRUE(reader.finish());
    return FrameHeader{
        .payload_length = payload_length.value(),
        .tag = tag.value(),
        .flags = flags.value(),
        .reserved = reserved.value(),
    };
}

auto consume_one_byte_at_a_time(FrameDecoder& decoder, const base::Bytes bytes) -> FrameView {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto step = decoder.consume(bytes.subspan(offset, 1U));
        EXPECT_TRUE(step);
        if (!step) {
            return {};
        }
        EXPECT_EQ(step.value().consumed, 1U);
        offset += step.value().consumed;
        if (step.value().frame.has_value()) {
            EXPECT_EQ(offset, bytes.size());
            return *step.value().frame;
        }
    }
    ADD_FAILURE() << "decoder did not complete a full frame";
    return {};
}

void expect_reusable_after_move(FrameDecoder& decoder) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(decoder.has_incomplete_frame());
    const auto& frame = test::rmdb_v3_fixture::kExecStreamSelectOne;
    const auto decoded = decoder.consume(frame);
    ASSERT_TRUE(decoded);
    ASSERT_TRUE(decoded->frame.has_value());
    EXPECT_EQ(decoded->consumed, frame.size());
    EXPECT_EQ(decoded->frame->header, frame_header_from(frame));
}

TEST(FrameCodecTest, PrefaceMatchesThePinnedFixtureAndRejectsAnyMismatch) {
    const auto& preface = test::rmdb_v3_fixture::kPrefaceV3;
    EXPECT_EQ(kRmdbV3Preface, preface);
    EXPECT_TRUE(validate_rmdb_v3_preface(preface));
    EXPECT_EQ(
        validate_rmdb_v3_preface(base::Bytes{preface}.first(preface.size() - 1U)).error().code(),
        base::ErrorCode::kCorruption
    );

    auto bad_preface = preface;
    bad_preface.back() = std::byte{0x01U};
    EXPECT_EQ(validate_rmdb_v3_preface(bad_preface).error().code(), base::ErrorCode::kCorruption);
}

TEST(FrameCodecTest, IncrementallyDecodesEveryAcceptedCorpusFrame) {
    FrameDecoder decoder;
    for (const Fixture& fixture : test::rmdb_v3_fixture::kCorpus) {
        if (fixture.name == "preface_v3"
            || fixture.expected_outcome != ExpectedOutcome::kAccepted) {
            continue;
        }
        const FrameView frame = consume_one_byte_at_a_time(decoder, fixture.bytes);
        EXPECT_EQ(frame.header, frame_header_from(fixture.bytes)) << fixture.name;
        EXPECT_TRUE(
            std::equal(
                frame.payload.begin(),
                frame.payload.end(),
                fixture.bytes.subspan(kRmdbFrameHeaderSize).begin(),
                fixture.bytes.subspan(kRmdbFrameHeaderSize).end()
            )
        ) << fixture.name;
        const auto validated =
            validate_inbound_frame_envelope(frame_direction(fixture.direction), frame);
        EXPECT_TRUE(validated) << fixture.name;
        EXPECT_EQ(validated.value().payload_length, frame.payload.size()) << fixture.name;
    }
}

TEST(FrameCodecTest, StopsAtTheFirstFrameAndLetsTheCallerRetainTheSuffix) {
    const auto& first = test::rmdb_v3_fixture::kExecStreamSelectOne;
    const auto& second = test::rmdb_v3_fixture::kPrepareSetTwoParameters;
    std::vector<std::byte> wire;
    wire.reserve(first.size() + second.size());
    wire.insert(wire.end(), first.begin(), first.end());
    wire.insert(wire.end(), second.begin(), second.end());

    FrameDecoder decoder;
    const auto first_step = decoder.consume(wire);
    ASSERT_TRUE(first_step);
    ASSERT_TRUE(first_step.value().frame.has_value());
    EXPECT_EQ(first_step.value().consumed, first.size());
    EXPECT_EQ(
        first_step.value().frame->header.tag,
        static_cast<std::uint8_t>(FrameTag::kExecStream)
    );

    const auto second_step =
        decoder.consume(base::Bytes{wire}.subspan(first_step.value().consumed));
    ASSERT_TRUE(second_step);
    ASSERT_TRUE(second_step.value().frame.has_value());
    EXPECT_EQ(second_step.value().consumed, second.size());
    EXPECT_EQ(
        second_step.value().frame->header.tag,
        static_cast<std::uint8_t>(FrameTag::kPrepareSet)
    );
}

TEST(FrameCodecTest, ReportsWhetherAConnectionStoppedInsideOneFrame) {
    FrameDecoder decoder;
    const auto& frame = test::rmdb_v3_fixture::kExecStreamSelectOne;

    const auto partial_header = decoder.consume(base::Bytes{frame}.first(3U));
    ASSERT_TRUE(partial_header);
    EXPECT_FALSE(partial_header.value().frame.has_value());
    EXPECT_TRUE(decoder.has_incomplete_frame());

    const auto partial_payload = decoder.consume(base::Bytes{frame}.subspan(3U, 7U));
    ASSERT_TRUE(partial_payload);
    EXPECT_FALSE(partial_payload.value().frame.has_value());
    EXPECT_TRUE(decoder.has_incomplete_frame());

    const auto completed = decoder.consume(base::Bytes{frame}.subspan(10U));
    ASSERT_TRUE(completed);
    EXPECT_TRUE(completed.value().frame.has_value());
    EXPECT_FALSE(decoder.has_incomplete_frame());
}

TEST(FrameCodecTest, MoveTransfersPartialFramingAndLeavesTheSourceReusable) {
    const auto& frame = test::rmdb_v3_fixture::kPrepareSetTwoParameters;

    FrameDecoder header_source;
    const auto partial_header = header_source.consume(base::Bytes{frame}.first(3U));
    ASSERT_TRUE(partial_header);
    ASSERT_FALSE(partial_header->frame.has_value());
    FrameDecoder header_destination{std::move(header_source)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_reusable_after_move(header_source);

    const auto completed_header = header_destination.consume(base::Bytes{frame}.subspan(3U));
    ASSERT_TRUE(completed_header);
    ASSERT_TRUE(completed_header->frame.has_value());
    EXPECT_EQ(completed_header->consumed, frame.size() - 3U);
    EXPECT_EQ(completed_header->frame->header, frame_header_from(frame));

    constexpr std::size_t k_payload_prefix_size = 2U;
    constexpr std::size_t k_partial_size = kRmdbFrameHeaderSize + k_payload_prefix_size;
    FrameDecoder payload_source;
    const auto partial_payload = payload_source.consume(base::Bytes{frame}.first(k_partial_size));
    ASSERT_TRUE(partial_payload);
    ASSERT_FALSE(partial_payload->frame.has_value());
    FrameDecoder payload_destination;
    ASSERT_TRUE(payload_destination.consume(base::Bytes{frame}.first(1U)));
    payload_destination = std::move(payload_source);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_reusable_after_move(payload_source);

    const auto completed_payload =
        payload_destination.consume(base::Bytes{frame}.subspan(k_partial_size));
    ASSERT_TRUE(completed_payload);
    ASSERT_TRUE(completed_payload->frame.has_value());
    EXPECT_EQ(completed_payload->consumed, frame.size() - k_partial_size);
    EXPECT_EQ(completed_payload->frame->header, frame_header_from(frame));
    EXPECT_TRUE(
        std::ranges::equal(
            completed_payload->frame->payload,
            base::Bytes{frame}.subspan(kRmdbFrameHeaderSize)
        )
    );
}

TEST(FrameCodecTest, RejectsOversizedFramesBeforeAllocatingPayloadStorage) {
    FrameDecoder decoder;
    const auto decoded = decoder.consume(test::rmdb_v3_fixture::kOversizedRequestHeader);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(decoder.payload_capacity(), 0U);
}

TEST(FrameCodecTest, MapsHeaderViolationsByPeerDirection) {
    const auto request_reserved = frame_header_from(test::rmdb_v3_fixture::kRequestReservedNonzero);
    const auto request_flags = frame_header_from(test::rmdb_v3_fixture::kRequestAutoAbortOnStream);
    const auto response_reserved =
        frame_header_from(test::rmdb_v3_fixture::kResponseReservedNonzero);

    EXPECT_EQ(
        validate_inbound_frame_header(FrameDirection::kClientToServer, request_reserved)
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        validate_inbound_frame_header(FrameDirection::kClientToServer, request_flags)
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        validate_inbound_frame_header(FrameDirection::kServerToClient, response_reserved)
            .error()
            .code(),
        base::ErrorCode::kCorruption
    );
    EXPECT_EQ(
        validate_inbound_frame_header(
            FrameDirection::kClientToServer,
            FrameHeader{.payload_length = 0U, .tag = 0xFFU}
        )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        validate_inbound_frame_header(
            FrameDirection::kClientToServer,
            FrameHeader{
                .payload_length = 0U,
                .tag = static_cast<std::uint8_t>(FrameTag::kMeta),
            }
        )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
}

TEST(FrameCodecTest, ValidatesTheEntireHeaderFixtureCorpusAndEnvelopeLengths) {
    for (const Fixture& fixture : test::rmdb_v3_fixture::kCorpus) {
        if (fixture.name == "preface_v3"
            || fixture.expected_outcome == ExpectedOutcome::kRejectPayload) {
            continue;
        }
        const FrameHeader header = frame_header_from(fixture.bytes);
        const auto validated =
            validate_inbound_frame_header(frame_direction(fixture.direction), header);
        if (fixture.expected_outcome == ExpectedOutcome::kAccepted) {
            EXPECT_TRUE(validated) << fixture.name;
            continue;
        }
        EXPECT_FALSE(validated) << fixture.name;
        if (!validated) {
            EXPECT_EQ(
                validated.error().code(),
                fixture.direction == Direction::kClientToServer ? base::ErrorCode::kInvalidArgument
                                                                : base::ErrorCode::kCorruption
            ) << fixture.name;
        }
    }

    const FrameView mismatched{
        .header =
            FrameHeader{
                .payload_length = 1U,
                .tag = static_cast<std::uint8_t>(FrameTag::kExecStream),
            },
        .payload = {},
    };
    EXPECT_EQ(
        validate_inbound_frame_envelope(FrameDirection::kClientToServer, mismatched).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        validate_inbound_frame_envelope(FrameDirection::kServerToClient, mismatched).error().code(),
        base::ErrorCode::kCorruption
    );
}

TEST(FrameCodecTest, EncodesAcceptedCorpusFramesByteForByteWithReusableStorage) {
    FrameEncoder encoder;
    std::size_t observed_capacity = 0U;
    for (const Fixture& fixture : test::rmdb_v3_fixture::kCorpus) {
        if (fixture.name == "preface_v3"
            || fixture.expected_outcome != ExpectedOutcome::kAccepted) {
            continue;
        }
        const FrameHeader header = frame_header_from(fixture.bytes);
        const auto encoded = encoder.encode(
            frame_direction(fixture.direction),
            static_cast<FrameTag>(header.tag),
            header.flags,
            fixture.bytes.subspan(kRmdbFrameHeaderSize)
        );
        ASSERT_TRUE(encoded) << fixture.name;
        EXPECT_TRUE(
            std::equal(
                encoded.value().begin(),
                encoded.value().end(),
                fixture.bytes.begin(),
                fixture.bytes.end()
            )
        ) << fixture.name;
        if (observed_capacity != 0U && encoded.value().size() <= observed_capacity) {
            EXPECT_GE(encoder.buffer_capacity(), observed_capacity);
        }
        observed_capacity = encoder.buffer_capacity();
    }

    const auto first = encoder.encode(
        FrameDirection::kClientToServer,
        FrameTag::kExecStream,
        kRmdbNoFrameFlags,
        base::Bytes{test::rmdb_v3_fixture::kExecStreamSelectOne}.subspan(kRmdbFrameHeaderSize)
    );
    ASSERT_TRUE(first);
    const auto capacity = encoder.buffer_capacity();
    const auto second = encoder.encode(
        FrameDirection::kClientToServer,
        FrameTag::kExecStream,
        kRmdbNoFrameFlags,
        base::Bytes{test::rmdb_v3_fixture::kExecStreamSelectOne}.subspan(kRmdbFrameHeaderSize)
    );
    ASSERT_TRUE(second);
    EXPECT_EQ(encoder.buffer_capacity(), capacity);
}

TEST(FrameCodecTest, EncoderRejectsWrongDirectionFlagsAndOversizedPayload) {
    FrameEncoder encoder;
    EXPECT_EQ(
        encoder.encode(FrameDirection::kClientToServer, FrameTag::kMeta, kRmdbNoFrameFlags, {})
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        encoder.encode(FrameDirection::kClientToServer, FrameTag::kExecBatch, kRmdbNoFrameFlags, {})
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );

    std::vector<std::byte> oversized(kMaximumRmdbFramePayloadSize + 1U);
    EXPECT_EQ(
        encoder
            .encode(
                FrameDirection::kClientToServer,
                FrameTag::kExecStream,
                kRmdbNoFrameFlags,
                oversized
            )
            .error()
            .code(),
        base::ErrorCode::kOutOfRange
    );
}

TEST(FrameCodecTest, EncoderSafelyStagesItsPreviousOutputWhenUsedAsPayload) {
    FrameEncoder encoder;
    const auto initial = encoder.encode(
        FrameDirection::kClientToServer,
        FrameTag::kExecStream,
        kRmdbNoFrameFlags,
        base::Bytes{test::rmdb_v3_fixture::kExecStreamSelectOne}.subspan(kRmdbFrameHeaderSize)
    );
    ASSERT_TRUE(initial);
    const std::vector<std::byte> expected_payload{initial.value().begin(), initial.value().end()};

    const auto nested = encoder.encode(
        FrameDirection::kClientToServer,
        FrameTag::kExecStream,
        kRmdbNoFrameFlags,
        initial.value()
    );
    ASSERT_TRUE(nested);
    EXPECT_EQ(nested.value().size(), kRmdbFrameHeaderSize + expected_payload.size());
    EXPECT_TRUE(
        std::equal(
            nested.value().begin() + static_cast<std::ptrdiff_t>(kRmdbFrameHeaderSize),
            nested.value().end(),
            expected_payload.begin(),
            expected_payload.end()
        )
    );
}

}  // namespace
}  // namespace db::protocol
