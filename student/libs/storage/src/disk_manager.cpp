#include "db/storage/disk_manager.hpp"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/stat.h>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/io/posix.hpp"

#include "detail/file_layout.hpp"
#include "detail/relation_segment.hpp"

namespace db::storage {
namespace {

struct FileEntry final {
    FileEntry(detail::NativeFile opened_file, std::filesystem::path opened_path)
        : file{std::move(opened_file)},
          path{std::move(opened_path)} {}

    detail::NativeFile file;
    std::filesystem::path path;
    std::mutex io_mutex;
};

struct RelationEntry final {
    RelationEntry(
        const base::RelationId opened_relation_id,
        std::shared_ptr<FileEntry> opened_first_segment
    )
        : relation_id{opened_relation_id},
          first_segment{std::move(opened_first_segment)} {}

    base::RelationId relation_id;
    std::shared_ptr<FileEntry> first_segment;
    std::atomic<std::uint64_t> page_count{0};
    std::mutex allocation_mutex;
    std::mutex segments_mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<FileEntry>> segments;
};

[[maybe_unused]] auto map_errno(const int error_number) noexcept -> base::ErrorCode {
    switch (error_number) {
        case ENOENT:
        case ENOTDIR:
            return base::ErrorCode::kNotFound;
        case EEXIST:
            return base::ErrorCode::kAlreadyExists;
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
        .with_context("storage.file", std::string{operation});
}

[[maybe_unused]] auto seek_file(
    [[maybe_unused]] std::FILE* const file,
    [[maybe_unused]] const std::uint64_t offset,
    [[maybe_unused]] const std::filesystem::path& path
) -> base::Status {
    // LAB-S01.Q01:R12-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R12"});
    // LAB-S01.Q01:R12-END
}

[[maybe_unused]] auto flush_file(
    [[maybe_unused]] std::FILE* const file,
    [[maybe_unused]] const std::filesystem::path& path
) -> base::Status {
    // LAB-S01.Q01:R13-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R13"});
    // LAB-S01.Q01:R13-END
}

[[maybe_unused]] auto read_file(
    [[maybe_unused]] FileEntry& entry,
    [[maybe_unused]] const std::uint64_t offset,
    [[maybe_unused]] Page::Storage& output
) -> base::Status {
    // LAB-S01.Q01:R14-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R14"});
    // LAB-S01.Q01:R14-END
}

[[maybe_unused]] auto write_file(
    [[maybe_unused]] FileEntry& entry,
    [[maybe_unused]] const std::uint64_t offset,
    [[maybe_unused]] const base::Bytes input
) -> base::Status {
    // LAB-S01.Q01:R15-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R15"});
    // LAB-S01.Q01:R15-END
}

[[maybe_unused]] auto file_size([[maybe_unused]] FileEntry& entry) -> base::Result<std::uint64_t> {
    // LAB-S01.Q01:R16-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R16"});
    // LAB-S01.Q01:R16-END
}

[[maybe_unused]] auto
truncate_file([[maybe_unused]] FileEntry& entry, [[maybe_unused]] const std::uint64_t size)
    -> base::Status {
    // LAB-S01.Q01:R17-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R17"});
    // LAB-S01.Q01:R17-END
}

// Extending a preceding segment creates a sparse zero-filled tail. This keeps
// the fixed page-id mapping contiguous when recovery or a raw caller writes a
// page beyond the current relation end.
[[maybe_unused]] auto
extend_file_to([[maybe_unused]] FileEntry& entry, [[maybe_unused]] const std::uint64_t size)
    -> base::Status {
    // LAB-S01.Q01:R18-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R18"});
    // LAB-S01.Q01:R18-END
}

[[maybe_unused]] auto sync_file_data([[maybe_unused]] FileEntry& entry) -> base::Status {
    // LAB-S01.Q01:R19-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R19"});
    // LAB-S01.Q01:R19-END
}

[[maybe_unused]] auto make_page_without_validation(
    const PageKind kind,
    const PageAddress address,
    const std::size_t special_size
) -> base::Result<Page::Storage> {
    static_cast<void>(address);
    Page::Storage bytes{};
    const PageHeader header{
        .kind = kind,
        .lower = kPageHeaderSize,
        .upper = static_cast<std::uint16_t>(kPageSize - special_size),
        .special = static_cast<std::uint16_t>(kPageSize - special_size),
    };
    auto encoded = header.encode(base::MutableBytes{bytes}.first(kPageHeaderSize));
    if (!encoded) {
        return base::fail(std::move(encoded).error());
    }
    return bytes;
}

[[maybe_unused]] auto relation_segment_exists(
    [[maybe_unused]] const detail::RelationFileLayout& layout,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const std::uint64_t segment_number
) -> base::Result<bool> {
    // LAB-S01.Q01:R20-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R20"});
    // LAB-S01.Q01:R20-END
}

}  // namespace

struct FileDiskManager::Impl final {
    explicit Impl(detail::RelationFileLayout opened_layout)
        : layout{std::move(opened_layout)} {}

