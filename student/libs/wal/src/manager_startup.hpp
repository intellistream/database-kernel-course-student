#pragma once

#include <filesystem>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/writer.hpp"

namespace db::wal::manager_detail {

struct ManagerStartup final {
    WalWriter writer;
    base::Lsn stream_start_lsn{base::kInvalidLsn};
    base::Lsn end_lsn{base::kInvalidLsn};
    bool repaired_incomplete_tail{false};
};

auto prepare_startup(const std::filesystem::path& directory, const WalManagerOptions& options)
    -> base::Result<ManagerStartup>;

}  // namespace db::wal::manager_detail
