#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/result.hpp"
#include "db/execution/exec_context.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/lowering.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/operator.hpp"
#include "db/types/value.hpp"

namespace db::benchmarks {
namespace {

using planner::PlanExpression;
using planner::PlanExpressionPtr;

class ConstantMetadataPort final : public planner::MetadataPort {
public:
    auto generation() const noexcept -> std::uint64_t override {
        return 1U;
    }

    auto lookup_table(const base::Oid oid) const
        -> base::Result<std::optional<planner::PlanningTable>> override {
        static_cast<void>(oid);
        return std::optional<planner::PlanningTable>{};
    }

    auto indexes_for_table(const base::Oid table_oid) const
        -> base::Result<std::vector<planner::PlanningIndex>> override {
        static_cast<void>(table_oid);
        return std::vector<planner::PlanningIndex>{};
    }
};

auto input_column() -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kInt64Type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{0U}}
    );
}

auto literal(const std::int64_t value) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kInt64Type,
        planner::PlanLiteralExpression{.value = types::Value::integer(value)}
    );
}

auto binary(
    const types::BinaryOperator operation,
    const types::DataType result_type,
    PlanExpressionPtr left,
    PlanExpressionPtr right
) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        result_type,
        planner::PlanBinaryExpression{
            .op = operation,
            .left = std::move(left),
            .right = std::move(right),
        }
    );
}

auto empty_context() -> base::Result<execution::ExecContext> {
    const std::vector<planner::ExternalParameterSlot> slots;
    auto parameters = planner::ParameterNamespace::from_external_slots(slots);
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    return execution::ExecContext::create(parameters.value(), {});
}

auto integer_tuple(const std::int64_t value) -> base::Result<execution::Tuple> {
    auto descriptor = execution::TupleDescriptor::create({execution::TupleColumn{
        .name = "value",
        .data_type = types::kInt64Type,
    }});
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return execution::Tuple::create(std::move(descriptor).value(), {types::Value::integer(value)});
}

auto empty_tuple() -> base::Result<execution::Tuple> {
    auto descriptor = execution::TupleDescriptor::create({});
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return execution::Tuple::create(std::move(descriptor).value(), {});
}

void input_arithmetic(benchmark::State& state) {
    auto tuple = integer_tuple(41);
    auto context = empty_context();
    auto expression =
        binary(types::BinaryOperator::kAdd, types::kInt64Type, input_column(), literal(1));
    auto program = execution::ExpressionProgram::compile(*expression);
    if (!tuple || !context || !program) {
        state.SkipWithError("failed to initialize expression benchmark");
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto result = program->evaluate(tuple.value(), context.value());
        if (!result) {
            state.SkipWithError("expression evaluation failed");
            break;
        }
        benchmark::DoNotOptimize(result->as_integer());
    }
    state.SetItemsProcessed(state.iterations());
}

void input_qualification(benchmark::State& state) {
    auto tuple = integer_tuple(41);
    auto context = empty_context();
    auto expression =
        binary(types::BinaryOperator::kEqual, types::kBoolType, input_column(), literal(41));
    auto program = execution::ExpressionProgram::compile(*expression);
    if (!tuple || !context || !program) {
        state.SkipWithError("failed to initialize qualification benchmark");
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto result = program->evaluate_qualification(tuple.value(), context.value());
        if (!result) {
            state.SkipWithError("qualification evaluation failed");
            break;
        }
        benchmark::DoNotOptimize(result.value());
    }
    state.SetItemsProcessed(state.iterations());
}

