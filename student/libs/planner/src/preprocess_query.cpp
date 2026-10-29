#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/lab_missing.hpp"
#include "db/planner/preprocess.hpp"
#include "db/types/type_registry.hpp"

#include "preprocess_internal.hpp"

namespace db::planner {
namespace {

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

[[maybe_unused]] auto preprocess_qualification_field(
    [[maybe_unused]] PreprocessedExpressionPtr& expression,
    [[maybe_unused]] const std::string& context
) -> base::Status {
    // LAB-S02.Q05:R94-BEGIN
    base::lab::missing_algorithm("S02.Q05:R94");
    // LAB-S02.Q05:R94-END
}

[[maybe_unused]] auto preprocess_expression_field(
    [[maybe_unused]] PreprocessedExpressionPtr& destination,
    [[maybe_unused]] const std::string& context
) -> base::Status {
    // LAB-S02.Q05:R95-BEGIN
    base::lab::missing_algorithm("S02.Q05:R95");
    // LAB-S02.Q05:R95-END
}

[[maybe_unused]] auto preprocess_expression_list(
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr>& expressions,
    [[maybe_unused]] const std::string& context
) -> base::Status {
    // LAB-S02.Q05:R96-BEGIN
    base::lab::missing_algorithm("S02.Q05:R96");
    // LAB-S02.Q05:R96-END
}

[[maybe_unused]] auto preprocess_table([[maybe_unused]] PreprocessedTableReference& table)
    -> base::Status {
    // LAB-S02.Q05:R97-BEGIN
    base::lab::missing_algorithm("S02.Q05:R97");
    // LAB-S02.Q05:R97-END
}

[[maybe_unused]] auto is_identity_column_projection(
    [[maybe_unused]] const PreprocessedSelectQuery& query,
    [[maybe_unused]] const PreprocessedBaseTable& base_table
) -> bool {
    // LAB-S02.Q05:R98-BEGIN
    base::lab::missing_algorithm("S02.Q05:R98");
    // LAB-S02.Q05:R98-END
}

[[maybe_unused]] auto eligible_pull_up_base([[maybe_unused]] PreprocessedDerivedTable& derived)
    -> PreprocessedBaseTable* {
    // LAB-S02.Q05:R99-BEGIN
    base::lab::missing_algorithm("S02.Q05:R99");
    // LAB-S02.Q05:R99-END
}

[[maybe_unused]] void remap_reference(
    [[maybe_unused]] QueryColumnReference& reference,
    [[maybe_unused]] const QueryRangeTableIndex old_index,
    [[maybe_unused]] const QueryRangeTableIndex new_index
) {
    // LAB-S02.Q05:R100-BEGIN
    base::lab::missing_algorithm("S02.Q05:R100");
    // LAB-S02.Q05:R100-END
}

void remap_expression_pointer(
    PreprocessedExpressionPtr& expression,
    QueryRangeTableIndex old_index,
    QueryRangeTableIndex new_index
);

[[maybe_unused]] void remap_expression_list(
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr>& expressions,
    [[maybe_unused]] const QueryRangeTableIndex old_index,
    [[maybe_unused]] const QueryRangeTableIndex new_index
) {
    // LAB-S02.Q05:R101-BEGIN
    base::lab::missing_algorithm("S02.Q05:R101");
    // LAB-S02.Q05:R101-END
}

[[maybe_unused]] void remap_expression_pointer(
    [[maybe_unused]] PreprocessedExpressionPtr& expression,
    [[maybe_unused]] const QueryRangeTableIndex old_index,
    [[maybe_unused]] const QueryRangeTableIndex new_index
) {
    // LAB-S02.Q05:R102-BEGIN
    base::lab::missing_algorithm("S02.Q05:R102");
    // LAB-S02.Q05:R102-END
}

[[maybe_unused]] auto merge_filter(
    [[maybe_unused]] PreprocessedExpressionPtr& destination,
    [[maybe_unused]] PreprocessedExpressionPtr source
) -> base::Status {
    // LAB-S02.Q05:R103-BEGIN
    base::lab::missing_algorithm("S02.Q05:R103");
    // LAB-S02.Q05:R103-END
}

[[maybe_unused]] auto simplify_table(
    [[maybe_unused]] PreprocessedTableReferencePtr& table,
    [[maybe_unused]] PreprocessedExpressionPtr& parent_filter
) -> base::Status {
    // LAB-S02.Q05:R104-BEGIN
    base::lab::missing_algorithm("S02.Q05:R104");
    // LAB-S02.Q05:R104-END
}

[[maybe_unused]] auto simplify_validated_jointree([[maybe_unused]] PreprocessedQuery& query)
    -> base::Status {
    // LAB-S02.Q05:R105-BEGIN
    base::lab::missing_algorithm("S02.Q05:R105");
    // LAB-S02.Q05:R105-END
}
}  // namespace

namespace detail {

[[maybe_unused]] auto
preprocess_value_expression_ptr([[maybe_unused]] PreprocessedExpressionPtr expression)
    -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S02.Q05:R106-BEGIN
    base::lab::missing_algorithm("S02.Q05:R106");
    // LAB-S02.Q05:R106-END
}

[[maybe_unused]] auto
preprocess_qualification_expression_ptr([[maybe_unused]] PreprocessedExpressionPtr expression)
    -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S02.Q05:R107-BEGIN
    base::lab::missing_algorithm("S02.Q05:R107");
    // LAB-S02.Q05:R107-END
}

[[maybe_unused]] auto preprocess_query_in_place([[maybe_unused]] PreprocessedQuery& query)
    -> base::Status {
    // LAB-S02.Q05:R108-BEGIN
    base::lab::missing_algorithm("S02.Q05:R108");
    // LAB-S02.Q05:R108-END
}
}  // namespace detail

[[maybe_unused]] auto simplify_jointree([[maybe_unused]] PreprocessedQuery& query) -> base::Status {
    // LAB-S02.Q05:R109-BEGIN
    base::lab::missing_algorithm("S02.Q05:R109");
    // LAB-S02.Q05:R109-END
}
}  // namespace db::planner