    auto get_or_open_relation(base::RelationId relation_id, bool create)
        -> base::Result<std::shared_ptr<RelationEntry>>;
    auto get_or_open_segment(
        const std::shared_ptr<RelationEntry>& relation,
        std::uint64_t segment_number,
        bool create
    ) const -> base::Result<std::shared_ptr<FileEntry>>;
    auto open_existing_segment(
        const std::shared_ptr<RelationEntry>& relation,
        std::uint64_t segment_number
    ) const -> base::Result<std::shared_ptr<FileEntry>>;
    auto discover_page_count(const std::shared_ptr<RelationEntry>& relation) const
        -> base::Result<std::uint64_t>;
    auto extend_relation_to(
        const std::shared_ptr<RelationEntry>& relation,
        std::uint64_t current_page_count,
        std::uint64_t target_segment
    ) const -> base::Result<std::shared_ptr<FileEntry>>;
    auto sync_relation_segments(const std::shared_ptr<RelationEntry>& relation) const
        -> base::Status;
    auto remove_relation_segments(base::RelationId relation_id, bool missing_ok) const
        -> base::Status;

    auto open_segment_entry(
        base::RelationId relation_id,
        std::uint64_t segment_number,
        bool create
    ) const -> base::Result<std::shared_ptr<FileEntry>>;