void parameter_arithmetic(benchmark::State& state) {
    const std::vector external_slots{
        planner::ExternalParameterSlot{
            .slot = planner::ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
    };
    auto parameters = planner::ParameterNamespace::from_external_slots(external_slots);
    if (!parameters) {
        state.SkipWithError("failed to initialize parameter namespace");
        return;
    }
    const std::vector values{types::Value::integer(41)};
    auto context = execution::ExecContext::create(parameters.value(), values);
    auto tuple = empty_tuple();
    if (!context || !tuple) {
        state.SkipWithError("failed to initialize parameter benchmark");
        return;
    }
    auto reference = parameters->external_reference(planner::ExternalParameterNumber{1U});
    if (!reference) {
        state.SkipWithError("failed to resolve parameter benchmark reference");
        return;
    }
    auto expression = binary(
        types::BinaryOperator::kAdd,
        types::kInt64Type,
        std::make_unique<PlanExpression>(
            planner::PlanSourceSpan{},
            types::kInt64Type,
            reference.value()
        ),
        literal(1)
    );
    auto program = execution::ExpressionProgram::compile(*expression);
    if (!program) {
        state.SkipWithError("failed to compile parameter benchmark expression");
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto result = program->evaluate(tuple.value(), context.value());
        if (!result) {
            state.SkipWithError("parameter expression evaluation failed");
            break;
        }
        benchmark::DoNotOptimize(result->as_integer());
    }
    state.SetItemsProcessed(state.iterations());
}

void exec_context_create(benchmark::State& state) {
    const auto parameter_count = static_cast<std::size_t>(state.range(0));
    std::vector<planner::ExternalParameterSlot> external_slots;
    std::vector<types::Value> values;
    external_slots.reserve(parameter_count);
    values.reserve(parameter_count);
    for (std::size_t index = 0U; index < parameter_count; ++index) {
        external_slots.push_back(
            planner::ExternalParameterSlot{
                .slot = planner::ExternalParameterNumber{static_cast<std::uint32_t>(index + 1U)},
                .data_type = types::kInt64Type,
            }
        );
        values.push_back(types::Value::integer(static_cast<std::int64_t>(index)));
    }
    auto parameters = planner::ParameterNamespace::from_external_slots(external_slots);
    if (!parameters) {
        state.SkipWithError("failed to initialize execution-context benchmark");
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto context = execution::ExecContext::create(parameters.value(), values);
        if (!context) {
            state.SkipWithError("execution-context construction failed");
            break;
        }
        benchmark::DoNotOptimize(&context.value());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(parameter_count));
}

void tuple_descriptor_from_plan(benchmark::State& state) {
    const auto column_count = static_cast<std::size_t>(state.range(0));
    planner::PlanSchema schema;
    schema.columns.reserve(column_count);
    for (std::size_t index = 0U; index < column_count; ++index) {
        schema.columns.push_back(
            planner::PlanOutputColumn{
                .name = "column_" + std::to_string(index),
                .data_type = types::kInt64Type,
                .origin = std::nullopt,
            }
        );
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto descriptor = execution::TupleDescriptor::from_plan_schema(schema);
        if (!descriptor) {
            state.SkipWithError("tuple descriptor construction failed");
            break;
        }
        benchmark::DoNotOptimize(descriptor->get());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(column_count));
}

auto constant_query(const std::size_t column_count) -> planner::PreprocessedQuery {
    planner::PreprocessedSelectQuery select;
    select.projection.reserve(column_count);
    for (std::size_t index = 0U; index < column_count; ++index) {
        select.projection.push_back(
            planner::PreprocessedProjectionItem{
                .expression = std::make_unique<planner::PreprocessedExpression>(
                    sql::SourceSpan{},
                    types::kInt64Type,
                    planner::QueryLiteralExpression{
                        .value = types::Value::integer(static_cast<std::int64_t>(index)),
                    }
                ),
                .alias = "column_" + std::to_string(index),
            }
        );
    }
    return planner::PreprocessedQuery{sql::SourceSpan{}, std::move(select)};
}

void lower_and_assemble(benchmark::State& state) {
    const auto column_count = static_cast<std::size_t>(state.range(0));
    ConstantMetadataPort metadata;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto parameters = planner::ParameterNamespace::from_external_slots({});
        if (!parameters) {
            state.SkipWithError("parameter namespace construction failed");
            break;
        }
        auto lowered = planner::lower_preprocessed_select(
            constant_query(column_count),
            metadata,
            std::move(parameters).value()
        );
        if (!lowered) {
            state.SkipWithError("SELECT lowering failed");
            break;
        }
        auto planned = planner::assemble_planned_select(std::move(lowered).value());
        if (!planned) {
            state.SkipWithError("SELECT assembly failed");
            break;
        }
        benchmark::DoNotOptimize(&planned->root());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(column_count));
}

// Google Benchmark's required registration macro owns a process-lifetime object
// through a mutable static pointer. The framework, rather than this benchmark,
// defines that registration lifetime.
// NOLINTBEGIN(cert-err58-cpp)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(input_arithmetic)->Name("expression/input_arithmetic");
BENCHMARK(input_qualification)->Name("expression/input_qualification");
BENCHMARK(parameter_arithmetic)->Name("expression/parameter_arithmetic");
BENCHMARK(exec_context_create)
    ->Name("execution/exec_context_create")
    ->ArgName("parameters")
    ->Arg(64);
BENCHMARK(tuple_descriptor_from_plan)
    ->Name("execution/tuple_descriptor_from_plan")
    ->ArgName("columns")
    ->Arg(64);
BENCHMARK(lower_and_assemble)->Name("planner/lower_and_assemble")->ArgName("columns")->Arg(64);
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cert-err58-cpp)

}  // namespace
}  // namespace db::benchmarks
