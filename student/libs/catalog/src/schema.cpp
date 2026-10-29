#include "db/catalog/schema.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/type_registry.hpp"

#include "metadata_validation.hpp"

namespace db::catalog {
namespace {

[[maybe_unused]] constexpr auto bitmap_size(const std::size_t count) noexcept -> std::size_t {
    constexpr std::size_t k_bits_per_byte = 8;
    return count / k_bits_per_byte + (count % k_bits_per_byte == 0U ? 0U : 1U);
}

[[maybe_unused]] auto valid_constraint(const ColumnConstraintKind kind) noexcept -> bool {
    switch (kind) {
        case ColumnConstraintKind::kNotNull:
        case ColumnConstraintKind::kUnique:
        case ColumnConstraintKind::kPrimaryKey:
        case ColumnConstraintKind::kCheck:
        case ColumnConstraintKind::kDefaultValue:
        case ColumnConstraintKind::kGenerated:
        case ColumnConstraintKind::kForeignKey:
            return true;
    }
    return false;
}

[[maybe_unused]] auto valid_constraint(const TableConstraintKind kind) noexcept -> bool {
    switch (kind) {
        case TableConstraintKind::kUnique:
        case TableConstraintKind::kPrimaryKey:
        case TableConstraintKind::kCheck:
        case TableConstraintKind::kForeignKey:
            return true;
    }
    return false;
}

}  // namespace

[[maybe_unused]] auto
column_constraint_name([[maybe_unused]] const ColumnConstraintKind kind) noexcept
    -> std::string_view {
    // LAB-S02.Q10:R19-BEGIN
    base::lab::missing_algorithm("S02.Q10:R19");
    // LAB-S02.Q10:R19-END
}

[[maybe_unused]] auto
table_constraint_name([[maybe_unused]] const TableConstraintKind kind) noexcept
    -> std::string_view {
    // LAB-S02.Q10:R20-BEGIN
    base::lab::missing_algorithm("S02.Q10:R20");
    // LAB-S02.Q10:R20-END
}

[[maybe_unused]] Schema::Schema()
    : columns_{std::make_shared<const std::vector<Column>>()} {
    // LAB-S02.Q10:R21-BEGIN
    base::lab::missing_algorithm("S02.Q10:R21");
    // LAB-S02.Q10:R21-END
}

[[maybe_unused]] Schema::Schema(
    [[maybe_unused]] std::vector<Column> columns,
    [[maybe_unused]] const std::size_t null_bitmap_size,
    [[maybe_unused]] const std::size_t encoded_row_size,
    [[maybe_unused]] std::vector<TableConstraintKind> table_constraints
)
    : columns_{std::make_shared<const std::vector<Column>>(std::move(columns))},
      null_bitmap_size_{null_bitmap_size},
      encoded_row_size_{encoded_row_size},
      table_constraints_{std::move(table_constraints)} {
    // LAB-S02.Q10:R22-BEGIN
    base::lab::missing_algorithm("S02.Q10:R22");
    // LAB-S02.Q10:R22-END
}

[[maybe_unused]] auto Schema::create(
    [[maybe_unused]] std::vector<Column> columns,
    [[maybe_unused]] const RowSizeLimit row_size_limit,
    [[maybe_unused]] std::vector<ColumnConstraint> column_constraints,
    [[maybe_unused]] std::vector<TableConstraintKind> table_constraints
) -> base::Result<Schema> {
    // LAB-S02.Q10:R23-BEGIN
    base::lab::missing_algorithm("S02.Q10:R23");
    // LAB-S02.Q10:R23-END
}

[[maybe_unused]] Schema::Schema([[maybe_unused]] Schema&& other) noexcept
    : columns_{std::move(other.columns_)},
      null_bitmap_size_{other.null_bitmap_size_},
      encoded_row_size_{other.encoded_row_size_},
      table_constraints_{std::move(other.table_constraints_)} {
    // LAB-S02.Q10:R24-BEGIN
    base::lab::missing_algorithm("S02.Q10:R24");
    // LAB-S02.Q10:R24-END
}

auto Schema::operator=(Schema&& other) noexcept -> Schema& {
    if (this != &other) {
        columns_ = std::move(other.columns_);
        null_bitmap_size_ = other.null_bitmap_size_;
        encoded_row_size_ = other.encoded_row_size_;
        table_constraints_ = std::move(other.table_constraints_);
        other.columns_ = columns_;
    }
    return *this;
}

auto Schema::columns() const noexcept -> std::span<const Column> {
    return *columns_;
}

[[maybe_unused]] auto Schema::position([[maybe_unused]] const std::string_view name) const noexcept
    -> std::optional<std::size_t> {
    // LAB-S02.Q10:R25-BEGIN
    base::lab::missing_algorithm("S02.Q10:R25");
    // LAB-S02.Q10:R25-END
}

auto Schema::size() const noexcept -> std::size_t {
    return columns_->size();
}

auto Schema::empty() const noexcept -> bool {
    return columns_->empty();
}

auto Schema::null_bitmap_size() const noexcept -> std::size_t {
    return null_bitmap_size_;
}

[[maybe_unused]] auto Schema::encoded_row_size() const noexcept -> std::size_t {
    // LAB-S02.Q10:R26-BEGIN
    base::lab::missing_algorithm("S02.Q10:R26");
    // LAB-S02.Q10:R26-END
}

[[maybe_unused]] auto Schema::table_constraints() const noexcept
    -> std::span<const TableConstraintKind> {
    // LAB-S02.Q10:R27-BEGIN
    base::lab::missing_algorithm("S02.Q10:R27");
    // LAB-S02.Q10:R27-END
}

auto Schema::operator==(const Schema& other) const noexcept -> bool {
    return *columns_ == *other.columns_ && null_bitmap_size_ == other.null_bitmap_size_
           && encoded_row_size_ == other.encoded_row_size_
           && table_constraints_ == other.table_constraints_;
}

}  // namespace db::catalog
