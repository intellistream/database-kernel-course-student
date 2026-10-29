#pragma once

#include <string>

#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/planner/preprocess.hpp"

namespace db::planner::detail {

auto preprocess_error(std::string message) -> base::Error;
auto unsupported_query_shape(std::string message) -> base::Error;

using EmbeddedQueryPreprocessor = base::Status (*)(PreprocessedQuery&);

auto fold_expression_ptr(
    PreprocessedExpressionPtr expression,
    EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr>;
auto preprocess_qualification_ptr(
    PreprocessedExpressionPtr expression,
    EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr>;
auto expression_has_outer_reference(const PreprocessedExpression& expression) noexcept -> bool;

// Query/DML preparation folds left-to-right and enters each embedded query at
// its expression position.  The first error wins; only qualifications perform
// boolean canonicalization after the expression traversal completes.
auto preprocess_value_expression_ptr(PreprocessedExpressionPtr expression)
    -> base::Result<PreprocessedExpressionPtr>;
auto preprocess_qualification_expression_ptr(PreprocessedExpressionPtr expression)
    -> base::Result<PreprocessedExpressionPtr>;

auto preprocess_query_in_place(PreprocessedQuery& query) -> base::Status;

}  // namespace db::planner::detail
