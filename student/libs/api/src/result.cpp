#include "db/api/result.hpp"

#include <string>
#include <utility>

#include "db/base/exception_boundary.hpp"
#include "db/types/type_registry.hpp"

namespace db::api {
namespace {

inline constexpr base::ExceptionBoundaryPolicy kResultExceptionBoundary{
    .component = "api.result",
    .allocation_message = "embedded result materialization could not allocate bounded state",
    .exception_prefix = "unexpected exception escaped embedded result handling: ",
    .unknown_exception_message = "unknown exception escaped embedded result handling",
};

template <typename Callback>
auto result_boundary(const std::string_view operation, Callback&& callback) {
    return base::invoke_exception_boundary(
        kResultExceptionBoundary,
        operation,
        std::forward<Callback>(callback)
    );
}

auto result_error(const base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("api.result", std::move(operation));
}

auto validate_schema_column(const ResultColumn& column, const std::size_t index) -> base::Status {
    if (column.name.empty()) {
        return base::fail(result_error(
            base::ErrorCode::kInvalidArgument,
            "result column " + std::to_string(index + 1U) + " has an empty name",
            "create result schema"
        ));
    }
    if (column.data_type.id() == types::kUnknownTypeId) {
        return base::fail(result_error(
            base::ErrorCode::kInvalidArgument,
            "result column " + std::to_string(index + 1U) + " has UNKNOWN type",
            "create result schema"
        ));
    }
    auto valid = types::validate_data_type(column.data_type);
    if (!valid) {
        return base::fail(
            std::move(valid).error().with_context("api.result", "create result schema")
        );
    }
    return {};
}

auto validate_row_values(const ResultSchema& schema, const std::span<const types::Value> values)
    -> base::Status {
    if (values.size() != schema.size()) {
        return base::fail(result_error(
            base::ErrorCode::kInvalidArgument,
            "result row value count does not match result schema",
            "create result row"
        ));
    }
    const auto columns = schema.columns();
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (values[index].data_type() != columns[index].data_type) {
            return base::fail(result_error(
                base::ErrorCode::kInvalidArgument,
                "result row value type does not match result column " + std::to_string(index + 1U),
                "create result row"
            ));
        }
    }
    return {};
}

}  // namespace

auto database_error_kind(const base::ErrorCode code) noexcept -> DatabaseErrorKind {
    switch (code) {
        case base::ErrorCode::kParse:
            return DatabaseErrorKind::kParse;
        case base::ErrorCode::kBind:
            return DatabaseErrorKind::kBind;
        case base::ErrorCode::kPlan:
            return DatabaseErrorKind::kPlan;
        case base::ErrorCode::kExecution:
            return DatabaseErrorKind::kExecution;
        case base::ErrorCode::kSerializationFailure:
            return DatabaseErrorKind::kSerializationFailure;
        case base::ErrorCode::kDeadlockDetected:
            return DatabaseErrorKind::kDeadlockDetected;
        case base::ErrorCode::kTransactionState:
            return DatabaseErrorKind::kTransactionState;
        case base::ErrorCode::kInvalidArgument:
        case base::ErrorCode::kOutOfRange:
            return DatabaseErrorKind::kInvalidArgument;
        case base::ErrorCode::kNotFound:
            return DatabaseErrorKind::kNotFound;
        case base::ErrorCode::kUnsupported:
            return DatabaseErrorKind::kUnsupported;
        case base::ErrorCode::kIo:
            return DatabaseErrorKind::kIo;
        case base::ErrorCode::kUnavailable:
            return DatabaseErrorKind::kUnavailable;
        case base::ErrorCode::kAlreadyExists:
        case base::ErrorCode::kCorruption:
        case base::ErrorCode::kInvariantViolation:
        case base::ErrorCode::kResourceExhausted:
        case base::ErrorCode::kCancelled:
        case base::ErrorCode::kInternal:
        case base::ErrorCode::kRebuildRequired:
            return DatabaseErrorKind::kInternal;
    }
    return DatabaseErrorKind::kInternal;
}

