#include "db/execution/tuple.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/planner/plan.hpp"
#include "db/types/type_registry.hpp"

namespace db::execution {
namespace {

auto tuple_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.tuple", "validate");
}

auto validate_columns(const std::span<const TupleColumn> columns) -> base::Status {
    for (const TupleColumn& column : columns) {
        auto valid_type = types::validate_data_type(column.data_type);
        if (!valid_type) {
            return base::fail(
                std::move(valid_type).error().with_context("execution.tuple", "create descriptor")
            );
        }
    }
    return {};
}

}  // namespace

TupleDescriptor::TupleDescriptor(
    const ConstructionToken construction_token,
    std::vector<TupleColumn> columns
)
    : columns_{std::move(columns)} {
    static_cast<void>(construction_token);
}

auto TupleDescriptor::create(std::vector<TupleColumn> columns)
    -> base::Result<TupleDescriptorHandle> {
    auto valid_columns = validate_columns(columns);
    if (!valid_columns) {
        return base::fail(std::move(valid_columns).error());
    }
    return std::make_shared<const TupleDescriptor>(ConstructionToken{}, std::move(columns));
}

auto TupleDescriptor::from_plan_schema(const planner::PlanSchema& schema)
    -> base::Result<TupleDescriptorHandle> {
    std::vector<TupleColumn> columns;
    columns.reserve(schema.columns.size());
    for (const planner::PlanOutputColumn& column : schema.columns) {
        if (column.name.empty()) {
            return base::fail(
                tuple_error(base::ErrorCode::kPlan, "plan output column has an empty name")
            );
        }
        columns.push_back(
            TupleColumn{
                .name = column.name,
                .data_type = column.data_type,
            }
        );
    }
    return create(std::move(columns));
}

auto TupleDescriptor::columns() const noexcept -> std::span<const TupleColumn> {
    return columns_;
}

auto TupleDescriptor::size() const noexcept -> std::size_t {
    return columns_.size();
}

auto TupleDescriptor::empty() const noexcept -> bool {
    return columns_.empty();
}

Tuple::Tuple(
    TupleDescriptorHandle descriptor,
    std::vector<types::Value> values,
    const std::optional<base::Rid> rid
) noexcept
    : descriptor_{std::move(descriptor)},
      values_{std::move(values)},
      rid_{rid} {}

auto Tuple::create(
    TupleDescriptorHandle descriptor,
    std::vector<types::Value> values,
    const std::optional<base::Rid> rid
) -> base::Result<Tuple> {
    if (descriptor == nullptr) {
        return base::fail(
            tuple_error(base::ErrorCode::kInvalidArgument, "tuple descriptor must not be null")
        );
    }
    if (values.size() != descriptor->size()) {
        return base::fail(tuple_error(
            base::ErrorCode::kInvalidArgument,
            "tuple value count does not match its descriptor"
        ));
    }
    const auto columns = descriptor->columns();
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (values[index].data_type() != columns[index].data_type) {
            return base::fail(tuple_error(
                base::ErrorCode::kInvalidArgument,
                "tuple value type does not match its descriptor column"
            ));
        }
    }
    return Tuple{std::move(descriptor), std::move(values), rid};
}

auto Tuple::descriptor() const noexcept -> const TupleDescriptorHandle& {
    return descriptor_;
}

auto Tuple::values() const noexcept -> std::span<const types::Value> {
    return values_;
}

auto Tuple::value_at(const std::size_t index) const
    -> base::Result<std::reference_wrapper<const types::Value>> {
    if (index >= values_.size()) {
        return base::fail(tuple_error(
            base::ErrorCode::kOutOfRange,
            "tuple column index is outside its descriptor"
        ));
    }
    return std::cref(values_[index]);
}

auto Tuple::rid() const noexcept -> std::optional<base::Rid> {
    return rid_;
}

auto Tuple::into_values() && noexcept -> std::vector<types::Value> {
    return std::move(values_);
}

TupleSlot::TupleSlot(TupleDescriptorHandle descriptor)
    : descriptor_{std::move(descriptor)},
      tuple_{descriptor_, {}, std::nullopt} {}

TupleSlot::TupleSlot(TupleSlot&& other) noexcept
    // Copying the immutable handle keeps the moved-from slot reusable.
    : descriptor_{other.descriptor_},  // NOLINT(performance-move-constructor-init)
      tuple_{descriptor_, std::move(other.tuple_.values_), other.tuple_.rid_},
      occupied_{other.occupied_} {
    other.tuple_.values_.clear();
    other.tuple_.rid_.reset();
    other.occupied_ = false;
}

auto TupleSlot::operator=(TupleSlot&& other) noexcept -> TupleSlot& {
    if (this != &other) {
        descriptor_ = other.descriptor_;
        tuple_.descriptor_ = descriptor_;
        tuple_.values_ = std::move(other.tuple_.values_);
        tuple_.rid_ = other.tuple_.rid_;
        occupied_ = other.occupied_;
        other.tuple_.values_.clear();
        other.tuple_.rid_.reset();
        other.occupied_ = false;
    }
    return *this;
}

auto TupleSlot::create(TupleDescriptorHandle descriptor) -> base::Result<TupleSlot> {
    if (descriptor == nullptr) {
        return base::fail(
            tuple_error(base::ErrorCode::kInvalidArgument, "tuple slot descriptor must not be null")
        );
    }
    return TupleSlot{std::move(descriptor)};
}

auto TupleSlot::descriptor() const noexcept -> const TupleDescriptorHandle& {
    return descriptor_;
}

auto TupleSlot::empty() const noexcept -> bool {
    return !occupied_;
}

auto TupleSlot::tuple() const noexcept -> const Tuple* {
    return occupied_ ? std::addressof(tuple_) : nullptr;
}

auto TupleSlot::store(Tuple tuple) -> base::Status {
    if (tuple.descriptor() != descriptor_) {
        return base::fail(tuple_error(
            base::ErrorCode::kInvalidArgument,
            "tuple descriptor does not match tuple slot descriptor"
        ));
    }
    tuple_.values_ = std::move(tuple.values_);
    tuple_.rid_ = tuple.rid_;
    occupied_ = true;
    return {};
}

auto TupleSlot::store_values(std::vector<types::Value> values, const std::optional<base::Rid> rid)
    -> base::Status {
    auto tuple = Tuple::create(descriptor_, std::move(values), rid);
    if (!tuple) {
        return base::fail(std::move(tuple).error().with_context("execution.tuple", "store slot"));
    }
    return store(std::move(tuple).value());
}

void TupleSlot::clear() noexcept {
    tuple_.values_.clear();
    tuple_.rid_.reset();
    occupied_ = false;
}

}  // namespace db::execution
