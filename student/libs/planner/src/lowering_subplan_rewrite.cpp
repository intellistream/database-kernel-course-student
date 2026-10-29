#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/type_registry.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {
namespace {

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

struct CorrelationBinding final {
    QueryRangeTableIndex range_table_index{};
    QueryColumnIndex column_index{};
    types::DataType data_type{types::kUnknownType};
    PlanExpressionPtr argument;
    PlanParameterReference parameter;
};

struct CorrelationRewriteState final {
    const InputLayout& parent_input;
    LoweringContext& context;
    std::vector<CorrelationBinding> bindings;
    bool uses_inherited_parameters{false};
};

[[maybe_unused]] auto make_input_argument(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::DataType data_type,
    [[maybe_unused]] const PlanColumnIndex column
) -> PlanExpressionPtr {
    // LAB-S07.Q08:R09-BEGIN
    base::lab::missing_algorithm("S07.Q08:R09");
    // LAB-S07.Q08:R09-END
}

auto rewrite_expression_outer_refs(
    PreprocessedExpression& expression,
    std::uint32_t query_depth,
    CorrelationRewriteState& state
) -> base::Status;

auto rewrite_query_outer_refs(
    PreprocessedQuery& query,
    std::uint32_t query_depth,
    CorrelationRewriteState& state
) -> base::Status;

auto rewrite_table_outer_refs(
    PreprocessedTableReference& table,
    std::uint32_t query_depth,
    CorrelationRewriteState& state
) -> base::Status;

[[maybe_unused]] auto rewrite_expression_pointer_outer_refs(
    [[maybe_unused]] PreprocessedExpressionPtr& expression,
    [[maybe_unused]] const std::string& subject,
    [[maybe_unused]] const std::uint32_t query_depth,
    [[maybe_unused]] CorrelationRewriteState& state
) -> base::Status {
    // LAB-S07.Q08:R10-BEGIN
    base::lab::missing_algorithm("S07.Q08:R10");
    // LAB-S07.Q08:R10-END
}

[[maybe_unused]] auto rewrite_optional_expression_outer_refs(
    [[maybe_unused]] PreprocessedExpressionPtr& expression,
    [[maybe_unused]] const std::uint32_t query_depth,
    [[maybe_unused]] CorrelationRewriteState& state
) -> base::Status {
    // LAB-S07.Q08:R11-BEGIN
    base::lab::missing_algorithm("S07.Q08:R11");
    // LAB-S07.Q08:R11-END
}

[[maybe_unused]] auto rewrite_expression_list_outer_refs(
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr>& expressions,
    [[maybe_unused]] const std::string& subject,
    [[maybe_unused]] const std::uint32_t query_depth,
    [[maybe_unused]] CorrelationRewriteState& state
) -> base::Status {
    // LAB-S07.Q08:R12-BEGIN
    base::lab::missing_algorithm("S07.Q08:R12");
    // LAB-S07.Q08:R12-END
}

[[maybe_unused]] auto next_query_depth([[maybe_unused]] const std::uint32_t depth)
    -> base::Result<std::uint32_t> {
    // LAB-S07.Q08:R13-BEGIN
    base::lab::missing_algorithm("S07.Q08:R13");
    // LAB-S07.Q08:R13-END
}

[[maybe_unused]] auto rewrite_current_parent_reference(
    [[maybe_unused]] PreprocessedExpression& expression,
    [[maybe_unused]] const QueryColumnReference& reference,
    [[maybe_unused]] CorrelationRewriteState& state
) -> base::Status {
    // LAB-S07.Q08:R14-BEGIN
    base::lab::missing_algorithm("S07.Q08:R14");
    // LAB-S07.Q08:R14-END
}

// The visitor is deliberately a small traversal object rather than one large
// lambda.  Each node kind has a stable, independently reviewable rewrite
// path, while recursive query/table traversal stays in the free functions
// below and remains reusable by derived-table handling.
struct CorrelationExpressionRewriter final {
    std::uint32_t query_depth;
    CorrelationRewriteState& state;

