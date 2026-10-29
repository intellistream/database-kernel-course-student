#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {

struct PlanSchema;

}  // namespace db::planner

namespace db::execution {

class PlanState;
class ProjectionProgram;
class ScanPort;

struct TupleColumn final {
    std::string name;
    types::DataType data_type{types::kUnknownType};

    auto operator==(const TupleColumn&) const -> bool = default;
};

class TupleDescriptor;
using TupleDescriptorHandle = std::shared_ptr<const TupleDescriptor>;

// Immutable logical output shape shared by all tuples produced by one plan
// state. Names are descriptive rather than lookup keys: duplicate names are
// valid SQL projection output and consumers use positional column access.
class TupleDescriptor final {
private:
    struct ConstructionToken final {};

public:
    // The private token keeps direct construction outside this checked factory
    // while allowing std::make_shared to coallocate the descriptor and its
    // reference-count control block.
    static auto create(std::vector<TupleColumn> columns) -> base::Result<TupleDescriptorHandle>;
    static auto from_plan_schema(const planner::PlanSchema& schema)
        -> base::Result<TupleDescriptorHandle>;

    auto columns() const noexcept -> std::span<const TupleColumn>;
    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;

    TupleDescriptor(ConstructionToken construction_token, std::vector<TupleColumn> columns);

private:
    std::vector<TupleColumn> columns_;
};

// A fully materialized logical tuple. It deliberately has no physical heap
// bytes or page ownership; access owns those representations and scan ports
// translate them at the execution boundary.
class Tuple final {
public:
    static auto create(
        TupleDescriptorHandle descriptor,
        std::vector<types::Value> values,
        std::optional<base::Rid> rid = std::nullopt
    ) -> base::Result<Tuple>;

    auto descriptor() const noexcept -> const TupleDescriptorHandle&;
    auto values() const noexcept -> std::span<const types::Value>;
    auto value_at(std::size_t index) const
        -> base::Result<std::reference_wrapper<const types::Value>>;
    auto rid() const noexcept -> std::optional<base::Rid>;

    // Consumes the logical row storage while discarding descriptor and RID
    // metadata. Scan states use this to recycle a rejected candidate without
    // copying its values.
    auto into_values() && noexcept -> std::vector<types::Value>;

private:
    friend class PlanState;
    friend class ProjectionProgram;
    friend class ScanPort;
    friend class TupleSlot;

    Tuple(
        TupleDescriptorHandle descriptor,
        std::vector<types::Value> values,
        std::optional<base::Rid> rid
    ) noexcept;

    TupleDescriptorHandle descriptor_;
    std::vector<types::Value> values_;
    std::optional<base::Rid> rid_;
};

// A move-only materialization location owned by one PlanState. The descriptor
// and resident Tuple object live for the complete slot lifetime; clear/store
// only change the resident row values, RID, and occupied flag. A pointer
// returned from tuple() is a logical row borrow that must not be used after a
// successful store(), clear(), the next PlanState lifecycle transition, moving
// this slot, or destruction. Move transfers the materialization while the
// source retains its immutable descriptor and becomes an empty reusable slot.
class TupleSlot final {
public:
    static auto create(TupleDescriptorHandle descriptor) -> base::Result<TupleSlot>;

    TupleSlot(const TupleSlot&) = delete;
    auto operator=(const TupleSlot&) -> TupleSlot& = delete;
    TupleSlot(TupleSlot&& other) noexcept;
    auto operator=(TupleSlot&& other) noexcept -> TupleSlot&;
    ~TupleSlot() = default;

    auto descriptor() const noexcept -> const TupleDescriptorHandle&;
    auto empty() const noexcept -> bool;
    auto tuple() const noexcept -> const Tuple*;

    auto store(Tuple tuple) -> base::Status;
    auto store_values(std::vector<types::Value> values, std::optional<base::Rid> rid = std::nullopt)
        -> base::Status;
    void clear() noexcept;

private:
    friend class PlanState;

    explicit TupleSlot(TupleDescriptorHandle descriptor);

    TupleDescriptorHandle descriptor_;
    Tuple tuple_;
    bool occupied_{false};
};

}  // namespace db::execution
