#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace db::test::rmdb_v3_fixture {

enum class Direction : std::uint8_t {
    kBidirectional,
    kClientToServer,
    kServerToClient,
};

enum class ExpectedOutcome : std::uint8_t {
    kAccepted,
    kRejectHeader,
    kRejectPayload,
};

inline constexpr std::uint8_t kNoFrameTag = 0xFFU;

struct Fixture final {
    std::string_view name;
    Direction direction;
    ExpectedOutcome expected_outcome;
    std::uint8_t frame_tag;
    std::span<const std::byte> bytes;
};

template <std::size_t Size>
constexpr auto view(const std::array<std::byte, Size>& bytes) noexcept
    -> std::span<const std::byte> {
    return {bytes.data(), bytes.size()};
}

inline constexpr std::array<std::byte, 8U> kPrefaceV3{
    std::byte{0x52U},
    std::byte{0x4DU},
    std::byte{0x44U},
    std::byte{0x42U},
    std::byte{0x00U},
    std::byte{0x03U},
    std::byte{0x00U},
    std::byte{0x00U},
};

inline constexpr std::array<std::byte, 17U> kExecStreamSelectOne{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x09U},
    std::byte{0x20U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x53U},
    std::byte{0x45U},
    std::byte{0x4CU},
    std::byte{0x45U},
    std::byte{0x43U},
    std::byte{0x54U},
    std::byte{0x20U},
    std::byte{0x31U},
    std::byte{0x3BU},
};

inline constexpr std::array<std::byte, 35U> kPrepareSetTwoParameters{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x1BU}, std::byte{0x21U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x01U},
    std::byte{0x00U}, std::byte{0x07U}, std::byte{0x01U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x01U}, std::byte{0x03U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x0EU}, std::byte{0x53U}, std::byte{0x45U}, std::byte{0x4CU}, std::byte{0x45U},
    std::byte{0x43U}, std::byte{0x54U}, std::byte{0x20U}, std::byte{0x24U}, std::byte{0x31U},
    std::byte{0x2CU}, std::byte{0x20U}, std::byte{0x24U}, std::byte{0x32U}, std::byte{0x3BU},
};

inline constexpr std::array<std::byte, 29U> kPrepareOkTwoColumns{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x15U}, std::byte{0x14U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x01U},
    std::byte{0x00U}, std::byte{0x07U}, std::byte{0x00U}, std::byte{0x02U}, std::byte{0x00U},
    std::byte{0x05U}, std::byte{0x74U}, std::byte{0x6FU}, std::byte{0x74U}, std::byte{0x61U},
    std::byte{0x6CU}, std::byte{0x01U}, std::byte{0x00U}, std::byte{0x04U}, std::byte{0x6EU},
    std::byte{0x6FU}, std::byte{0x74U}, std::byte{0x65U}, std::byte{0x03U},
};

inline constexpr std::array<std::byte, 24U> kExecBatchTwoParameters{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x10U}, std::byte{0x22U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x01U},
    std::byte{0x00U}, std::byte{0x07U}, std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x2AU}, std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x02U}, std::byte{0x68U}, std::byte{0x69U},
};

inline constexpr std::array<std::byte, 25U> kMetaTwoColumns{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x11U}, std::byte{0x01U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x00U}, std::byte{0x05U}, std::byte{0x74U}, std::byte{0x6FU}, std::byte{0x74U},
    std::byte{0x61U}, std::byte{0x6CU}, std::byte{0x01U}, std::byte{0x00U}, std::byte{0x04U},
    std::byte{0x6EU}, std::byte{0x6FU}, std::byte{0x74U}, std::byte{0x65U}, std::byte{0x03U},
};

inline constexpr std::array<std::byte, 20U> kRowTwoValues{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x0CU}, std::byte{0x02U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x01U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x2AU}, std::byte{0x01U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U}, std::byte{0x68U}, std::byte{0x69U},
};

inline constexpr std::array<std::byte, 16U> kResultEndOne{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x08U},
    std::byte{0x11U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x01U},
};

inline constexpr std::array<std::byte, 8U> kCommandOk{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x10U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
};

inline constexpr std::array<std::byte, 13U> kTransactionAbortRetry{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x05U},
    std::byte{0x12U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x72U},
    std::byte{0x65U},
    std::byte{0x74U},
    std::byte{0x72U},
    std::byte{0x79U},
};

inline constexpr std::array<std::byte, 15U> kErrorBadSql{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x07U},
    std::byte{0x13U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x62U},
    std::byte{0x61U},
    std::byte{0x64U},
    std::byte{0x20U},
    std::byte{0x53U},
    std::byte{0x51U},
    std::byte{0x4CU},
};

