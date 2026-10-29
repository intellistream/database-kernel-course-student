#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "db/api/result.hpp"
#include "db/api/session.hpp"
#include "db/base/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::api {

struct DatabaseConfig final {
    std::filesystem::path data_directory;
    std::size_t buffer_pool_pages{64U};
    std::size_t recovery_page_cache_pages{64U};
    std::size_t wal_ring_pages{128U};
    std::uint32_t timeline{1U};
    std::uint64_t system_identifier{0U};
    std::uint32_t directory_permissions{0750U};
    std::size_t maximum_catalog_image_size{
        std::size_t{64U} * std::size_t{1024U} * std::size_t{1024U}
    };
    std::size_t maximum_encoded_row_size{4'036U};

    auto operator==(const DatabaseConfig&) const -> bool = default;
};

using DatabaseOptions = DatabaseConfig;

struct StaticCheckpointOptions final {
    std::chrono::milliseconds drain_timeout{5'000};
};

using CopyRowReader = std::function<base::Result<std::optional<std::vector<types::Value>>>()>;

enum class DatabaseState : std::uint8_t {
    kShutdown,
    kStartingUp,
    kInProduction,
    kShuttingDown,
};

enum class DispatchStatementKind : std::uint8_t {
    kQuery,
    kInsert,
    kUpdate,
    kDelete,
    kTransaction,
    kUtility,
    kExplain,
    kScript,
};

struct DispatchParameters final {
    std::span<const types::DataType> types{};
    std::span<const types::Value> values{};
};

struct DispatchResult final {
    DispatchStatementKind kind{DispatchStatementKind::kScript};
    std::size_t statement_count{0U};
    std::size_t row_count{0U};
    std::size_t affected_row_count{0U};
    std::vector<ResultSet> result_sets;

    auto operator==(const DispatchResult&) const -> bool = default;
};

using PreparedColumn = ResultColumn;

class PreparedStatement final {
public:
    PreparedStatement(PreparedStatement&& other) noexcept;
    auto operator=(PreparedStatement&& other) noexcept -> PreparedStatement&;
    PreparedStatement(const PreparedStatement&) = delete;
    auto operator=(const PreparedStatement&) -> PreparedStatement& = delete;
    ~PreparedStatement();

    auto valid() const noexcept -> bool;
    auto kind() const noexcept -> DispatchStatementKind;
    auto catalog_generation() const noexcept -> std::uint64_t;
    auto parameter_count() const noexcept -> std::size_t;
    auto parameter_types() const noexcept -> std::span<const types::DataType>;
    auto result_columns() const noexcept -> std::span<const PreparedColumn>;

private:
    struct Impl;

    explicit PreparedStatement(std::unique_ptr<Impl> implementation) noexcept;

    friend class Database;

    std::unique_ptr<Impl> implementation_;
};

// Embedded composition facade. All engine, catalog, planner, executor, page,
// latch, and WAL implementation types are confined to its private PImpl.
class Database final {
public:
    static auto create(const DatabaseConfig& config) -> base::Result<Database>;
    static auto open(const DatabaseConfig& config) -> base::Result<Database>;
    // Selects create/open atomically inside the engine directory-lock
    // boundary. Existing invalid metadata is rejected rather than replaced.
    static auto open_or_create(const DatabaseConfig& config) -> base::Result<Database>;
    static auto create(const std::filesystem::path& data_directory, DatabaseConfig config = {})
        -> base::Result<Database>;
    static auto open(const std::filesystem::path& data_directory, DatabaseConfig config = {})
        -> base::Result<Database>;
    static auto
    open_or_create(const std::filesystem::path& data_directory, DatabaseConfig config = {})
        -> base::Result<Database>;

    Database(Database&& other) noexcept;
    // Database close is fallible and preserves retry ownership, so replacing
    // a live facade through a noexcept assignment is intentionally forbidden.
    auto operator=(Database&& other) -> Database& = delete;
    Database(const Database&) = delete;
    auto operator=(const Database&) -> Database& = delete;
    ~Database();

    auto close() -> base::Status;
    auto is_open() const noexcept -> bool;
    auto state() const noexcept -> DatabaseState;
    auto data_directory() const noexcept -> const std::filesystem::path&;
    auto system_identifier() const noexcept -> std::uint64_t;
    auto config() const noexcept -> const DatabaseConfig&;

    auto new_session() -> base::Result<Session>;
    auto session() -> base::Result<Session>;

    // Idle-session-only maintenance operation. Rejects new transactions and
    // implicit commands while existing transactions drain, then durably
    // checkpoints the quiescent database. kUnavailable is retryable.
    auto static_checkpoint(Session& session, StaticCheckpointOptions options = {}) -> base::Status;

    auto dispatch(Session& session, std::string_view sql, DispatchParameters parameters = {})
        -> base::Result<DispatchResult>;

    auto prepare(std::string_view sql, std::span<const types::DataType> parameter_types = {})
        -> base::Result<PreparedStatement>;

    auto execute_prepared(
        Session& session,
        const PreparedStatement& statement,
        std::span<const types::Value> parameter_values = {}
    ) -> base::Result<DispatchResult>;

    auto copy_from(std::string_view sql, std::istream& input, Session& session)
        -> base::Result<std::size_t>;

    auto copy_from_rows(std::string_view sql, CopyRowReader reader, Session& session)
        -> base::Result<std::size_t>;

    auto load_into_empty_from(
        std::string_view sql,
        std::istream& input,
        std::uint64_t source_size_bytes,
        Session& session
    ) -> base::Result<std::size_t>;

private:
    struct Impl;

    explicit Database(std::unique_ptr<Impl> implementation) noexcept;

    static auto open_impl(const DatabaseConfig& config, bool create_database)
        -> base::Result<Database>;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::api
