#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"

namespace db::io {

enum class FileAccess : std::uint8_t {
    kReadOnly,
    kWriteOnly,
    kReadWrite,
};

struct FileOpenOptions final {
    FileAccess access{FileAccess::kReadOnly};
    bool create{false};
    bool exclusive{false};
    bool truncate{false};
    std::uint32_t permissions{0640U};
};

class PosixFile final {
public:
    static auto open(const std::filesystem::path& path, FileOpenOptions options = {})
        -> base::Result<PosixFile>;

    ~PosixFile() noexcept;

    PosixFile(const PosixFile&) = delete;
    auto operator=(const PosixFile&) -> PosixFile& = delete;
    PosixFile(PosixFile&& other) noexcept;
    auto operator=(PosixFile&& other) noexcept -> PosixFile&;

    auto is_open() const noexcept -> bool;
    auto native_handle() const noexcept -> int;

    auto read_at(std::uint64_t offset, base::MutableBytes output) const
        -> base::Result<std::size_t>;
    auto read_exact_at(std::uint64_t offset, base::MutableBytes output) const -> base::Status;
    auto write_at(std::uint64_t offset, base::Bytes input) const -> base::Result<std::size_t>;
    auto write_all_at(std::uint64_t offset, base::Bytes input) const -> base::Status;

    auto size() const -> base::Result<std::uint64_t>;
    auto truncate(std::uint64_t size) const -> base::Status;
    auto sync() const -> base::Status;
    auto sync_data() const -> base::Status;
    auto close() -> base::Status;

private:
    friend auto publish_new_file_open(
        const std::filesystem::path& path,
        base::Bytes contents,
        std::uint32_t permissions
    ) -> base::Result<PosixFile>;

    PosixFile(int descriptor, std::filesystem::path path) noexcept;
    void set_diagnostic_path(std::filesystem::path path) noexcept;
    void close_best_effort() noexcept;

    int descriptor_{-1};
    std::filesystem::path path_;
};

class PosixDirectory final {
public:
    static auto open(const std::filesystem::path& path) -> base::Result<PosixDirectory>;

    ~PosixDirectory() noexcept;

    PosixDirectory(const PosixDirectory&) = delete;
    auto operator=(const PosixDirectory&) -> PosixDirectory& = delete;
    PosixDirectory(PosixDirectory&& other) noexcept;
    auto operator=(PosixDirectory&& other) noexcept -> PosixDirectory&;

    auto is_open() const noexcept -> bool;
    auto native_handle() const noexcept -> int;
    auto sync() const -> base::Status;
    auto close() -> base::Status;

private:
    PosixDirectory(int descriptor, std::filesystem::path path) noexcept;
    void close_best_effort() noexcept;

    int descriptor_{-1};
    std::filesystem::path path_;
};

auto create_directories(const std::filesystem::path& path, std::uint32_t permissions = 0750U)
    -> base::Status;

auto rename_file(const std::filesystem::path& source, const std::filesystem::path& destination)
    -> base::Status;
auto remove_file(const std::filesystem::path& path, bool missing_ok = false) -> base::Status;
auto sync_parent_directory(const std::filesystem::path& path) -> base::Status;

}  // namespace db::io
