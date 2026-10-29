#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/io/posix.hpp"

namespace db::storage::detail {

struct FileCloser final {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            // NativeFile owns this C stream at the C API boundary.
            static_cast<void>(std::fclose(file));  // NOLINT(cppcoreguidelines-owning-memory)
        }
    }
};

using NativeFile = std::unique_ptr<std::FILE, FileCloser>;

// Owns the durable namespace for relation files. Segment zero is the base
// relation path; later segments append a fixed-width hexadecimal suffix.
class RelationFileLayout final {
public:
    static auto create(const std::filesystem::path& directory) -> base::Result<RelationFileLayout>;
    static auto open(const std::filesystem::path& directory) -> base::Result<RelationFileLayout>;

    RelationFileLayout(const RelationFileLayout&) = delete;
    auto operator=(const RelationFileLayout&) -> RelationFileLayout& = delete;
    RelationFileLayout(RelationFileLayout&& other) noexcept;
    auto operator=(RelationFileLayout&& other) noexcept -> RelationFileLayout&;
    ~RelationFileLayout() = default;

    // Moving transfers both the namespace and its open directory handle. The
    // source becomes inert so it cannot resolve paths relative to the process
    // working directory.
    auto valid() const noexcept -> bool;
    auto directory() const noexcept -> const std::filesystem::path&;
    auto relation_path(base::RelationId relation_id) const -> base::Result<std::filesystem::path>;
    auto segment_path(base::RelationId relation_id, std::uint64_t segment_number) const
        -> base::Result<std::filesystem::path>;

    auto create_segment(
        base::RelationId relation_id,
        std::uint64_t segment_number,
        std::uint32_t permissions = 0640U
    ) const -> base::Result<NativeFile>;
    auto open_segment(
        base::RelationId relation_id,
        std::uint64_t segment_number,
        io::FileAccess access = io::FileAccess::kReadWrite
    ) const -> base::Result<NativeFile>;
    auto remove_segment(
        base::RelationId relation_id,
        std::uint64_t segment_number,
        bool missing_ok = false
    ) const -> base::Status;
    auto publish_segment(
        base::RelationId relation_id,
        std::uint64_t segment_number,
        base::Bytes contents,
        std::uint32_t permissions = 0640U
    ) const -> base::Status;
    auto sync() const -> base::Status;

private:
    RelationFileLayout(std::filesystem::path directory, io::PosixDirectory handle);

    std::filesystem::path directory_;
    io::PosixDirectory handle_;
};

}  // namespace db::storage::detail
