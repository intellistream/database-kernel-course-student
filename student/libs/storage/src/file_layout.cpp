#include "detail/file_layout.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/io/durable.hpp"

namespace db::storage::detail {
namespace {

[[maybe_unused]] auto segment_suffix(std::uint64_t value) -> std::string {
    constexpr std::array<char, 16>
        digits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string result(16, '0');
    for (auto index = result.size(); index > 0; --index) {
        result[index - 1] = digits.at(value & 0xFU);
        value >>= 4U;
    }
    return result;
}

[[maybe_unused]] auto inactive_layout() -> base::Error {
    return base::Error{
        base::ErrorCode::kUnavailable,
        "relation file layout has been moved from",
    };
}

[[maybe_unused]] auto map_errno(const int error_number) noexcept -> base::ErrorCode {
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

[[maybe_unused]] auto file_error(
    const int error_number,
    const std::string_view operation,
    const std::filesystem::path& path
) -> base::Error {
    std::string message{operation};
    message.append(" failed for '");
    message.append(path.string());
    message.push_back('\'');
    return base::Error{
        map_errno(error_number),
        std::move(message),
        std::error_code{error_number, std::generic_category()},
    }
        .with_context("storage.file_layout", std::string{operation});
}

[[maybe_unused]] auto file_mode(const io::FileAccess access) noexcept -> const char* {
    switch (access) {
        case io::FileAccess::kReadOnly:
            return "r";
        case io::FileAccess::kWriteOnly:
            return "w";
        case io::FileAccess::kReadWrite:
            return "r+";
    }
    return "r+";
}

[[maybe_unused]] auto access_flags(const io::FileAccess access) noexcept -> int {
    switch (access) {
        case io::FileAccess::kReadOnly:
            return O_RDONLY;
        case io::FileAccess::kWriteOnly:
            return O_WRONLY;
        case io::FileAccess::kReadWrite:
            return O_RDWR;
    }
    return O_RDWR;
}

[[maybe_unused]] auto open_native_file(
    const std::filesystem::path& path,
    const io::FileAccess access,
    const bool create,
    const bool exclusive,
    const std::uint32_t permissions
) -> base::Result<NativeFile> {
    int flags = access_flags(access) | O_CLOEXEC;
    if (create) {
        flags |= O_CREAT;
    }
    if (exclusive) {
        flags |= O_EXCL;
    }

    int descriptor = -1;
    do {
        // POSIX open requires the variadic mode argument when O_CREAT is set.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        descriptor = ::open(path.c_str(), flags, static_cast<mode_t>(permissions));
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        return base::fail(file_error(errno, "open file", path));
    }

    std::FILE* stream = ::fdopen(descriptor, file_mode(access));
    if (stream == nullptr) {
        const int error_number = errno;
        static_cast<void>(::close(descriptor));
        return base::fail(file_error(error_number, "fdopen file", path));
    }
    return NativeFile{stream};
}

}  // namespace

auto RelationFileLayout::create(const std::filesystem::path& directory)
    -> base::Result<RelationFileLayout> {
    auto created = io::create_directories(directory);
    if (!created) {
        return base::fail(std::move(created).error());
    }
    return open(directory);
}

auto RelationFileLayout::open(const std::filesystem::path& directory)
    -> base::Result<RelationFileLayout> {
    if (directory.empty()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "relation directory is empty",
            }
        );
    }
    auto handle = io::PosixDirectory::open(directory);
    if (!handle) {
        return base::fail(std::move(handle).error());
    }
    return RelationFileLayout{directory, std::move(handle).value()};
}

RelationFileLayout::RelationFileLayout(std::filesystem::path directory, io::PosixDirectory handle)
    : directory_{std::move(directory)},
      handle_{std::move(handle)} {}

RelationFileLayout::RelationFileLayout(RelationFileLayout&& other) noexcept
    : directory_{std::move(other.directory_)},
      handle_{std::move(other.handle_)} {
    other.directory_.clear();
}

auto RelationFileLayout::operator=(RelationFileLayout&& other) noexcept -> RelationFileLayout& {
    if (this != &other) {
        directory_ = std::move(other.directory_);
        handle_ = std::move(other.handle_);
        other.directory_.clear();
    }
    return *this;
}

auto RelationFileLayout::valid() const noexcept -> bool {
    return !directory_.empty() && handle_.is_open();
}

auto RelationFileLayout::directory() const noexcept -> const std::filesystem::path& {
    return directory_;
}

auto RelationFileLayout::relation_path(const base::RelationId relation_id) const
    -> base::Result<std::filesystem::path> {
    if (!valid()) {
        return base::fail(inactive_layout());
    }
    return directory_ / (std::to_string(relation_id.value()) + ".tbl");
}

auto RelationFileLayout::segment_path(
    const base::RelationId relation_id,
    const std::uint64_t segment_number
) const -> base::Result<std::filesystem::path> {
    auto base_path = relation_path(relation_id);
    if (!base_path) {
        return base::fail(std::move(base_path).error());
    }
    if (segment_number == 0) {
        return std::move(base_path).value();
    }
    return std::filesystem::path{
        std::move(base_path).value().string() + "." + segment_suffix(segment_number)
    };
}

[[maybe_unused]] auto RelationFileLayout::create_segment(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const std::uint64_t segment_number,
    [[maybe_unused]] const std::uint32_t permissions
) const -> base::Result<NativeFile> {
    // LAB-S01.Q01:R08-BEGIN
    static_cast<void>(relation_id);
    static_cast<void>(segment_number);
    static_cast<void>(permissions);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R08: algorithm not implemented"}
    );
    // LAB-S01.Q01:R08-END
}

auto RelationFileLayout::open_segment(
    const base::RelationId relation_id,
    const std::uint64_t segment_number,
    const io::FileAccess access
) const -> base::Result<NativeFile> {
    auto path = segment_path(relation_id, segment_number);
    if (!path) {
        return base::fail(std::move(path).error());
    }
    return open_native_file(path.value(), access, false, false, 0640U);
}

[[maybe_unused]] auto RelationFileLayout::remove_segment(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const std::uint64_t segment_number,
    [[maybe_unused]] const bool missing_ok
) const -> base::Status {
    // LAB-S01.Q01:R09-BEGIN
    static_cast<void>(relation_id);
    static_cast<void>(segment_number);
    static_cast<void>(missing_ok);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R09: algorithm not implemented"}
    );
    // LAB-S01.Q01:R09-END
}

[[maybe_unused]] auto RelationFileLayout::publish_segment(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const std::uint64_t segment_number,
    [[maybe_unused]] const base::Bytes contents,
    [[maybe_unused]] const std::uint32_t permissions
) const -> base::Status {
    // LAB-S01.Q01:R10-BEGIN
    static_cast<void>(relation_id);
    static_cast<void>(segment_number);
    static_cast<void>(contents);
    static_cast<void>(permissions);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R10: algorithm not implemented"}
    );
    // LAB-S01.Q01:R10-END
}

[[maybe_unused]] auto RelationFileLayout::sync() const -> base::Status {
    // LAB-S01.Q01:R11-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R11: algorithm not implemented"}
    );
    // LAB-S01.Q01:R11-END
}

}  // namespace db::storage::detail
