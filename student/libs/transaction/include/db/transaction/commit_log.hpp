#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {

class TransactionManager;
class TransactionStartupState;

namespace detail {
class TransactionCompletionAccess;
}  // namespace detail

enum class TransactionStatus : std::uint8_t {
    kInProgress = 0,
    kCommitted = 1,
    kAborted = 2,
};

struct CommitLogOpenOptions final {
    std::uint64_t system_identifier{0};
    bool create{false};
};

// Segmented two-bit transaction status store. The default constructor creates
// an in-memory log; open() creates or restores a database-bound durable log.
class CommitLog final {
public:
    CommitLog();
    ~CommitLog();
    CommitLog(const CommitLog&) = delete;
    auto operator=(const CommitLog&) -> CommitLog& = delete;
    CommitLog(CommitLog&& other) noexcept;
    auto operator=(CommitLog&& other) noexcept -> CommitLog&;

    // Moving transfers the complete in-memory/durable status service. The
    // source becomes inert and mutating operations reject it with kUnavailable.
    auto valid() const noexcept -> bool;

    static auto open(const std::filesystem::path& directory, const CommitLogOpenOptions& options)
        -> base::Result<CommitLog>;

    auto directory() const noexcept -> const std::filesystem::path&;
    auto system_identifier() const noexcept -> std::uint64_t;
    auto next_xid() const -> base::Xid;
    auto durable_next_xid() const -> base::Xid;
    auto segment_count() const -> std::size_t;
    auto segment_path(std::uint32_t segment_number) const -> std::filesystem::path;

    auto extend_to(base::Xid next_xid) -> base::Status;
    auto ensure_xid_allocated(base::Xid xid) -> base::Status;
    auto set_status(base::Xid xid, TransactionStatus status) -> base::Status;
    auto status(base::Xid xid) const -> TransactionStatus;
    auto is_committed(base::Xid xid) const -> bool;
    auto is_aborted(base::Xid xid) const -> bool;
    auto is_in_progress(base::Xid xid) const -> bool;

    auto abort_in_progress_before(base::Xid next_xid) -> base::Status;
    auto flush() -> base::Status;

private:
    friend class detail::TransactionCompletionAccess;
    friend class TransactionManager;
    friend class TransactionStartupState;

    struct Inner;

    explicit CommitLog(std::unique_ptr<Inner> inner) noexcept;
    static auto create_new(const std::filesystem::path& directory, std::uint64_t system_identifier)
        -> base::Result<CommitLog>;
    static auto
    open_existing(const std::filesystem::path& directory, std::uint64_t system_identifier)
        -> base::Result<CommitLog>;
    auto set_status_preallocated(base::Xid xid, TransactionStatus status) -> base::Status;
    auto require_recovery_resolved_before(base::Xid next_xid) const -> base::Status;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::transaction