auto database_error_kind_name(const DatabaseErrorKind kind) noexcept -> std::string_view {
    switch (kind) {
        case DatabaseErrorKind::kParse:
            return "parse";
        case DatabaseErrorKind::kBind:
            return "bind";
        case DatabaseErrorKind::kPlan:
            return "plan";
        case DatabaseErrorKind::kExecution:
            return "execution";
        case DatabaseErrorKind::kSerializationFailure:
            return "serialization_failure";
        case DatabaseErrorKind::kDeadlockDetected:
            return "deadlock_detected";
        case DatabaseErrorKind::kTransactionState:
            return "transaction_state";
        case DatabaseErrorKind::kInvalidArgument:
            return "invalid_argument";
        case DatabaseErrorKind::kNotFound:
            return "not_found";
        case DatabaseErrorKind::kUnsupported:
            return "unsupported";
        case DatabaseErrorKind::kIo:
            return "io";
        case DatabaseErrorKind::kUnavailable:
            return "unavailable";
        case DatabaseErrorKind::kInternal:
            return "internal";
    }
    return "internal";
}

ResultSchema::ResultSchema(std::vector<ResultColumn> columns) noexcept
    : columns_{std::move(columns)} {}

auto ResultSchema::create(std::vector<ResultColumn> columns) -> base::Result<ResultSchema> {
    return result_boundary("create result schema", [&]() -> base::Result<ResultSchema> {
        for (std::size_t index = 0U; index < columns.size(); ++index) {
            auto valid = validate_schema_column(columns[index], index);
            if (!valid) {
                return base::fail(std::move(valid).error());
            }
        }
        return ResultSchema{std::move(columns)};
    });
}

auto ResultSchema::columns() const noexcept -> std::span<const ResultColumn> {
    return columns_;
}

auto ResultSchema::size() const noexcept -> std::size_t {
    return columns_.size();
}

auto ResultSchema::empty() const noexcept -> bool {
    return columns_.empty();
}

auto ResultSchema::position(const std::string_view name) const -> base::Result<std::size_t> {
    return result_boundary("lookup result column", [&]() -> base::Result<std::size_t> {
        for (std::size_t index = 0U; index < columns_.size(); ++index) {
            if (columns_[index].name == name) {
                return index;
            }
        }
        return base::fail(result_error(
            base::ErrorCode::kNotFound,
            "result column was not found: " + std::string{name},
            "lookup result column"
        ));
    });
}

auto ResultSchema::at(const std::size_t index) const
    -> base::Result<std::reference_wrapper<const ResultColumn>> {
    return result_boundary(
        "access result column",
        [&]() -> base::Result<std::reference_wrapper<const ResultColumn>> {
            if (index >= columns_.size()) {
                return base::fail(result_error(
                    base::ErrorCode::kOutOfRange,
                    "result column index is out of range",
                    "access result column"
                ));
            }
            return std::cref(columns_[index]);
        }
    );
}

ResultRow::ResultRow(
    std::shared_ptr<const ResultSchema> schema,
    std::vector<types::Value> values
) noexcept
    : schema_{std::move(schema)},
      values_{std::move(values)} {}

auto ResultRow::create(ResultSchema schema, std::vector<types::Value> values)
    -> base::Result<ResultRow> {
    return result_boundary("create result row", [&]() -> base::Result<ResultRow> {
        auto valid = validate_row_values(schema, values);
        if (!valid) {
            return base::fail(std::move(valid).error());
        }
        auto shared_schema = std::make_shared<const ResultSchema>(std::move(schema));
        return ResultRow{std::move(shared_schema), std::move(values)};
    });
}

auto ResultRow::valid() const noexcept -> bool {
    return schema_ != nullptr && values_.size() == schema_->size();
}

auto ResultRow::empty_schema() noexcept -> const ResultSchema& {
    static const ResultSchema kEmpty{};
    return kEmpty;
}

auto ResultRow::schema() const noexcept -> const ResultSchema& {
    return schema_ == nullptr ? empty_schema() : *schema_;
}

auto ResultRow::values() const noexcept -> std::span<const types::Value> {
    return values_;
}

auto ResultRow::size() const noexcept -> std::size_t {
    return values_.size();
}

