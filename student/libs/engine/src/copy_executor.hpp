#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <mutex>
#include <string_view>

#include "db/base/result.hpp"
#include "db/catalog/snapshot.hpp"
#include "db/engine/database.hpp"
#include "db/engine/session.hpp"

#include "physical_services.hpp"

namespace db::engine::detail {

enum class CopyIndexMaintenance : std::uint8_t {
    kIncremental,
    kBulkBuildEmptyIndexes,
};

struct CopyExecutionContext final {
    PhysicalServices services;
    std::mutex& utility_mutex;
    CopyIndexMaintenance index_maintenance{CopyIndexMaintenance::kIncremental};
    std::uint64_t source_size_bytes{0U};
    std::function<base::Status()> checkpoint;
};

auto execute_copy_from_text(
    CopyExecutionContext context,
    Session& session,
    std::string_view sql,
    std::istream& input
) -> base::Result<std::size_t>;

auto execute_copy_from_rows(
    CopyExecutionContext context,
    Session& session,
    std::string_view sql,
    CopyRowReader reader
) -> base::Result<std::size_t>;

}  // namespace db::engine::detail
