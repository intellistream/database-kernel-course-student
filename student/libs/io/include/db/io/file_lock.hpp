#pragma once

#include <cstdint>
#include <filesystem>

#include "db/base/result.hpp"
#include "db/io/posix.hpp"

namespace db::io {

// A process-scoped, advisory, exclusive lock held by one open file description.
// Closing or destroying the move-only object releases the lock. Contention is
// reported as kUnavailable rather than hidden behind an unbounded wait.
class ExclusiveFileLock final {
public:
    static auto try_acquire(const std::filesystem::path& path, std::uint32_t permissions = 0640U)
        -> base::Result<ExclusiveFileLock>;

    ~ExclusiveFileLock() noexcept = default;

    ExclusiveFileLock(const ExclusiveFileLock&) = delete;
    auto operator=(const ExclusiveFileLock&) -> ExclusiveFileLock& = delete;
    ExclusiveFileLock(ExclusiveFileLock&&) noexcept = default;
    auto operator=(ExclusiveFileLock&&) noexcept -> ExclusiveFileLock& = default;

    auto is_locked() const noexcept -> bool;
    auto release() -> base::Status;

private:
    explicit ExclusiveFileLock(PosixFile file) noexcept;

    PosixFile file_;
};

}  // namespace db::io