    detail::RelationFileLayout layout;
    std::shared_mutex lifecycle_mutex;
    std::shared_mutex entries_mutex;
    std::unordered_map<base::RelationId, std::shared_ptr<RelationEntry>> entries;
};

[[maybe_unused]] auto FileDiskManager::Impl::open_segment_entry(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const std::uint64_t segment_number,
    [[maybe_unused]] const bool create
) const -> base::Result<std::shared_ptr<FileEntry>> {
    // LAB-S01.Q01:R21-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R21"});
    // LAB-S01.Q01:R21-END
}

[[maybe_unused]] auto FileDiskManager::Impl::get_or_open_segment(
    [[maybe_unused]] const std::shared_ptr<RelationEntry>& relation,
    [[maybe_unused]] const std::uint64_t segment_number,
    [[maybe_unused]] const bool create
) const -> base::Result<std::shared_ptr<FileEntry>> {
    // LAB-S01.Q01:R22-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R22"});
    // LAB-S01.Q01:R22-END
}

[[maybe_unused]] auto FileDiskManager::Impl::open_existing_segment(
    [[maybe_unused]] const std::shared_ptr<RelationEntry>& relation,
    [[maybe_unused]] const std::uint64_t segment_number
) const -> base::Result<std::shared_ptr<FileEntry>> {
    // LAB-S01.Q01:R23-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R23"});
    // LAB-S01.Q01:R23-END
}

[[maybe_unused]] auto FileDiskManager::Impl::discover_page_count(
    [[maybe_unused]] const std::shared_ptr<RelationEntry>& relation
) const -> base::Result<std::uint64_t> {
    // LAB-S01.Q01:R24-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R24"});
    // LAB-S01.Q01:R24-END
}

[[maybe_unused]] auto FileDiskManager::Impl::get_or_open_relation(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const bool create
) -> base::Result<std::shared_ptr<RelationEntry>> {
    // LAB-S01.Q01:R25-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R25"});
    // LAB-S01.Q01:R25-END
}

[[maybe_unused]] auto FileDiskManager::Impl::extend_relation_to(
    [[maybe_unused]] const std::shared_ptr<RelationEntry>& relation,
    [[maybe_unused]] const std::uint64_t current_page_count,
    [[maybe_unused]] const std::uint64_t target_segment
) const -> base::Result<std::shared_ptr<FileEntry>> {
    // LAB-S01.Q01:R26-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R26"});
    // LAB-S01.Q01:R26-END
}

[[maybe_unused]] auto FileDiskManager::Impl::sync_relation_segments(
    [[maybe_unused]] const std::shared_ptr<RelationEntry>& relation
) const -> base::Status {
    // LAB-S01.Q01:R27-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R27"});
    // LAB-S01.Q01:R27-END
}

[[maybe_unused]] auto FileDiskManager::Impl::remove_relation_segments(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const bool missing_ok
) const -> base::Status {
    // LAB-S01.Q01:R28-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R28"});
    // LAB-S01.Q01:R28-END
}

[[maybe_unused]] auto
FileDiskManager::open([[maybe_unused]] const std::filesystem::path& relation_directory)
    -> base::Result<std::unique_ptr<FileDiskManager>> {
    // LAB-S01.Q01:R29-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R29"});
    // LAB-S01.Q01:R29-END
}

FileDiskManager::FileDiskManager(std::unique_ptr<Impl> implementation)
    : implementation_{std::move(implementation)} {}

FileDiskManager::~FileDiskManager() = default;

[[maybe_unused]] auto FileDiskManager::read_page([[maybe_unused]] const PageAddress address)
    -> base::Result<Page> {
    // LAB-S01.Q01:R30-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R30"});
    // LAB-S01.Q01:R30-END
}

[[maybe_unused]] auto FileDiskManager::write_page(
    [[maybe_unused]] const PageAddress address,
    [[maybe_unused]] const Page& page
) -> base::Status {
    // LAB-S01.Q01:R31-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R31"});
    // LAB-S01.Q01:R31-END
}

[[maybe_unused]] auto FileDiskManager::allocate_page(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const PageKind kind,
    [[maybe_unused]] const std::size_t special_size
) -> base::Result<PageAddress> {
    // LAB-S01.Q01:R32-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R32"});
    // LAB-S01.Q01:R32-END
}

[[maybe_unused]] auto
FileDiskManager::page_count([[maybe_unused]] const base::RelationId relation_id)
    -> base::Result<std::uint64_t> {
    // LAB-S01.Q01:R33-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R33"});
    // LAB-S01.Q01:R33-END
}

[[maybe_unused]] auto
FileDiskManager::sync_relation([[maybe_unused]] const base::RelationId relation_id)
    -> base::Status {
    // LAB-S01.Q01:R34-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R34"});
    // LAB-S01.Q01:R34-END
}

[[maybe_unused]] auto FileDiskManager::sync_all() -> base::Status {
    // LAB-S01.Q01:R35-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R35"});
    // LAB-S01.Q01:R35-END
}

[[maybe_unused]] auto FileDiskManager::remove_relation(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const bool missing_ok
) -> base::Status {
    // LAB-S01.Q01:R36-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q01:R36"});
    // LAB-S01.Q01:R36-END
}

}  // namespace db::storage
