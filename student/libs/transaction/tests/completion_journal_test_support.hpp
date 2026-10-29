#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/completion_journal.hpp"

namespace db::transaction::test {

struct CompletionAppend final {
    base::Xid xid{base::kInvalidXid};
    CompletionOutcome outcome{CompletionOutcome::kAbort};
    base::Lsn end_lsn{base::kInvalidLsn};

    auto operator==(const CompletionAppend&) const noexcept -> bool = default;
};

struct CompletionFlush final {
    base::Lsn end_lsn{base::kInvalidLsn};
    std::chrono::microseconds group_delay{};

    auto operator==(const CompletionFlush&) const noexcept -> bool = default;
};

class RecordingCompletionJournal final : public CompletionJournalPort {
public:
    auto append_completion(const base::Xid xid, const CompletionOutcome outcome)
        -> base::Result<base::Lsn> override {
        const std::lock_guard lock{mutex_};
        if (fail_next_append_ == outcome) {
            fail_next_append_.reset();
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "injected completion journal append failure",
                }
            );
        }
        const auto end_lsn = base::Lsn{next_lsn_};
        next_lsn_ += 8U;
        appends_.push_back(CompletionAppend{.xid = xid, .outcome = outcome, .end_lsn = end_lsn});
        return end_lsn;
    }

    auto flush_through(const base::Lsn end_lsn, const std::chrono::microseconds group_delay)
        -> base::Status override {
        const std::lock_guard lock{mutex_};
        flushes_.push_back(CompletionFlush{.end_lsn = end_lsn, .group_delay = group_delay});
        if (fail_next_flush_) {
            fail_next_flush_ = false;
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "injected completion journal flush failure",
                }
            );
        }
        return {};
    }

    void fail_next_flush() {
        const std::lock_guard lock{mutex_};
        fail_next_flush_ = true;
    }

    void fail_next_append(const CompletionOutcome outcome) {
        const std::lock_guard lock{mutex_};
        fail_next_append_ = outcome;
    }

    auto appends() const -> std::vector<CompletionAppend> {
        const std::lock_guard lock{mutex_};
        return appends_;
    }

    auto flushes() const -> std::vector<CompletionFlush> {
        const std::lock_guard lock{mutex_};
        return flushes_;
    }

    auto append_count() const -> std::size_t {
        const std::lock_guard lock{mutex_};
        return appends_.size();
    }

    auto flush_count() const -> std::size_t {
        const std::lock_guard lock{mutex_};
        return flushes_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<CompletionAppend> appends_;
    std::vector<CompletionFlush> flushes_;
    std::uint64_t next_lsn_{8U};
    std::optional<CompletionOutcome> fail_next_append_;
    bool fail_next_flush_{false};
};

}  // namespace db::transaction::test
