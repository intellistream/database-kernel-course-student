#include "db/io/posix.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

#include <sys/stat.h>
#include <sys/types.h>

#include "db/base/error.hpp"

#include "unique_file_descriptor.hpp"

namespace db::io {
namespace {

auto map_errno(const int error_number) noexcept -> base::ErrorCode {
    switch (error_number) {
        case ENOENT:
        case ENOTDIR:
            return base::ErrorCode::kNotFound;
        case EEXIST:
            return base::ErrorCode::kAlreadyExists;
        case EINVAL:
        case EISDIR:
        case ENAMETOOLONG:
            return base::ErrorCode::kInvalidArgument;
        case ENOSPC:
        case EDQUOT:
        case EMFILE:
        case ENFILE:
            return base::ErrorCode::kResourceExhausted;
        default:
            return base::ErrorCode::kIo;
    }
}

auto posix_error(
    const int error_number,
    const std::string_view operation,
    const std::filesystem::path& path
) -> base::Error {
    std::string message{operation};
    message.append(" failed");
    if (!path.empty()) {
        message.append(" for '");
        message.append(path.string());
        message.push_back('\'');
    }
    return base::Error{
        map_errno(error_number),
        std::move(message),
        std::error_code{error_number, std::generic_category()},
    }
        .with_context("io", std::string{operation});
}

auto closed_error(const std::string_view operation) -> base::Error {
    return base::Error{
        base::ErrorCode::kUnavailable,
        "POSIX handle is closed",
    }
        .with_context("io", std::string{operation});
}

auto checked_offset(const std::uint64_t value, const std::string_view operation)
    -> base::Result<off_t> {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
    if (value > maximum) {
        return base::fail(
            base::Error{
                base::ErrorCode::kOutOfRange,
                "file offset is outside the POSIX off_t range",
            }
                .with_context("io", std::string{operation})
        );
    }
    return static_cast<off_t>(value);
}

auto bounded_io_size(const std::size_t requested) noexcept -> std::size_t {
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
    return std::min(requested, maximum);
}

auto advance_offset(
    const std::uint64_t offset,
    const std::size_t transferred,
    const std::string_view operation
) -> base::Result<std::uint64_t> {
    if (transferred > std::numeric_limits<std::uint64_t>::max() - offset) {
        return base::fail(
            base::Error{
                base::ErrorCode::kOutOfRange,
                "file offset overflow",
            }
                .with_context("io", std::string{operation})
        );
    }
    return offset + static_cast<std::uint64_t>(transferred);
}

auto access_flags(const FileAccess access) noexcept -> int {
    switch (access) {
        case FileAccess::kReadOnly:
            return O_RDONLY;
        case FileAccess::kWriteOnly:
            return O_WRONLY;
        case FileAccess::kReadWrite:
            return O_RDWR;
    }
    return O_RDONLY;
}

auto validate_permissions(const std::uint32_t permissions) -> base::Status {
    if ((permissions & ~0777U) != 0U) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "POSIX permissions contain unsupported bits",
            }
        );
    }
    return {};
}

}  // namespace

auto PosixFile::open(const std::filesystem::path& path, const FileOpenOptions options)
    -> base::Result<PosixFile> {
    if (path.empty()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "file path is empty",
            }
        );
    }
    auto permissions = validate_permissions(options.permissions);
    if (!permissions) {
        return base::fail(std::move(permissions).error());
    }
    if (options.exclusive && !options.create) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "exclusive open requires create",
            }
        );
    }
    if (options.truncate && options.access == FileAccess::kReadOnly) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "truncate requires writable file access",
            }
        );
    }

    // Copy the diagnostic path before acquiring a descriptor. Once open(2)
    // succeeds, the private descriptor guard owns the resource until the
    // public move-only handle is fully constructed.
    std::filesystem::path owned_path{path};

    int flags = access_flags(options.access) | O_CLOEXEC;
    if (options.create) {
        flags |= O_CREAT;
    }
    if (options.exclusive) {
        flags |= O_EXCL;
    }
    if (options.truncate) {
        flags |= O_TRUNC;
    }

    int descriptor = -1;
    do {
        // POSIX exposes open(2) as a variadic C API; O_CREAT requires mode.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        descriptor = ::open(path.c_str(), flags, static_cast<mode_t>(options.permissions));
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        return base::fail(posix_error(errno, "open file", path));
    }
    internal::UniqueFileDescriptor acquired{descriptor};
    return PosixFile{acquired.release(), std::move(owned_path)};
}

PosixFile::~PosixFile() noexcept {
    close_best_effort();
}

PosixFile::PosixFile(PosixFile&& other) noexcept
    : descriptor_{std::exchange(other.descriptor_, -1)},
      path_{std::move(other.path_)} {}

