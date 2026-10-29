#include "db/io/durable.hpp"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

#include "db/base/error.hpp"
#include "db/io/posix.hpp"

namespace db::io {
namespace {

constexpr std::uint32_t kMaximumTemporaryAttempts = 64;

class TemporaryFile final {
public:
    TemporaryFile(std::filesystem::path staged_path, PosixFile staged_file) noexcept
        : path{std::move(staged_path)},
          file{std::move(staged_file)} {}

    TemporaryFile(const TemporaryFile&) = delete;
    auto operator=(const TemporaryFile&) -> TemporaryFile& = delete;

    TemporaryFile(TemporaryFile&& other) noexcept
        : path{std::move(other.path)},
          file{std::move(other.file)},
          owns_path_{std::exchange(other.owns_path_, false)} {}

    auto operator=(TemporaryFile&&) -> TemporaryFile& = delete;

    ~TemporaryFile() noexcept {
        if (!owns_path_ || path.empty()) {
            return;
        }
        int result = -1;
        do {
            result = ::unlink(path.c_str());
        } while (result != 0 && errno == EINTR);
    }

    void release_path() noexcept {
        owns_path_ = false;
    }

    std::filesystem::path path;
    PosixFile file;

private:
    bool owns_path_{true};
};

auto temporary_path_for(const std::filesystem::path& path) -> std::filesystem::path {
    static std::atomic<std::uint64_t> next_temporary_id{0};
    auto parent = path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    std::string name{"."};
    name.append(path.filename().string());
    name.append(".tmp.");
    name.append(std::to_string(static_cast<std::uint64_t>(::getpid())));
    name.push_back('.');
    name.append(std::to_string(next_temporary_id.fetch_add(1, std::memory_order_relaxed)));
    return parent / name;
}

auto create_temporary(const std::filesystem::path& path, const std::uint32_t permissions)
    -> base::Result<TemporaryFile> {
    for (std::uint32_t attempt = 0; attempt < kMaximumTemporaryAttempts; ++attempt) {
        auto temporary_path = temporary_path_for(path);
        auto temporary = PosixFile::open(
            temporary_path,
            FileOpenOptions{
                .access = FileAccess::kReadWrite,
                .create = true,
                .exclusive = true,
                .permissions = permissions,
            }
        );
        if (temporary) {
            return TemporaryFile{std::move(temporary_path), std::move(temporary).value()};
        }
        if (temporary.error().code() != base::ErrorCode::kAlreadyExists) {
            return base::fail(std::move(temporary).error());
        }
    }
    return base::fail(
        base::Error{
            base::ErrorCode::kResourceExhausted,
            "could not allocate a unique publication temporary file",
        }
    );
}

auto cleanup_staged_after_failure(
    const std::filesystem::path& path,
    base::Error primary_error,
    const std::string_view operation
) -> base::Error {
    auto removed = remove_file(path, true);
    if (!removed) {
        return std::move(removed).error().with_context(
            "io",
            std::string{operation} + "; original failure: " + primary_error.to_string()
        );
    }
    return primary_error;
}

auto no_replace_error(const int error_number) -> base::Error {
    base::ErrorCode code = base::ErrorCode::kIo;
    if (error_number == EEXIST) {
        code = base::ErrorCode::kAlreadyExists;
    } else if (error_number == ENOSPC || error_number == EDQUOT) {
        code = base::ErrorCode::kResourceExhausted;
    }
    return base::Error{
        code,
        "could not publish a new complete file",
        std::error_code{error_number, std::generic_category()},
    }
        .with_context("io", "publish new file");
}

auto rename_file_no_replace(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
) -> base::Status {
    int result = -1;
    do {
        result =
            ::renameat2(AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(no_replace_error(errno));
    }
    return {};
}

auto stage_synced_file(
    const std::filesystem::path& path,
    const base::Bytes contents,
    const std::uint32_t permissions
) -> base::Result<TemporaryFile> {
    auto temporary = create_temporary(path, permissions);
    if (!temporary) {
        return base::fail(std::move(temporary).error());
    }
    auto& selected = temporary.value();
    auto written = selected.file.write_all_at(0, contents);
    if (written) {
        written = selected.file.sync();
    }
    if (!written) {
        return base::fail(cleanup_staged_after_failure(
            selected.path,
            std::move(written).error(),
            "remove incomplete publication temporary"
        ));
    }
    return std::move(selected);
}

auto stage_complete_file(
    const std::filesystem::path& path,
    const base::Bytes contents,
    const std::uint32_t permissions
) -> base::Result<std::filesystem::path> {
    auto staged = stage_synced_file(path, contents, permissions);
    if (!staged) {
        return base::fail(std::move(staged).error());
    }
    auto closed = staged->file.close();
    if (!closed) {
        return base::fail(cleanup_staged_after_failure(
            staged->path,
            std::move(closed).error(),
            "remove publication temporary after close failure"
        ));
    }
    auto staged_path = std::move(staged->path);
    staged->release_path();
    return staged_path;
}

auto prepare_publication(const std::filesystem::path& path) -> base::Status {
    if (path.empty() || path.filename().empty()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "publication path must name a file",
            }
        );
    }

    auto parent = path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    auto created_parent = db::io::create_directories(parent);
    if (!created_parent) {
        return base::fail(std::move(created_parent).error());
    }
    return {};
}

}  // namespace

auto publish_new_file(
    const std::filesystem::path& path,
    const base::Bytes contents,
    const std::uint32_t permissions
) -> base::Status {
    auto prepared = prepare_publication(path);
    if (!prepared) {
        return base::fail(std::move(prepared).error());
    }
    auto staged = stage_complete_file(path, contents, permissions);
    if (!staged) {
        return base::fail(std::move(staged).error());
    }
    auto published = rename_file_no_replace(staged.value(), path);
    if (!published) {
        return base::fail(cleanup_staged_after_failure(
            staged.value(),
            std::move(published).error(),
            "remove unpublished complete file"
        ));
    }
    return {};
}

auto publish_new_file_open(
    const std::filesystem::path& path,
    const base::Bytes contents,
    const std::uint32_t permissions
) -> base::Result<PosixFile> {
    auto prepared = prepare_publication(path);
    if (!prepared) {
        return base::fail(std::move(prepared).error());
    }
    auto staged = stage_synced_file(path, contents, permissions);
    if (!staged) {
        return base::fail(std::move(staged).error());
    }
    staged->file.set_diagnostic_path(path);
    auto published = rename_file_no_replace(staged->path, path);
    if (!published) {
        return base::fail(cleanup_staged_after_failure(
            staged->path,
            std::move(published).error(),
            "remove unpublished open file"
        ));
    }
    staged->release_path();
    return std::move(staged->file);
}

auto atomic_replace(
    const std::filesystem::path& path,
    const base::Bytes contents,
    const std::uint32_t permissions
) -> base::Status {
    auto prepared = prepare_publication(path);
    if (!prepared) {
        return base::fail(std::move(prepared).error());
    }
    auto staged = stage_complete_file(path, contents, permissions);
    if (!staged) {
        return base::fail(std::move(staged).error());
    }
    auto renamed = rename_file(staged.value(), path);
    if (!renamed) {
        return base::fail(cleanup_staged_after_failure(
            staged.value(),
            std::move(renamed).error(),
            "remove unpublished replacement file"
        ));
    }
    return sync_parent_directory(path);
}

auto remove_file_durable(const std::filesystem::path& path, const bool missing_ok) -> base::Status {
    auto removed = remove_file(path, missing_ok);
    if (!removed) {
        return removed;
    }
    return sync_parent_directory(path);
}

}  // namespace db::io
