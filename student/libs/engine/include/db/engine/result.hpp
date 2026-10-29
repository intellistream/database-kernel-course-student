#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/types/value.hpp"

namespace db::engine {

namespace detail {
class MaterializedResultReceiver;
}  // namespace detail

struct ResultColumn final {
    std::string name;
    types::DataType data_type{types::kUnknownType};

    auto operator==(const ResultColumn&) const -> bool = default;
};

// Immutable output metadata. Duplicate names are intentional: positional SQL
// projections and wildcard joins may expose the same name more than once.
class ResultSchema final {
public:
    ResultSchema() = default;
    ResultSchema(const ResultSchema&) = default;
    auto operator=(const ResultSchema&) -> ResultSchema& = default;
    ResultSchema(ResultSchema&&) noexcept = default;
    auto operator=(ResultSchema&&) noexcept -> ResultSchema& = default;
    ~ResultSchema() = default;

    static auto create(std::vector<ResultColumn> columns) -> base::Result<ResultSchema>;

    auto columns() const noexcept -> std::span<const ResultColumn>;
    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;
    auto position(std::string_view name) const -> base::Result<std::size_t>;
    auto at(std::size_t index) const -> base::Result<std::reference_wrapper<const ResultColumn>>;

    auto operator==(const ResultSchema&) const -> bool = default;

private:
    explicit ResultSchema(std::vector<ResultColumn> columns) noexcept;

    std::vector<ResultColumn> columns_;
};

// A materialized row owns its values and shares the immutable schema. No
// parser, planner, page, or executor object can be reached through this API.
class ResultRow final {
public:
    static auto create(ResultSchema schema, std::vector<types::Value> values)
        -> base::Result<ResultRow>;

    ResultRow(const ResultRow&) = default;
    auto operator=(const ResultRow&) -> ResultRow& = default;
    ResultRow(ResultRow&&) noexcept = default;
    auto operator=(ResultRow&&) noexcept -> ResultRow& = default;
    ~ResultRow() = default;

    auto valid() const noexcept -> bool;
    auto schema() const noexcept -> const ResultSchema&;
    auto values() const noexcept -> std::span<const types::Value>;
    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;
    auto at(std::size_t index) const -> base::Result<std::reference_wrapper<const types::Value>>;
    auto at(std::string_view name) const
        -> base::Result<std::reference_wrapper<const types::Value>>;

    auto operator==(const ResultRow& other) const -> bool;

private:
    friend class ResultSet;
    friend class detail::MaterializedResultReceiver;

    ResultRow(
        std::shared_ptr<const ResultSchema> schema,
        std::vector<types::Value> values
    ) noexcept;

    static auto empty_schema() noexcept -> const ResultSchema&;

    std::shared_ptr<const ResultSchema> schema_;
    std::vector<types::Value> values_;
};

// Rvalue handoff form for composition-layer adapters. It carries the canonical
// materialized values directly rather than wrapping a ResultSet in another
// validation or view type.
struct ResultSetData final {
    std::vector<ResultColumn> columns;
    std::vector<std::vector<types::Value>> rows;
};

// A result set owns the schema and every materialized row. It is copyable for
// value-oriented callers and movable for efficient dispatch handoff.
class ResultSet final {
public:
    static auto create(ResultSchema schema, std::vector<std::vector<types::Value>> rows)
        -> base::Result<ResultSet>;

    ResultSet(const ResultSet&) = default;
    auto operator=(const ResultSet&) -> ResultSet& = default;
    ResultSet(ResultSet&&) noexcept = default;
    auto operator=(ResultSet&&) noexcept -> ResultSet& = default;
    ~ResultSet() = default;

    auto valid() const noexcept -> bool;
    auto schema() const noexcept -> const ResultSchema&;
    auto rows() const noexcept -> std::span<const ResultRow>;
    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;
    auto at(std::size_t index) const -> base::Result<std::reference_wrapper<const ResultRow>>;

    // Consumes this result and transfers row-value ownership to an adapter.
    // The source becomes the same inert state as a moved-from ResultSet.
    auto into_data() && -> ResultSetData;

    auto operator==(const ResultSet& other) const -> bool;

private:
    friend class detail::MaterializedResultReceiver;

    ResultSet(std::shared_ptr<const ResultSchema> schema, std::vector<ResultRow> rows) noexcept;

    static auto empty_schema() noexcept -> const ResultSchema&;

    std::shared_ptr<const ResultSchema> schema_;
    std::vector<ResultRow> rows_;
};

}  // namespace db::engine