auto PosixFile::operator=(PosixFile&& other) noexcept -> PosixFile& {
    if (this != &other) {
        close_best_effort();
        descriptor_ = std::exchange(other.descriptor_, -1);
        path_ = std::move(other.path_);
    }
    return *this;
}

auto PosixFile::is_open() const noexcept -> bool {
    return descriptor_ >= 0;
}

auto PosixFile::native_handle() const noexcept -> int {
    return descriptor_;
}

auto PosixFile::read_at(const std::uint64_t offset, const base::MutableBytes output) const
    -> base::Result<std::size_t> {
    if (!is_open()) {
        return base::fail(closed_error("pread"));
    }
    auto position = checked_offset(offset, "pread");
    if (!position) {
        return base::fail(std::move(position).error());
    }
    if (output.empty()) {
        return 0U;
    }

    ssize_t result = -1;
    do {
        result =
            ::pread(descriptor_, output.data(), bounded_io_size(output.size()), position.value());
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        return base::fail(posix_error(errno, "pread", path_));
    }
    return static_cast<std::size_t>(result);
}

auto PosixFile::read_exact_at(std::uint64_t offset, base::MutableBytes output) const
    -> base::Status {
    while (!output.empty()) {
        auto read = read_at(offset, output);
        if (!read) {
            return base::fail(std::move(read).error());
        }
        if (read.value() == 0U) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "unexpected end of file",
                }
                    .with_context("io", "pread exact")
            );
        }
        auto next = advance_offset(offset, read.value(), "pread exact");
        if (!next) {
            return base::fail(std::move(next).error());
        }
        offset = next.value();
        output = output.subspan(read.value());
    }
    return {};
}

auto PosixFile::write_at(const std::uint64_t offset, const base::Bytes input) const
    -> base::Result<std::size_t> {
    if (!is_open()) {
        return base::fail(closed_error("pwrite"));
    }
    auto position = checked_offset(offset, "pwrite");
    if (!position) {
        return base::fail(std::move(position).error());
    }
    if (input.empty()) {
        return 0U;
    }

    ssize_t result = -1;
    do {
        result =
            ::pwrite(descriptor_, input.data(), bounded_io_size(input.size()), position.value());
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        return base::fail(posix_error(errno, "pwrite", path_));
    }
    return static_cast<std::size_t>(result);
}

auto PosixFile::write_all_at(std::uint64_t offset, base::Bytes input) const -> base::Status {
    while (!input.empty()) {
        auto written = write_at(offset, input);
        if (!written) {
            return base::fail(std::move(written).error());
        }
        if (written.value() == 0U) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "pwrite made no progress",
                }
                    .with_context("io", "pwrite all")
            );
        }
        auto next = advance_offset(offset, written.value(), "pwrite all");
        if (!next) {
            return base::fail(std::move(next).error());
        }
        offset = next.value();
        input = input.subspan(written.value());
    }
    return {};
}

auto PosixFile::size() const -> base::Result<std::uint64_t> {
    if (!is_open()) {
        return base::fail(closed_error("fstat"));
    }

    struct stat metadata{};

    if (::fstat(descriptor_, &metadata) != 0) {
        return base::fail(posix_error(errno, "fstat", path_));
    }
    if (metadata.st_size < 0) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "POSIX file reported a negative size",
            }
        );
    }
    return static_cast<std::uint64_t>(metadata.st_size);
}

auto PosixFile::truncate(const std::uint64_t size) const -> base::Status {
    if (!is_open()) {
        return base::fail(closed_error("ftruncate"));
    }
    auto length = checked_offset(size, "ftruncate");
    if (!length) {
        return base::fail(std::move(length).error());
    }
    int result = -1;
    do {
        result = ::ftruncate(descriptor_, length.value());
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(posix_error(errno, "ftruncate", path_));
    }
    return {};
}

auto PosixFile::sync() const -> base::Status {
    if (!is_open()) {
        return base::fail(closed_error("fsync"));
    }
    int result = -1;
    do {
        result = ::fsync(descriptor_);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(posix_error(errno, "fsync", path_));
    }
    return {};
}

auto PosixFile::sync_data() const -> base::Status {
    if (!is_open()) {
        return base::fail(closed_error("fdatasync"));
    }
    int result = -1;
    do {
        result = ::fdatasync(descriptor_);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(posix_error(errno, "fdatasync", path_));
    }
    return {};
}

auto PosixFile::close() -> base::Status {
    if (!is_open()) {
        return {};
    }
    const int descriptor = std::exchange(descriptor_, -1);
    if (::close(descriptor) != 0) {
        return base::fail(posix_error(errno, "close file", path_));
    }
    return {};
}

