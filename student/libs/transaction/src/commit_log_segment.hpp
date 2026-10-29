#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <vector>

#include "db/transaction/commit_log.hpp"

#include "commit_log_format.hpp"

namespace db::transaction {

struct CommitLogSegmentSnapshot final {
    std::vector<std::byte> payload;
    std::array<std::uint64_t, kCommitLogPagesPerSegment> page_generations{};
    std::uint64_t generation{0};
};

enum class CommitLogStatusWrite : std::uint8_t {
    kUnchanged,
    kChanged,
    kConflict,
};

// Owns one segment's packed statuses and page generations. Dirty-set membership and
// filesystem publication remain the CommitLog service's responsibility.
class CommitLogSegment final {
public:
    explicit CommitLogSegment(std::uint64_t initial_generation, bool initially_dirty = false);
    CommitLogSegment(std::uint64_t durable_generation, std::vector<std::byte> payload);

    auto status(std::uint32_t segment_offset) const -> TransactionStatus;
    auto publish(std::uint32_t segment_offset, TransactionStatus status) -> CommitLogStatusWrite;
    auto abort_in_progress(std::uint32_t begin_offset, std::uint32_t end_offset) -> bool;

    auto snapshot() const -> CommitLogSegmentSnapshot;
    auto has_changed_since(std::uint64_t generation) const noexcept -> bool;
    void mark_snapshot_persisted(const CommitLogSegmentSnapshot& snapshot);
    auto is_dirty() const -> bool;

    auto dirty_queued() const noexcept -> bool;
    auto enqueue_if_clean() noexcept -> bool;
    void reset_dirty_queued() noexcept;
    void restore_dirty_queued() noexcept;

private:
    mutable std::array<std::shared_mutex, kCommitLogPagesPerSegment> page_mutexes_;
    std::vector<std::byte> bytes_;
    std::array<std::uint64_t, kCommitLogPagesPerSegment> page_generations_{};
    std::array<std::uint64_t, kCommitLogPagesPerSegment> persisted_page_generations_{};
    std::atomic<std::uint64_t> generation_{1};
    std::atomic<bool> dirty_queued_{false};
};

}  // namespace db::transaction