inline constexpr std::array<std::byte, 37U> kBatchResultSuccess{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x1DU}, std::byte{0x15U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x01U},
    std::byte{0x00U}, std::byte{0xFFU}, std::byte{0xFFU}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x01U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x01U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x2AU},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x68U}, std::byte{0x69U},
};

inline constexpr std::array<std::byte, 22U> kBatchResultTransactionAbort{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x0EU}, std::byte{0x15U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x03U}, std::byte{0x73U}, std::byte{0x73U}, std::byte{0x69U},
    std::byte{0x00U}, std::byte{0x00U},
};

inline constexpr std::array<std::byte, 9U> kRequestReservedNonzero{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x01U},
    std::byte{0x20U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x01U},
    std::byte{0x78U},
};

inline constexpr std::array<std::byte, 9U> kRequestAutoAbortOnStream{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x01U},
    std::byte{0x20U},
    std::byte{0x01U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x78U},
};

inline constexpr std::array<std::byte, 8U> kOversizedRequestHeader{
    std::byte{0x00U},
    std::byte{0x10U},
    std::byte{0x00U},
    std::byte{0x01U},
    std::byte{0x20U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
};

inline constexpr std::array<std::byte, 9U> kRowInvalidNullMarker{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x01U},
    std::byte{0x02U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x02U},
};

inline constexpr std::array<std::byte, 9U> kErrorInvalidUtf8{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x01U},
    std::byte{0x13U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0xFFU},
};

inline constexpr std::array<std::byte, 8U> kResponseReservedNonzero{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x10U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x01U},
};

inline constexpr std::array<Fixture, 19U> kCorpus{
    Fixture{
        "preface_v3",
        Direction::kBidirectional,
        ExpectedOutcome::kAccepted,
        kNoFrameTag,
        view(kPrefaceV3)
    },
    Fixture{
        "exec_stream_select_one",
        Direction::kClientToServer,
        ExpectedOutcome::kAccepted,
        0x20U,
        view(kExecStreamSelectOne)
    },
    Fixture{
        "prepare_set_two_parameters",
        Direction::kClientToServer,
        ExpectedOutcome::kAccepted,
        0x21U,
        view(kPrepareSetTwoParameters)
    },
    Fixture{
        "prepare_ok_two_columns",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x14U,
        view(kPrepareOkTwoColumns)
    },
    Fixture{
        "exec_batch_two_parameters",
        Direction::kClientToServer,
        ExpectedOutcome::kAccepted,
        0x22U,
        view(kExecBatchTwoParameters)
    },
    Fixture{
        "meta_two_columns",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x01U,
        view(kMetaTwoColumns)
    },
    Fixture{
        "row_two_values",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x02U,
        view(kRowTwoValues)
    },
    Fixture{
        "result_end_one",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x11U,
        view(kResultEndOne)
    },
    Fixture{
        "command_ok",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x10U,
        view(kCommandOk)
    },
    Fixture{
        "transaction_abort_retry",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x12U,
        view(kTransactionAbortRetry)
    },
    Fixture{
        "error_bad_sql",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x13U,
        view(kErrorBadSql)
    },
    Fixture{
        "batch_result_success",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x15U,
        view(kBatchResultSuccess)
    },
    Fixture{
        "batch_result_transaction_abort",
        Direction::kServerToClient,
        ExpectedOutcome::kAccepted,
        0x15U,
        view(kBatchResultTransactionAbort)
    },
    Fixture{
        "request_reserved_nonzero",
        Direction::kClientToServer,
        ExpectedOutcome::kRejectHeader,
        0x20U,
        view(kRequestReservedNonzero)
    },
    Fixture{
        "request_auto_abort_on_stream",
        Direction::kClientToServer,
        ExpectedOutcome::kRejectHeader,
        0x20U,
        view(kRequestAutoAbortOnStream)
    },
    Fixture{
        "oversized_request_header",
        Direction::kClientToServer,
        ExpectedOutcome::kRejectHeader,
        0x20U,
        view(kOversizedRequestHeader)
    },
    Fixture{
        "row_invalid_null_marker",
        Direction::kServerToClient,
        ExpectedOutcome::kRejectPayload,
        0x02U,
        view(kRowInvalidNullMarker)
    },
    Fixture{
        "error_invalid_utf8",
        Direction::kServerToClient,
        ExpectedOutcome::kRejectPayload,
        0x13U,
        view(kErrorInvalidUtf8)
    },
    Fixture{
        "response_reserved_nonzero",
        Direction::kServerToClient,
        ExpectedOutcome::kRejectHeader,
        0x10U,
        view(kResponseReservedNonzero)
    },
};

}  // namespace db::test::rmdb_v3_fixture