    [[maybe_unused]] auto operator()([[maybe_unused]] const QueryLiteralExpression& literal) const
        -> base::Status {
        // LAB-S07.Q08:R15-BEGIN
        base::lab::missing_algorithm("S07.Q08:R15");
        // LAB-S07.Q08:R15-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryColumnReference& reference) const
        -> base::Status {
        // LAB-S07.Q08:R16-BEGIN
        base::lab::missing_algorithm("S07.Q08:R16");
        // LAB-S07.Q08:R16-END
    }

    [[maybe_unused]] auto
    operator()([[maybe_unused]] const QueryParameterReference& parameter) const -> base::Status {
        // LAB-S07.Q08:R17-BEGIN
        base::lab::missing_algorithm("S07.Q08:R17");
        // LAB-S07.Q08:R17-END
    }

    [[maybe_unused]] auto
    operator()([[maybe_unused]] const QueryExecutorParameterReference& parameter) const
        -> base::Status {
        // LAB-S07.Q08:R18-BEGIN
        base::lab::missing_algorithm("S07.Q08:R18");
        // LAB-S07.Q08:R18-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryUnaryExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R19-BEGIN
        base::lab::missing_algorithm("S07.Q08:R19");
        // LAB-S07.Q08:R19-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryBinaryExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R20-BEGIN
        base::lab::missing_algorithm("S07.Q08:R20");
        // LAB-S07.Q08:R20-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryBooleanExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R21-BEGIN
        base::lab::missing_algorithm("S07.Q08:R21");
        // LAB-S07.Q08:R21-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryIsNullExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R22-BEGIN
        base::lab::missing_algorithm("S07.Q08:R22");
        // LAB-S07.Q08:R22-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryInListExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R23-BEGIN
        base::lab::missing_algorithm("S07.Q08:R23");
        // LAB-S07.Q08:R23-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryBetweenExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R24-BEGIN
        base::lab::missing_algorithm("S07.Q08:R24");
        // LAB-S07.Q08:R24-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryCastExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R25-BEGIN
        base::lab::missing_algorithm("S07.Q08:R25");
        // LAB-S07.Q08:R25-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryAggregateExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R26-BEGIN
        base::lab::missing_algorithm("S07.Q08:R26");
        // LAB-S07.Q08:R26-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryScalarSubqueryExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R27-BEGIN
        base::lab::missing_algorithm("S07.Q08:R27");
        // LAB-S07.Q08:R27-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryExistsExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R28-BEGIN
        base::lab::missing_algorithm("S07.Q08:R28");
        // LAB-S07.Q08:R28-END
    }

    [[maybe_unused]] auto operator()([[maybe_unused]] QueryInSubqueryExpression& node) const
        -> base::Status {
        // LAB-S07.Q08:R29-BEGIN
        base::lab::missing_algorithm("S07.Q08:R29");
        // LAB-S07.Q08:R29-END
    }

private:
    [[maybe_unused]] auto rewrite_nested_query(
        [[maybe_unused]] PreprocessedQueryPtr& query,
        [[maybe_unused]] const std::string& subject
    ) const -> base::Status {
        // LAB-S07.Q08:R30-BEGIN
        base::lab::missing_algorithm("S07.Q08:R30");
        // LAB-S07.Q08:R30-END
    }
};

[[maybe_unused]] auto rewrite_expression_outer_refs(
    [[maybe_unused]] PreprocessedExpression& expression,
    [[maybe_unused]] const std::uint32_t query_depth,
    [[maybe_unused]] CorrelationRewriteState& state
) -> base::Status {
    // LAB-S07.Q08:R31-BEGIN
    base::lab::missing_algorithm("S07.Q08:R31");
    // LAB-S07.Q08:R31-END
}

[[maybe_unused]] auto rewrite_join_condition_outer_refs(
    [[maybe_unused]] PreprocessedJoinTable::Condition& condition,
    [[maybe_unused]] const std::uint32_t query_depth,
    [[maybe_unused]] CorrelationRewriteState& state
) -> base::Status {
    // LAB-S07.Q08:R32-BEGIN
    base::lab::missing_algorithm("S07.Q08:R32");
    // LAB-S07.Q08:R32-END
}

[[maybe_unused]] auto rewrite_table_outer_refs(
    [[maybe_unused]] PreprocessedTableReference& table,
    [[maybe_unused]] const std::uint32_t query_depth,
    [[maybe_unused]] CorrelationRewriteState& state
) -> base::Status {
    // LAB-S07.Q08:R33-BEGIN
    base::lab::missing_algorithm("S07.Q08:R33");
    // LAB-S07.Q08:R33-END
}

[[maybe_unused]] auto rewrite_query_outer_refs(
    [[maybe_unused]] PreprocessedQuery& query,
    [[maybe_unused]] const std::uint32_t query_depth,
    [[maybe_unused]] CorrelationRewriteState& state
) -> base::Status {
    // LAB-S07.Q08:R34-BEGIN
    base::lab::missing_algorithm("S07.Q08:R34");
    // LAB-S07.Q08:R34-END
}
}  // namespace

[[maybe_unused]] auto rewrite_subplan_correlation(
    [[maybe_unused]] PreprocessedQuery& query,
    [[maybe_unused]] const InputLayout& parent_input,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<SubPlanCorrelation> {
    // LAB-S07.Q08:R35-BEGIN
    base::lab::missing_algorithm("S07.Q08:R35");
    // LAB-S07.Q08:R35-END
}
}  // namespace db::planner::detail
