#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/engine/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::engine {

class DispatchExecutionPort;
class Session;
struct DispatchParameters;
struct DispatchResult;

namespace detail {
class DispatchFactory;
class PreparedFactory;
class DispatchPlanAccess;
}  // namespace detail

class Database;

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

struct DispatchExecutionResult final {
    std::size_t row_count{0U};
    std::size_t affected_row_count{0U};
    std::optional<ResultSet> result_set{};

    auto operator==(const DispatchExecutionResult&) const -> bool = default;
};

struct DispatchParameters final {
    std::span<const types::DataType> types;
    std::span<const types::Value> values;
};

using PreparedColumn = ResultColumn;

// A prepared statement owns one immutable planned statement and the copied
// external parameter type layout used to bind each execution.  It is
// move-only at the public boundary; the physical plan is shared internally so
// every execution can create only a short-lived dispatch view without
// rebuilding parser, binder, or planner state.
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

    friend class Database;
    friend class detail::PreparedFactory;
    friend class detail::DispatchPlanAccess;

    explicit PreparedStatement(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

// An immutable, engine-owned handoff from SQL/planner to an execution
// consumer. The public object intentionally has no planner or executor type;
// an execution adapter can inspect the stable metadata and consume the plan
// only during execute().
class DispatchPlan final {
public:
    DispatchPlan(DispatchPlan&& other) noexcept;
    auto operator=(DispatchPlan&& other) noexcept -> DispatchPlan&;
    DispatchPlan(const DispatchPlan&) = delete;
    auto operator=(const DispatchPlan&) -> DispatchPlan& = delete;
    ~DispatchPlan();

    auto kind() const noexcept -> DispatchStatementKind;
    auto catalog_generation() const noexcept -> std::uint64_t;
    auto parameter_count() const noexcept -> std::size_t;

private:
    struct Impl;

    friend class Database;
    friend class detail::DispatchFactory;
    friend class detail::DispatchPlanAccess;

    explicit DispatchPlan(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

// Consumer-owned execution boundary. Dispatch owns parse/bind/plan ordering,
// catalog guards, and parameter validation/type-modifier adaptation. The
// values passed to execute() therefore match the plan's external parameter
// layout exactly. Access/storage-backed state construction is injected here so
// tests and alternate consumers can replace physical execution without
// exposing planner state through the public facade.
class DispatchExecutionPort {
public:
    DispatchExecutionPort() = default;
    DispatchExecutionPort(const DispatchExecutionPort&) = delete;
    auto operator=(const DispatchExecutionPort&) -> DispatchExecutionPort& = delete;
    DispatchExecutionPort(DispatchExecutionPort&&) = delete;
    auto operator=(DispatchExecutionPort&&) -> DispatchExecutionPort& = delete;
    virtual ~DispatchExecutionPort() = default;

    virtual auto execute(const DispatchPlan& plan, std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> = 0;
};

struct DispatchResult final {
    DispatchStatementKind kind{DispatchStatementKind::kScript};
    std::size_t statement_count{0U};
    std::size_t row_count{0U};
    std::size_t affected_row_count{0U};
    std::vector<ResultSet> result_sets;

    auto operator==(const DispatchResult&) const -> bool = default;
};

}  // namespace db::engine