auto ResultRow::empty() const noexcept -> bool {
    return values_.empty();
}

auto ResultRow::at(const std::size_t index) const
    -> base::Result<std::reference_wrapper<const types::Value>> {
    return result_boundary(
        "access result row value",
        [&]() -> base::Result<std::reference_wrapper<const types::Value>> {
            if (index >= values_.size()) {
                return base::fail(result_error(
                    base::ErrorCode::kOutOfRange,
                    "result row index is out of range",
                    "access result row value"
                ));
            }
            return std::cref(values_[index]);
        }
    );
}

auto ResultRow::at(const std::string_view name) const
    -> base::Result<std::reference_wrapper<const types::Value>> {
    return result_boundary(
        "access result row value by name",
        [&]() -> base::Result<std::reference_wrapper<const types::Value>> {
            auto index = schema().position(name);
            if (!index) {
                return base::fail(std::move(index).error());
            }
            return at(index.value());
        }
    );
}

auto ResultRow::operator==(const ResultRow& other) const -> bool {
    return schema() == other.schema() && values_ == other.values_;
}

ResultSet::ResultSet(
    std::shared_ptr<const ResultSchema> schema,
    std::vector<ResultRow> rows
) noexcept
    : schema_{std::move(schema)},
      rows_{std::move(rows)} {}

auto ResultSet::create(ResultSchema schema, std::vector<std::vector<types::Value>> rows)
    -> base::Result<ResultSet> {
    return result_boundary("create result set", [&]() -> base::Result<ResultSet> {
        auto shared_schema = std::make_shared<const ResultSchema>(std::move(schema));
        std::vector<ResultRow> result_rows;
        result_rows.reserve(rows.size());
        for (auto& values : rows) {
            auto valid = validate_row_values(*shared_schema, values);
            if (!valid) {
                return base::fail(
                    std::move(valid).error().with_context("api.result", "create result set")
                );
            }
            result_rows.push_back(ResultRow{shared_schema, std::move(values)});
        }
        return ResultSet{std::move(shared_schema), std::move(result_rows)};
    });
}

auto ResultSet::valid() const noexcept -> bool {
    return schema_ != nullptr;
}

auto ResultSet::empty_schema() noexcept -> const ResultSchema& {
    static const ResultSchema kEmpty{};
    return kEmpty;
}

auto ResultSet::schema() const noexcept -> const ResultSchema& {
    return schema_ == nullptr ? empty_schema() : *schema_;
}

auto ResultSet::rows() const noexcept -> std::span<const ResultRow> {
    return rows_;
}

auto ResultSet::size() const noexcept -> std::size_t {
    return rows_.size();
}

auto ResultSet::empty() const noexcept -> bool {
    return rows_.empty();
}

auto ResultSet::at(const std::size_t index) const
    -> base::Result<std::reference_wrapper<const ResultRow>> {
    return result_boundary(
        "access result set row",
        [&]() -> base::Result<std::reference_wrapper<const ResultRow>> {
            if (index >= rows_.size()) {
                return base::fail(result_error(
                    base::ErrorCode::kOutOfRange,
                    "result set row index is out of range",
                    "access result set row"
                ));
            }
            return std::cref(rows_[index]);
        }
    );
}

auto ResultSet::into_data() && -> base::Result<ResultSetData> {
    return result_boundary("consume result set", [&]() -> base::Result<ResultSetData> {
        if (!valid()) {
            return base::fail(result_error(
                base::ErrorCode::kInvalidArgument,
                "result set has been consumed or moved from",
                "consume result set"
            ));
        }
        // Finish every allocation before transferring ownership. Immutable
        // metadata may also belong to independent ResultSet/ResultRow copies.
        ResultSetData data;
        data.rows.reserve(rows_.size());
        data.schema = std::move(schema_);
        for (auto& row : rows_) {
            data.rows.push_back(std::move(row.values_));
        }
        rows_.clear();
        return data;
    });
}

auto ResultSet::operator==(const ResultSet& other) const -> bool {
    return schema() == other.schema() && rows_ == other.rows_;
}

}  // namespace db::api
