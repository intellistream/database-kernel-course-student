#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/types/coercion.hpp"
#include "db/types/data_type.hpp"
#include "db/types/operator.hpp"
#include "db/types/value.hpp"

namespace db::planner {

// Physical plans retain byte offsets for diagnostics without importing the
// SQL source-AST type that originally supplied them.
struct PlanSourceSpan final {
    std::size_t begin_byte{0U};
    std::size_t end_byte{0U};

    constexpr auto operator==(const PlanSourceSpan&) const -> bool = default;
};

// Physical input and output positions are deliberately distinct from SQL
// binding range-table positions. A planner rewrites a resolved bound column
// into the zero-based position of the operator input that will provide it.
class PlanColumnIndex final {
public:
    constexpr explicit PlanColumnIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const PlanColumnIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{0U};
};

// Parameter IDs are zero-based physical slots.
class PlanParameterId final {
public:
    constexpr explicit PlanParameterId(
        const std::uint32_t zero_based = std::numeric_limits<std::uint32_t>::max()
    ) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto valid() const noexcept -> bool {
        return zero_based_ != std::numeric_limits<std::uint32_t>::max();
    }

    constexpr auto operator==(const PlanParameterId&) const -> bool = default;

private:
    std::uint32_t zero_based_{std::numeric_limits<std::uint32_t>::max()};
};

class PlanAggregateIndex final {
public:
    constexpr explicit PlanAggregateIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const PlanAggregateIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{0U};
};

// An index-key position is not a relation-column position. Keeping the two
// strong types distinct prevents an optimizer from accidentally treating the
// second relation column as the second key of a reordered composite index.
class PlanIndexKeyIndex final {
public:
    constexpr explicit PlanIndexKeyIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const PlanIndexKeyIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{0U};
};

class PlanSubPlanIndex final {
public:
    constexpr explicit PlanSubPlanIndex(
        const std::uint32_t zero_based = std::numeric_limits<std::uint32_t>::max()
    ) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto valid() const noexcept -> bool {
        return zero_based_ != std::numeric_limits<std::uint32_t>::max();
    }

    constexpr auto operator==(const PlanSubPlanIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{std::numeric_limits<std::uint32_t>::max()};
};

enum class PlanParameterKind : std::uint8_t {
    kExternal,
    kExecutor,
};

// External parameter numbers preserve the one-based `$n` contract at the
// planner boundary without exposing the SQL parser's ParameterSlot type.
class ExternalParameterNumber final {
public:
    constexpr explicit ExternalParameterNumber(const std::uint32_t one_based = 0U) noexcept
        : one_based_{one_based} {}

    constexpr auto one_based() const noexcept -> std::uint32_t {
        return one_based_;
    }

    constexpr auto valid() const noexcept -> bool {
        return one_based_ != 0U;
    }

    constexpr auto operator==(const ExternalParameterNumber&) const -> bool = default;

private:
    std::uint32_t one_based_{0U};
};

inline constexpr ExternalParameterNumber kInvalidExternalParameterNumber{};

struct ExternalParameterSlot final {
    ExternalParameterNumber slot{kInvalidExternalParameterNumber};
    types::DataType data_type{types::kUnknownType};

    auto operator==(const ExternalParameterSlot&) const -> bool = default;
};

struct PlanParameter final {
    PlanParameterId id{};
    PlanParameterKind kind{PlanParameterKind::kExternal};
    types::DataType data_type{types::kUnknownType};
    std::optional<ExternalParameterNumber> external_slot{};

    auto operator==(const PlanParameter&) const -> bool = default;
};

struct PlanParameterReference final {
    PlanParameterId id{};
    PlanParameterKind kind{PlanParameterKind::kExternal};
    types::DataType data_type{types::kUnknownType};

    auto operator==(const PlanParameterReference&) const -> bool = default;
};

// PlannerContext-like state shared by every query level in one statement.
// External slots occupy the initial stable range; executor slots allocated for
// correlated subplans and parameterized joins are appended and can never
// collide with an external `$n` parameter.
class ParameterNamespace final {
public:
    static auto from_external_slots(std::span<const ExternalParameterSlot> external_slots)
        -> base::Result<ParameterNamespace>;

