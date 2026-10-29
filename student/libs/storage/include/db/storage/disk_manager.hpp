#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"

namespace db::storage {

class DiskManager {
public:
    DiskManager() = default;
    DiskManager(const DiskManager&) = delete;
    auto operator=(const DiskManager&) -> DiskManager& = delete;
    DiskManager(DiskManager&&) = delete;
    auto operator=(DiskManager&&) -> DiskManager& = delete;
    virtual ~DiskManager() = default;

    // Implementations report operational failures through Result and do not
    // throw. This is a raw page transport boundary: successful reads and
    // writes do not validate the address or page image. BufferPool converts
    // escaped exceptions, and mutating implementations must remain safe to
    // retry if completion is uncertain.
    virtual auto read_page(PageAddress address) -> base::Result<Page> = 0;
    virtual auto write_page(PageAddress address, const Page& page) -> base::Status = 0;
    virtual auto
    allocate_page(base::RelationId relation_id, PageKind kind, std::size_t special_size = 0)
        -> base::Result<PageAddress> = 0;
    virtual auto page_count(base::RelationId relation_id) -> base::Result<std::uint64_t> = 0;
    virtual auto sync_relation(base::RelationId relation_id) -> base::Status = 0;
    virtual auto sync_all() -> base::Status = 0;
    virtual auto remove_relation(base::RelationId relation_id, bool missing_ok = false)
        -> base::Status = 0;
};

class FileDiskManager final : public DiskManager {
public:
    static auto open(const std::filesystem::path& relation_directory)
        -> base::Result<std::unique_ptr<FileDiskManager>>;

    FileDiskManager(const FileDiskManager&) = delete;
    auto operator=(const FileDiskManager&) -> FileDiskManager& = delete;
    FileDiskManager(FileDiskManager&&) = delete;
    auto operator=(FileDiskManager&&) -> FileDiskManager& = delete;
    ~FileDiskManager() override;

    auto read_page(PageAddress address) -> base::Result<Page> override;
    auto write_page(PageAddress address, const Page& page) -> base::Status override;
    auto allocate_page(base::RelationId relation_id, PageKind kind, std::size_t special_size = 0)
        -> base::Result<PageAddress> override;
    auto page_count(base::RelationId relation_id) -> base::Result<std::uint64_t> override;
    auto sync_relation(base::RelationId relation_id) -> base::Status override;
    auto sync_all() -> base::Status override;
    auto remove_relation(base::RelationId relation_id, bool missing_ok = false)
        -> base::Status override;

private:
    struct Impl;

    explicit FileDiskManager(std::unique_ptr<Impl> implementation);

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::storage
