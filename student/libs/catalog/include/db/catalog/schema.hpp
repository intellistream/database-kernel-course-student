#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/types/data_type.hpp"

namespace db::catalog {

inline constexpr std::size_t kMaximumCatalogNameBytes = std::numeric_limits<std::uint16_t>::max();

enum class ColumnConstraintKind : std::uint8_t {
    kNotNull,
    kUnique,
    kPrimaryKey,
    kCheck,
    kDefaultValue,
    kGenerated,
    kForeignKey,
};

enum class TableConstraintKind : std::uint8_t {
    kUnique,
    kPrimaryKey,
    kCheck,
    kForeignKey,
};

auto column_constraint_name(ColumnConstraintKind kind) noexcept -> std::string_view;
auto table_constraint_name(TableConstraintKind kind) noexcept -> std::string_view;

struct ColumnConstraint final {
    std::string column_name;
    ColumnConstraintKind kind;

    auto operator==(const ColumnConstraint&) const -> bool = default;
};

struct RowSizeLimit final {
    std::size_t maximum_encoded_size{0};

    auto operator==(const RowSizeLimit&) const -> bool = default;
};

struct Column final {
    std::string name;
    types::DataType data_type;

    auto operator==(const Column&) const -> bool = default;
};

// Schema copies share one immutable column array. This keeps catalog, binder,
// planner, and executor views cheap without exposing mutable metadata.
class Schema final {
public:
    Schema();
    static auto create(
        std::vector<Column> columns,
        RowSizeLimit row_size_limit,
        std::vector<ColumnConstraint> column_constraints = {},
        std::vector<TableConstraintKind> table_constraints = {}
    ) -> base::Result<Schema>;
    Schema(const Schema&) noexcept = default;
    auto operator=(const Schema&) noexcept -> Schema& = default;
    Schema(Schema&& other) noexcept;
    auto operator=(Schema&& other) noexcept -> Schema&;
    ~Schema() = default;

    auto columns() const noexcept -> std::span<const Column>;
    auto position(std::string_view name) const noexcept -> std::optional<std::size_t>;
    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;
    auto null_bitmap_size() const noexcept -> std::size_t;
    auto encoded_row_size() const noexcept -> std::size_t;
    auto table_constraints() const noexcept -> std::span<const TableConstraintKind>;

    auto operator==(const Schema& other) const noexcept -> bool;

private:
    Schema(
        std::vector<Column> columns,
        std::size_t null_bitmap_size,
        std::size_t encoded_row_size,
        std::vector<TableConstraintKind> table_constraints
    );

    std::shared_ptr<const std::vector<Column>> columns_;
    std::size_t null_bitmap_size_{0};
    std::size_t encoded_row_size_{0};
    std::vector<TableConstraintKind> table_constraints_;
};

}  // namespace db::catalog
