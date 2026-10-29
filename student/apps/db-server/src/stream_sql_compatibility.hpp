#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/protocol/session_port.hpp"
#include "db/types/value.hpp"

namespace db::server::internal {

// The historical RMDB client has a small set of frontend-only SQL spellings.
// These helpers deliberately recognize only that fixed surface; they do not
// parse, bind, or otherwise change the engine's SQL semantics.
enum class StreamControl : std::uint8_t {
    kBegin,
    kCommit,
    kRollback,
    kReadCommitted,
    kRepeatableRead,
    kSerializable,
};

struct LoadRequest final {
    std::filesystem::path source;
    std::string table;
};

auto parse_stream_control(std::string_view sql) -> std::optional<StreamControl>;
auto is_load_statement(std::string_view sql) noexcept -> bool;
auto parse_load_statement(std::string_view sql) -> base::Result<LoadRequest>;
auto has_multiple_top_level_statements(std::string_view sql) noexcept -> bool;

auto rewrite_legacy_create_index(std::string_view sql, std::uint64_t& next_index_id)
    -> base::Result<std::optional<std::string>>;

// RMDB's historical client represents an empty, scalar numeric SUM as zero.
// The caller passes only materialized protocol-facing rows, so this rule cannot
// retain an engine cursor or alter ordinary shell/API aggregate semantics.
void normalize_empty_stream_sums(
    std::string_view sql,
    std::span<const protocol::ColumnMetadata> columns,
    std::vector<std::vector<types::Value>>& rows
);

}  // namespace db::server::internal