    ParameterNamespace(const ParameterNamespace&) = delete;
    auto operator=(const ParameterNamespace&) -> ParameterNamespace& = delete;
    ParameterNamespace(ParameterNamespace&& other) noexcept;
    auto operator=(ParameterNamespace&& other) noexcept -> ParameterNamespace&;
    ~ParameterNamespace() = default;

    // False only for the inert source left by a move. Its observers return an
    // empty namespace and lookup/allocation operations return kUnavailable.
    auto valid() const noexcept -> bool;

    auto external_reference(ExternalParameterNumber slot) const
        -> base::Result<PlanParameterReference>;
    auto allocate_executor_parameter(types::DataType data_type)
        -> base::Result<PlanParameterReference>;
    auto reference(PlanParameterId id) const -> base::Result<PlanParameterReference>;

    auto parameters() const noexcept -> std::span<const PlanParameter>;
    auto external_parameter_count() const noexcept -> std::uint32_t;

private:
    explicit ParameterNamespace(std::vector<PlanParameter> parameters, std::uint32_t external_count)
        : parameters_{std::move(parameters)},
          external_count_{external_count} {}

    std::vector<PlanParameter> parameters_;
    std::uint32_t external_count_{0U};
    bool active_{true};
};

struct PlanColumnOrigin final {
    base::Oid relation_oid{base::kInvalidOid};
    PlanColumnIndex relation_column{};

    auto operator==(const PlanColumnOrigin&) const -> bool = default;
};

// Duplicate output names are allowed because SQL projections retain their
// positional behavior. `origin` is optional for computed expressions and
// derived outputs, rather than inventing a base relation identity.
struct PlanOutputColumn final {
    std::string name;
    types::DataType data_type{types::kUnknownType};
    std::optional<PlanColumnOrigin> origin{};

    auto operator==(const PlanOutputColumn&) const -> bool = default;
};

struct PlanSchema final {
    std::vector<PlanOutputColumn> columns;

    auto operator==(const PlanSchema&) const -> bool = default;
};

// Every physical operator carries its own resolved output schema. Runtime
// cardinality and work are execution facts, not planner metadata: deterministic
// planning deliberately has no cost, row-count, or width estimate surface.
struct PlanOperatorMetadata final {
    PlanSchema output;

    auto operator==(const PlanOperatorMetadata&) const -> bool = default;
};

struct PlanExpression;
using PlanExpressionPtr = std::unique_ptr<PlanExpression>;

struct PlanLiteralExpression final {
    types::Value value{types::Value::null(types::kUnknownType)};
};

struct PlanInputColumnReference final {
    PlanColumnIndex column{};
};

struct PlanUnaryExpression final {
    types::UnaryOperator op{types::UnaryOperator::kPlus};
    PlanExpressionPtr operand;
};

struct PlanBinaryExpression final {
    types::BinaryOperator op{types::BinaryOperator::kEqual};
    PlanExpressionPtr left;
    PlanExpressionPtr right;
};

struct PlanBooleanExpression final {
    types::BooleanOperator op{types::BooleanOperator::kAnd};
    std::vector<PlanExpressionPtr> operands;
};

struct PlanIsNullExpression final {
    bool negated{false};
    PlanExpressionPtr operand;
};

struct PlanInListExpression final {
    bool negated{false};
    PlanExpressionPtr operand;
    std::vector<PlanExpressionPtr> values;
};

struct PlanBetweenExpression final {
    bool negated{false};
    PlanExpressionPtr operand;
    PlanExpressionPtr lower;
    PlanExpressionPtr upper;
};

enum class PlanCastForm : std::uint8_t {
    kImplicit,
    kExplicit,
};

struct PlanCastExpression final {
    PlanExpressionPtr operand;
    types::DataType target_type{types::kUnknownType};
    types::CoercionPath path{};
    PlanCastForm form{PlanCastForm::kImplicit};
};

enum class PlanAggregateKind : std::uint8_t {
    kCount,
    kSum,
    kAverage,
    kMinimum,
    kMaximum,
};

struct PlanAggregateExpression final {
    PlanAggregateKind kind{PlanAggregateKind::kCount};
    bool distinct{false};
    bool star{false};
    std::vector<PlanExpressionPtr> arguments;
    PlanAggregateIndex aggregate_index{};
};

// A subplan expression is normalized into this physical reference during lowering.
// Its ID is meaningful only in the nearest enclosing SubPlanEvalPlan.
struct PlanSubPlanReference final {
    PlanSubPlanIndex index{};
};

// Planner-owned expression tree. It contains physical input positions and
// parameter IDs instead of bound AST scope/range-table pointers, so execution
// can consume a plan without retaining SQL binder state.
struct PlanExpression final {
    using Node = std::variant<
        PlanLiteralExpression,
        PlanInputColumnReference,
        PlanParameterReference,
        PlanUnaryExpression,
        PlanBinaryExpression,
        PlanBooleanExpression,
        PlanIsNullExpression,
        PlanInListExpression,
        PlanBetweenExpression,
        PlanCastExpression,
        PlanAggregateExpression,
        PlanSubPlanReference>;

