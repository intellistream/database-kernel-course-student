#include "db/io/file_lock.hpp"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include <sys/file.h>

#include "db/base/error.hpp"

namespace db::io {
namespace {

auto lock_error(const int error_number) -> base::Error {
    return base::Error{
        base::ErrorCode::kIo,
        "could not acquire exclusive file lock",
        std::error_code{error_number, std::generic_category()},
    }
        .with_context("io.file_lock", "acquire exclusive");
}

auto contention_error(const int error_number) -> base::Error {
    return base::Error{
        base::ErrorCode::kUnavailable,
        "file is already locked",
        std::error_code{error_number, std::generic_category()},
    }
        .with_context("io.file_lock", "acquire exclusive");
}

}  // namespace

auto ExclusiveFileLock::try_acquire(
    const std::filesystem::path& path,
    const std::uint32_t permissions
) -> base::Result<ExclusiveFileLock> {
    auto file = PosixFile::open(
        path,
        FileOpenOptions{
            .access = FileAccess::kReadWrite,
            .create = true,
            .permissions = permissions,
        }
    );
    if (!file) {
        return base::fail(std::move(file).error().with_context("io.file_lock", "open lock file"));
    }

    int result = -1;
    do {
        result = ::flock(file->native_handle(), LOCK_EX | LOCK_NB);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        const int error_number = errno;
        if (error_number == EWOULDBLOCK) {
            return base::fail(contention_error(error_number));
        }
        return base::fail(lock_error(error_number));
    }
    return ExclusiveFileLock{std::move(file).value()};
}

ExclusiveFileLock::ExclusiveFileLock(PosixFile file) noexcept
    : file_{std::move(file)} {}

auto ExclusiveFileLock::is_locked() const noexcept -> bool {
    return file_.is_open();
}

auto ExclusiveFileLock::release() -> base::Status {
    auto closed = file_.close();
    if (!closed) {
        return base::fail(
            std::move(closed).error().with_context("io.file_lock", "release exclusive")
        );
    }
    return {};
}

}  // namespace db::io
