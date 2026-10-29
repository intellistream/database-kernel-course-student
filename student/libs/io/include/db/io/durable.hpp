#pragma once

#include <cstdint>
#include <filesystem>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/io/posix.hpp"

namespace db::io {

// Publish a complete, file-synced image under a previously absent name. The
// final rename is atomic and never replaces an existing destination. The
// caller owns parent-directory sync when the namespace entry must be durable.
auto publish_new_file(
    const std::filesystem::path& path,
    base::Bytes contents,
    std::uint32_t permissions = 0640U
) -> base::Status;

// The same no-replace publication boundary, but transfers the already-synced
// read/write file handle instead of closing and reopening the published name.
// This removes the post-publication reopen window for append-only owners. The
// caller still owns parent-directory sync.
auto publish_new_file_open(
    const std::filesystem::path& path,
    base::Bytes contents,
    std::uint32_t permissions = 0640U
) -> base::Result<PosixFile>;

// Replace a complete file image in one namespace operation: create a unique
// same-directory temporary, sync it, rename it over the destination, then sync
// the parent directory. A returned error after rename means publication may
// have happened but is not known durable.
auto atomic_replace(
    const std::filesystem::path& path,
    base::Bytes contents,
    std::uint32_t permissions = 0640U
) -> base::Status;

// Remove a directory entry and sync its parent before reporting success.
auto remove_file_durable(const std::filesystem::path& path, bool missing_ok = false)
    -> base::Status;

}  // namespace db::io