    PlanExpression() = delete;

    PlanExpression(PlanSourceSpan source_span, types::DataType result_type, Node plan_node)
        : span{source_span},
          data_type{result_type},
          node{std::move(plan_node)} {}

    ~PlanExpression();
    PlanExpression(const PlanExpression&) = delete;
    auto operator=(const PlanExpression&) -> PlanExpression& = delete;
    PlanExpression(PlanExpression&&) noexcept = default;
    auto operator=(PlanExpression&&) noexcept -> PlanExpression& = default;

    PlanSourceSpan span{};
    types::DataType data_type{types::kUnknownType};
    Node node;
};

struct Plan;

struct PlanDeleter final {
    constexpr PlanDeleter() noexcept = default;

    constexpr PlanDeleter(
        [[maybe_unused]] const std::default_delete<Plan>& source_deleter
    ) noexcept {}

    void operator()(Plan* plan) const noexcept;
};

using PlanPtr = std::unique_ptr<Plan, PlanDeleter>;

struct PlanRelation final {
    base::Oid oid{base::kInvalidOid};
    std::string name;
    std::optional<std::string> alias{};
    PlanSchema schema;
};

struct PlanIndexKey final {
    PlanColumnIndex relation_column{};
    std::string column_name;
    types::DataType data_type{types::kUnknownType};
};

struct PlanIndex final {
    base::Oid oid{base::kInvalidOid};
    std::string name;
    std::vector<PlanIndexKey> keys;
    bool unique{false};
};

// Path selection chooses and validates these typed bounds. The IR retains the
// metadata so an IndexScan never falls back to stringly SQL predicates.
struct PlanIndexBound final {
    PlanIndexKeyIndex key_index{};
    types::BinaryOperator comparison{types::BinaryOperator::kEqual};
    PlanExpressionPtr value;
};

struct ValuesPlan final {
    PlanOperatorMetadata metadata;
    std::vector<std::vector<PlanExpressionPtr>> rows;
};

struct SeqScanPlan final {
    PlanOperatorMetadata metadata;
    PlanRelation relation;
    // `nullopt` preserves full-row materialization. An engaged, possibly
    // empty list is the exact set of relation columns needed by this plan.
    // The logical output schema remains `relation.schema` in both cases.
    std::optional<std::vector<PlanColumnIndex>> required_columns{};
    PlanExpressionPtr filter;
};

struct IndexScanPlan final {
    PlanOperatorMetadata metadata;
    PlanRelation relation;
    PlanIndex index;
    std::vector<PlanIndexBound> bounds;
    std::optional<std::vector<PlanColumnIndex>> required_columns{};
    PlanExpressionPtr filter;
};

struct PlanProjectionItem final {
    PlanExpressionPtr expression;
    std::string output_name;
};

struct ResultPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr input;
    std::vector<PlanProjectionItem> projection;
};

enum class ModifyOperation : std::uint8_t {
    kInsert,
    kUpdate,
    kDelete,
};

struct PlanAssignment final {
    PlanColumnIndex target_column{};
    PlanExpressionPtr expression;
};

struct PlanValuesModificationSource final {
    std::vector<std::vector<PlanExpressionPtr>> rows;
};

struct PlanQueryModificationSource final {
    PlanPtr input;
};

// A monostate source is valid only for DELETE. INSERT/UPDATE sources are made
// concrete by statement lowering; representing that distinction here keeps their
// physical target metadata independent of SQL bound nodes.
using PlanModificationSource =
    std::variant<std::monostate, PlanValuesModificationSource, PlanQueryModificationSource>;

struct ModifyTablePlan final {
    PlanOperatorMetadata metadata;
    ModifyOperation operation{ModifyOperation::kInsert};
    PlanRelation target;
    std::vector<PlanColumnIndex> target_columns;
    std::vector<PlanAssignment> assignments;
    PlanModificationSource source{};
};

enum class PlanSortDirection : std::uint8_t {
    kDefault,
    kAscending,
    kDescending,
};

enum class PlanNullOrder : std::uint8_t {
    kDefault,
    kFirst,
    kLast,
};

struct PlanSortItem final {
    PlanExpressionPtr expression;
    PlanSortDirection direction{PlanSortDirection::kDefault};
    PlanNullOrder null_order{PlanNullOrder::kDefault};
};

struct SortPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr input;
    std::vector<PlanSortItem> order_by;
};

