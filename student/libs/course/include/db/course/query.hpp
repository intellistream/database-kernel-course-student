#pragma once

#include "db/execution/exec_context.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/operators.hpp"
#include "db/execution/plan_state.hpp"
#include "db/execution/plan_state_factory.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/explain.hpp"
#include "db/planner/lowering.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/planner/plan.hpp"
#include "db/sql/ast_common.hpp"
#include "db/sql/binder.hpp"
#include "db/sql/bound_ast.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/parser.hpp"
#include "db/sql/source_ast.hpp"
#include "db/types/coercion.hpp"

namespace db::course::query {

using AggregateKind = planner::PlanAggregateKind;
using BinaryOperator = sql::BinaryOperator;
using BindResult = base::Result<sql::BoundScript>;
using BoundExpression = sql::BoundExpression;
using BoundQuery = sql::BoundQuery;
using BoundScript = sql::BoundScript;
using BoundStatement = sql::BoundStatement;
using CatalogReadPort = sql::CatalogReadPort;
using CoercionContext = types::CoercionContext;
using CoercionForm = sql::BoundCastForm;
using CoercionPath = types::CoercionPath;
using ExecContext = execution::ExecContext;
using ExpressionProgram = execution::ExpressionProgram;
using JoinKind = sql::JoinKind;
using MetadataPort = planner::MetadataPort;
using NullOrder = sql::NullOrder;
using Plan = planner::Plan;
using PlanError = base::Error;
using PlanResult = base::Result<planner::PlannedStatement>;
using PlannedStatement = planner::PlannedStatement;
using PlanState = execution::PlanState;
using PlanStateFactory = execution::PlanStateFactory;
using SortDirection = sql::SortDirection;
using SourceScript = sql::SourceScript;
using Tuple = execution::Tuple;
using TupleDescriptor = execution::TupleDescriptor;
using TupleSlot = execution::TupleSlot;
using UnaryOperator = sql::UnaryOperator;

using planner::assemble_planned_select;
using planner::lower_bound_select;
using planner::lower_bound_statement;
using planner::lower_preprocessed_select;
using planner::lower_preprocessed_statement;
using planner::plan_metadata;
using planner::plan_operator_kind;
using planner::render_explain;
using planner::validate_plan;
using planner::validate_planned_statement;
using sql::bind_script;
using sql::bind_script_with_parameters;
using sql::parse_sql;
using sql::source_position;
using types::apply_coercion;
using types::find_coercion_path;

}  // namespace db::course::query
