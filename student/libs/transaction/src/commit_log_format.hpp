#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {

inline constexpr std::uint16_t kCommitLogFormatVersion = 1;
inline constexpr std::size_t kCommitLogPageSize = std::size_t{4} * 1024U;
inline constexpr std::size_t kCommitLogPagesPerSegment = 32;
inline constexpr std::size_t kCommitLogMetadataSize = 64;
inline constexpr std::size_t kCommitLogSegmentHeaderSize = 64;
inline constexpr std::size_t kCommitLogChecksumOffset = 40;
inline constexpr std::size_t kCommitLogSegmentPayloadSize =
    kCommitLogPageSize * kCommitLogPagesPerSegment;
inline constexpr std::uint32_t kTransactionsPerCommitLogSegment =
    static_cast<std::uint32_t>(kCommitLogSegmentPayloadSize * 4U);

// Complete values at the CLOG durable-format boundary. They deliberately carry no mutexes,
// cache state, dirty bookkeeping, filesystem paths, or CommitLog implementation ownership.
struct CommitLogMetadataImage final {
    std::uint64_t system_identifier{0};
    std::uint64_t generation{0};
    base::Xid next_xid{kFirstNormalXid};
    std::uint32_t segment_count{0};
};

struct CommitLogSegmentImage final {
    std::uint64_t generation{0};
    std::vector<std::byte> payload{};
};

auto encode_commit_log_metadata(const CommitLogMetadataImage& metadata)
    -> base::Result<std::vector<std::byte>>;

auto decode_commit_log_metadata(base::Bytes encoded) -> base::Result<CommitLogMetadataImage>;

auto encode_commit_log_segment(
    std::uint64_t system_identifier,
    std::uint32_t segment_number,
    std::uint64_t generation,
    base::Bytes payload
) -> base::Result<std::vector<std::byte>>;

auto decode_commit_log_segment(
    base::Bytes encoded,
    std::uint64_t system_identifier,
    std::uint32_t expected_segment_number
) -> base::Result<CommitLogSegmentImage>;

}  // namespace db::transaction
