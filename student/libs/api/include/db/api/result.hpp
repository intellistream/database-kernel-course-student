#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::api {

namespace detail {
class ResultFactory;
}  // namespace detail

using Error = base::Error;
using ErrorCode = base::ErrorCode;
using TimestampValue = types::TimestampValue;
using TypeId = base::TypeId;
using TypeModifier = types::TypeModifier;
using DataType = types::DataType;
using Value = types::Value;

// Stable database-facing error categories. The complete base::Error remains
// available to callers; this enum permits branching without parsing text.
enum class DatabaseErrorKind : std::uint8_t {
    kParse,
    kBind,
    kPlan,
    kExecution,
    kSerializationFailure,
    kDeadlockDetected,
    kTransactionState,
    kInvalidArgument,
    kNotFound,
    kUnsupported,
    kIo,
    kUnavailable,
    kInternal,
};

auto database_error_kind(base::ErrorCode code) noexcept -> DatabaseErrorKind;
auto database_error_kind_name(DatabaseErrorKind kind) noexcept -> std::string_view;

struct ResultColumn final {
    std::string name;
    types::DataType data_type{types::kUnknownType};

    auto operator==(const ResultColumn&) const -> bool = default;
};

// Immutable, copyable output metadata. Duplicate names are preserved and
// positional access remains authoritative.
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
    friend class detail::ResultFactory;

    explicit ResultSchema(std::vector<ResultColumn> columns) noexcept;

    std::vector<ResultColumn> columns_;
};

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
    friend class detail::ResultFactory;

    ResultRow(
        std::shared_ptr<const ResultSchema> schema,
        std::vector<types::Value> values
    ) noexcept;

    static auto empty_schema() noexcept -> const ResultSchema&;

    std::shared_ptr<const ResultSchema> schema_;
    std::vector<types::Value> values_;
};

// API-owned data extracted from a materialized result. The immutable schema
// remains shared (and is non-null on success); mutable rows retain their original
// Value storage, including long text.
struct ResultSetData final {
    std::shared_ptr<const ResultSchema> schema;
    std::vector<std::vector<types::Value>> rows;
};

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

    // Consumes only a mutable rvalue. Success invalidates all borrowed schema,
    // row and value views from this result and leaves it invalid with an empty
    // schema and zero rows. Independent value copies remain valid. Allocation
    // failure leaves this result unchanged; consuming an invalid result returns
    // kInvalidArgument. The returned data owns its schema and row values.
    auto into_data() && -> base::Result<ResultSetData>;

    auto operator==(const ResultSet& other) const -> bool;

private:
    friend class detail::ResultFactory;

    ResultSet(std::shared_ptr<const ResultSchema> schema, std::vector<ResultRow> rows) noexcept;

    static auto empty_schema() noexcept -> const ResultSchema&;

    std::shared_ptr<const ResultSchema> schema_;
    std::vector<ResultRow> rows_;
};

}  // namespace db::api