struct LimitPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr input;
    PlanExpressionPtr limit;
    PlanExpressionPtr offset;
};

struct TopNPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr input;
    std::vector<PlanSortItem> order_by;
    PlanExpressionPtr limit;
    PlanExpressionPtr offset;
};

struct AppendPlan final {
    PlanOperatorMetadata metadata;
    std::vector<PlanPtr> inputs;
};

struct UniquePlan final {
    PlanOperatorMetadata metadata;
    PlanPtr input;
};

struct PlanAggregate final {
    PlanAggregateKind kind{PlanAggregateKind::kCount};
    std::vector<PlanExpressionPtr> arguments;
    bool distinct{false};
    bool star{false};
    std::string output_name;
    // Aggregate output types cannot in general be inferred from the input
    // (for example COUNT and AVG), so keep the resolved physical type here.
    types::DataType data_type{types::kUnknownType};
    PlanAggregateIndex aggregate_index{};
};

struct AggregatePlan final {
    PlanOperatorMetadata metadata;
    PlanPtr input;
    std::vector<PlanExpressionPtr> group_by;
    std::vector<PlanAggregate> aggregates;
    PlanExpressionPtr having;
    bool ordered_min{false};
};

struct NestedLoopParameter final {
    PlanParameterReference parameter;
    PlanColumnIndex outer_column{};
};

struct NestedLoopPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr outer;
    PlanPtr inner;
    PlanExpressionPtr join_filter;
    std::vector<NestedLoopParameter> parameters;
};

struct PlanJoinColumnPair final {
    PlanColumnIndex outer_column{};
    PlanColumnIndex inner_column{};
};

struct MergeJoinPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr outer;
    PlanPtr inner;
    std::vector<PlanJoinColumnPair> join_columns;
    PlanExpressionPtr residual_filter;
};

struct HashJoinPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr outer;
    PlanPtr inner;
    std::vector<PlanJoinColumnPair> join_columns;
    PlanExpressionPtr residual_filter;
};

struct SubqueryScanPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr subquery;
    std::string alias;
    PlanExpressionPtr filter;
};

enum class PlanSubPlanStrategy : std::uint8_t {
    kIn,
    kNotIn,
    kExists,
    kNotExists,
    kScalar,
};

struct PlanSubPlan final {
    PlanPtr plan;
    PlanSubPlanStrategy strategy{PlanSubPlanStrategy::kScalar};
    PlanExpressionPtr input_expression;
    std::vector<PlanExpressionPtr> arguments;
    std::vector<PlanParameterReference> parameters;
    // The child consumes executor slots that an ancestor already populated.
    // It may coexist with this subplan's own argument-to-slot mappings: the
    // two sets describe different ownership levels in one statement-wide
    // namespace.
    bool uses_inherited_parameters{false};
};

