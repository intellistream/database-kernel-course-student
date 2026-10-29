#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/engine/dispatch.hpp"
#include "db/engine/session.hpp"
#include "db/types/value.hpp"

namespace db::engine {

// Options owned by the composition root. They describe resource sizing and
// format identities, but do not expose a storage or WAL implementation type.
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

// A COPY row source is pulled by the database one row at a time.  The
// optional terminator keeps the producer independent of engine buffering and
// lets large imports remain bounded by the COPY writer's batch limits.
using CopyRowReader = std::function<base::Result<std::optional<std::vector<types::Value>>>()>;

enum class DatabaseState : std::uint8_t {
    kShutdown,
    kStartingUp,
    kInProduction,
    kShuttingDown,
};

// Database is the process-local composition root for durable services. The
// public facade deliberately exposes no page, latch, executor, catalog, WAL,
// or transaction-manager implementation type; engine operations borrow those
// services through private ports and session facades.
class Database final {
public:
    static auto create(const DatabaseConfig& config) -> base::Result<Database>;
    static auto open(const DatabaseConfig& config) -> base::Result<Database>;
    // Opens an existing control generation or initializes one when it is
    // absent. The choice is made while holding the database-directory lock,
    // so applications do not inspect engine-private paths or race another
    // cooperating opener. An existing but invalid control image is opened and
    // rejected; it is never replaced as a new database.
    static auto open_or_create(const DatabaseConfig& config) -> base::Result<Database>;

    static auto create(const std::filesystem::path& data_directory, DatabaseConfig config = {})
        -> base::Result<Database>;
    static auto open(const std::filesystem::path& data_directory, DatabaseConfig config = {})
        -> base::Result<Database>;
    static auto
    open_or_create(const std::filesystem::path& data_directory, DatabaseConfig config = {})
        -> base::Result<Database>;

    Database(Database&& other) noexcept;
    // Closing a live database may fail and must retain ownership for retry, so
    // replacement cannot be represented by a noexcept assignment operator.
    auto operator=(Database&& other) -> Database& = delete;
    Database(const Database&) = delete;
    auto operator=(const Database&) -> Database& = delete;
    ~Database();

    // Idempotent after a successful close. Once shutdown state is durable, a
    // later failure leaves the facade retained only for a close retry; it no
    // longer accepts work because a crash must recover that shutdown boundary.
    auto close() -> base::Status;

    auto is_open() const noexcept -> bool;
    auto state() const noexcept -> DatabaseState;
    auto data_directory() const noexcept -> const std::filesystem::path&;
    auto system_identifier() const noexcept -> std::uint64_t;
    auto config() const noexcept -> const DatabaseConfig&;

    // Creates a session whose lifetime must end before this Database is
    // closed. The session owns its transaction ProcArray lease and never
    // exposes the lower transaction manager.
    auto new_session() -> base::Result<Session>;
    auto session() -> base::Result<Session>;

    // Idle-session-only maintenance operation. Rejects new transactions and
    // implicit commands while existing transactions drain, then durably
    // checkpoints the quiescent database. kUnavailable is retryable.
    auto static_checkpoint(Session& session, StaticCheckpointOptions options = {}) -> base::Status;

    // Parses, binds, plans, and hands each executable statement to the
    // consumer-owned execution port. Catalog binding/planning use one
    // immutable snapshot and reject generation drift before handoff.
    auto dispatch(
        Session& session,
        std::string_view sql,
        DispatchExecutionPort& execution,
        DispatchParameters parameters = {}
    ) -> base::Result<DispatchResult>;

    // Built-in storage-backed query/DML execution for application sessions.
    // The explicit port overload above remains available for deterministic
    // tests and alternate consumers; this overload owns the composition
    // adapter inside the database boundary.
    auto dispatch(Session& session, std::string_view sql, DispatchParameters parameters = {})
        -> base::Result<DispatchResult>;

    // Parses, binds and lowers one reusable DML/query statement. The supplied
    // parameter type span is copied before this call returns; the resulting
    // plan retains no parser, binder, catalog-port or caller span.
    auto prepare(std::string_view sql, std::span<const types::DataType> parameter_types = {})
        -> base::Result<PreparedStatement>;

    // Executes a prepared statement against this database and session. Values
    // are checked against the copied base types and target typmods are applied
    // to an execution-local vector when needed; the caller's values are never
    // mutated. The immutable plan is rejected if its catalog generation is
    // stale rather than silently replanned.
    auto execute_prepared(
        Session& session,
        const PreparedStatement& statement,
        DispatchExecutionPort& execution,
        std::span<const types::Value> parameter_values = {}
    ) -> base::Result<DispatchResult>;

    auto execute_prepared(
        Session& session,
        const PreparedStatement& statement,
        std::span<const types::Value> parameter_values = {}
    ) -> base::Result<DispatchResult>;

    // Executes one bound `COPY ... FROM STDIN` header and pulls PostgreSQL
    // text-format lines from input.  The caller owns the surrounding Session
    // transaction; implicit sessions commit on success and explicit blocks
    // remain open until commit, while any error follows Session's normal
    // failed-transaction rules.
    auto copy_from(std::string_view sql, std::istream& input, Session& session)
        -> base::Result<std::size_t>;

    // Typed in-process COPY path.  Values must have the same base type as
    // their target column (typed NULL is accepted); target type modifiers are
    // applied by the writer before bounded heap/index insertion.
    auto copy_from_rows(std::string_view sql, CopyRowReader reader, Session& session)
        -> base::Result<std::size_t>;

    // Compatibility LOAD path for a physically empty table with rootless
    // indexes. It retains COPY text decoding and transaction visibility, but
    // emits no heap, B+Tree, or transaction WAL: it first synchronizes the
    // complete physical image through a checkpoint, then durably publishes
    // CLOG. Any nonempty heap or rooted index is rejected rather than falling
    // back to WAL-backed incremental maintenance.
    auto load_into_empty_from(
        std::string_view sql,
        std::istream& input,
        std::uint64_t source_size_bytes,
        Session& session
    ) -> base::Result<std::size_t>;

private:
    struct Impl;
    enum class OpenMode : std::uint8_t;

    explicit Database(std::shared_ptr<Impl> implementation) noexcept;

    static auto open_impl(const DatabaseConfig& config, OpenMode mode) -> base::Result<Database>;

    std::shared_ptr<Impl> implementation_;
};

}  // namespace db::engine