PosixFile::PosixFile(const int descriptor, std::filesystem::path path) noexcept
    : descriptor_{descriptor},
      path_{std::move(path)} {}

void PosixFile::set_diagnostic_path(std::filesystem::path path) noexcept {
    path_ = std::move(path);
}

void PosixFile::close_best_effort() noexcept {
    if (is_open()) {
        const int descriptor = std::exchange(descriptor_, -1);
        static_cast<void>(::close(descriptor));
    }
}

auto PosixDirectory::open(const std::filesystem::path& path) -> base::Result<PosixDirectory> {
    if (path.empty()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "directory path is empty",
            }
        );
    }
    std::filesystem::path owned_path{path};
    int descriptor = -1;
    do {
        // POSIX exposes open(2) as a variadic C API even without O_CREAT.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        return base::fail(posix_error(errno, "open directory", path));
    }
    internal::UniqueFileDescriptor acquired{descriptor};
    return PosixDirectory{acquired.release(), std::move(owned_path)};
}

PosixDirectory::~PosixDirectory() noexcept {
    close_best_effort();
}

PosixDirectory::PosixDirectory(PosixDirectory&& other) noexcept
    : descriptor_{std::exchange(other.descriptor_, -1)},
      path_{std::move(other.path_)} {}

auto PosixDirectory::operator=(PosixDirectory&& other) noexcept -> PosixDirectory& {
    if (this != &other) {
        close_best_effort();
        descriptor_ = std::exchange(other.descriptor_, -1);
        path_ = std::move(other.path_);
    }
    return *this;
}

auto PosixDirectory::is_open() const noexcept -> bool {
    return descriptor_ >= 0;
}

auto PosixDirectory::native_handle() const noexcept -> int {
    return descriptor_;
}

auto PosixDirectory::sync() const -> base::Status {
    if (!is_open()) {
        return base::fail(closed_error("fsync directory"));
    }
    int result = -1;
    do {
        result = ::fsync(descriptor_);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(posix_error(errno, "fsync directory", path_));
    }
    return {};
}

auto PosixDirectory::close() -> base::Status {
    if (!is_open()) {
        return {};
    }
    const int descriptor = std::exchange(descriptor_, -1);
    if (::close(descriptor) != 0) {
        return base::fail(posix_error(errno, "close directory", path_));
    }
    return {};
}

PosixDirectory::PosixDirectory(const int descriptor, std::filesystem::path path) noexcept
    : descriptor_{descriptor},
      path_{std::move(path)} {}

void PosixDirectory::close_best_effort() noexcept {
    if (is_open()) {
        const int descriptor = std::exchange(descriptor_, -1);
        static_cast<void>(::close(descriptor));
    }
}

auto create_directories(const std::filesystem::path& path, const std::uint32_t permissions)
    -> base::Status {
    if (path.empty()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "directory path is empty",
            }
        );
    }
    auto valid_permissions = validate_permissions(permissions);
    if (!valid_permissions) {
        return base::fail(std::move(valid_permissions).error());
    }

    std::filesystem::path current;
    for (const auto& component : path) {
        current /= component;
        if (current == path.root_path() || component == ".") {
            continue;
        }
        if (::mkdir(current.c_str(), static_cast<mode_t>(permissions)) == 0) {
            continue;
        }
        const int error_number = errno;
        if (error_number == EEXIST) {
            struct stat metadata{};

            if (::stat(current.c_str(), &metadata) == 0 && S_ISDIR(metadata.st_mode)) {
                continue;
            }
        }
        return base::fail(posix_error(error_number, "create directory", current));
    }
    return {};
}

auto rename_file(const std::filesystem::path& source, const std::filesystem::path& destination)
    -> base::Status {
    if (source.empty() || destination.empty()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "rename requires non-empty source and destination paths",
            }
        );
    }

    int result = -1;
    do {
        result = ::rename(source.c_str(), destination.c_str());
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(posix_error(errno, "rename", source));
    }
    return {};
}

auto remove_file(const std::filesystem::path& path, const bool missing_ok) -> base::Status {
    if (path.empty()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "remove path is empty",
            }
        );
    }

    int result = -1;
    do {
        result = ::unlink(path.c_str());
    } while (result != 0 && errno == EINTR);
    if (result != 0 && (!missing_ok || errno != ENOENT)) {
        return base::fail(posix_error(errno, "unlink", path));
    }
    return {};
}

auto sync_parent_directory(const std::filesystem::path& path) -> base::Status {
    if (path.empty()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "path whose parent is synced is empty",
            }
        );
    }
    auto parent = path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    auto directory = PosixDirectory::open(parent);
    if (!directory) {
        return base::fail(std::move(directory).error());
    }
    return directory.value().sync();
}

}  // namespace db::io