struct SubPlanEvalPlan final {
    PlanOperatorMetadata metadata;
    PlanPtr input;
    std::vector<PlanSubPlan> subplans;
    PlanExpressionPtr filter;
};

enum class PlanOperatorKind : std::uint8_t {
    kValues,
    kSeqScan,
    kIndexScan,
    kResult,
    kModifyTable,
    kSort,
    kLimit,
    kTopN,
    kUnique,
    kAppend,
    kAggregate,
    kNestedLoop,
    kMergeJoin,
    kHashJoin,
    kSubqueryScan,
    kSubPlanEval,
};

// Move-only physical operator tree. All children and expressions are owned by
// this tree; no `CatalogReadPort`, parser arena, binder scope, or executor
// state is retained. Execution can therefore create a new PlanState tree for each
// execution without sharing mutable operator state.
struct Plan final {
    using Node = std::variant<
        ValuesPlan,
        SeqScanPlan,
        IndexScanPlan,
        ResultPlan,
        ModifyTablePlan,
        SortPlan,
        LimitPlan,
        TopNPlan,
        UniquePlan,
        AppendPlan,
        AggregatePlan,
        NestedLoopPlan,
        MergeJoinPlan,
        HashJoinPlan,
        SubqueryScanPlan,
        SubPlanEvalPlan>;

    explicit Plan(Node plan_node)
        : node{std::move(plan_node)} {}

    ~Plan();
    Plan(const Plan&) = delete;
    auto operator=(const Plan&) -> Plan& = delete;
    Plan(Plan&&) noexcept = default;
    auto operator=(Plan&&) noexcept -> Plan& = default;

    Node node;
};

// A completed plan owns the statement-wide physical parameter namespace and
// records the immutable catalog generation used by the planner. It is the
// reusable prepared-plan unit: execution creates fresh state from `root`,
// while engine dispatch verifies the captured catalog generation before reuse. Individual
// Plan nodes intentionally do not own this global state, so nested subplans
// share one namespace without copies or ID collisions. Construction is private
// so this completed boundary cannot expose a malformed or subsequently mutable
// physical tree.
class PlannedStatement final {
public:
    static auto
    create(std::uint64_t catalog_generation, Plan planned_root, ParameterNamespace parameters)
        -> base::Result<PlannedStatement>;

    PlannedStatement(const PlannedStatement&) = delete;
    auto operator=(const PlannedStatement&) -> PlannedStatement& = delete;
    PlannedStatement(PlannedStatement&&) noexcept = default;
    auto operator=(PlannedStatement&&) noexcept -> PlannedStatement& = default;
    ~PlannedStatement() = default;

    auto catalog_generation() const noexcept -> std::uint64_t {
        return catalog_generation_;
    }

    auto root() const noexcept -> const Plan& {
        return root_;
    }

    auto parameter_namespace() const noexcept -> const ParameterNamespace& {
        return parameter_namespace_;
    }

private:
    PlannedStatement(
        const std::uint64_t catalog_generation,
        Plan planned_root,
        ParameterNamespace parameters
    )
        : catalog_generation_{catalog_generation},
          root_{std::move(planned_root)},
          parameter_namespace_{std::move(parameters)} {}

    std::uint64_t catalog_generation_{0U};
    Plan root_;
    ParameterNamespace parameter_namespace_;
};

auto plan_operator_kind(const Plan& plan) -> PlanOperatorKind;
auto plan_metadata(const Plan& plan) -> const PlanOperatorMetadata&;

// Structural validation is deliberately separate from plan construction so
// optimizer stages can build move-only nodes incrementally, then validate at
// their public boundary. Supplying a namespace additionally verifies every
// physical parameter reference against the statement-wide allocator.
auto validate_plan(const Plan& plan) -> base::Status;
auto validate_plan(const Plan& plan, const ParameterNamespace& parameters) -> base::Status;
auto validate_planned_statement(const PlannedStatement& statement) -> base::Status;

inline PlanExpression::~PlanExpression() = default;
inline Plan::~Plan() = default;

inline void PlanDeleter::operator()(Plan* plan) const noexcept {
    // std::unique_ptr transfers its owned object to this deleter protocol.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete plan;
}

}  // namespace db::planner
