#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/result.hpp"
#include "db/execution/plan_state_factory.hpp"
#include "db/execution/subplan.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"

#include "execution_bench_support.hpp"

namespace db::benchmarks {
namespace {

namespace support = execution_support;

class GeneratedChildFactory final : public execution::PlanStateFactory {
public:
    GeneratedChildFactory(
        planner::PlanSchema schema,
        const std::size_t rows,
        const std::size_t key_cardinality
    )
        : schema_{std::move(schema)},
          rows_{rows},
          key_cardinality_{key_cardinality} {}

    auto create(const planner::Plan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>> override {
        static_cast<void>(plan);
        return support::row_source(schema_, rows_, key_cardinality_);
    }

private:
    planner::PlanSchema schema_;
    std::size_t rows_;
    std::size_t key_cardinality_;
};

auto dummy_plan(const planner::PlanSchema& schema) -> planner::PlanPtr {
    return std::make_unique<planner::Plan>(planner::ValuesPlan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .rows = {},
    });
}

void run_subplan(
    benchmark::State& state,
    base::Result<std::unique_ptr<execution::SubPlanEvalState>> executor,
    base::Result<execution::ExecContext> context,
    const std::size_t outer_rows,
    const char* const initialization_error
) {
    if (!executor || !context || !executor.value()->open(context.value())) {
        state.SkipWithError(initialization_error);
        return;
    }

    bool first = true;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        if (!first && !support::rescan_outside_timing(state, *executor.value(), context.value())) {
            return;
        }
        first = false;
        if (!support::drain(state, *executor.value(), context.value(), outer_rows)) {
            return;
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(outer_rows));
}

void scalar_subplan(benchmark::State& state) {
    const bool correlated = state.range(0) != 0;
    constexpr std::size_t outer_rows = 4096U;
    const planner::PlanSchema outer_schema = support::integer_schema(1U);
    const planner::PlanSchema scalar_schema = support::integer_schema(1U);

    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        state.SkipWithError("failed to construct subplan parameter namespace");
        return;
    }
    std::vector<planner::PlanExpressionPtr> arguments;
    std::vector<planner::PlanParameterReference> mappings;
    if (correlated) {
        auto parameter = parameters->allocate_executor_parameter(types::kInt64Type);
        if (!parameter) {
            state.SkipWithError("failed to allocate correlated subplan parameter");
            return;
        }
        arguments.push_back(support::input_column(0U));
        mappings.push_back(parameter.value());
    }

    auto input = support::row_source(outer_schema, outer_rows, outer_rows);
    if (!input) {
        state.SkipWithError("failed to construct subplan outer input");
        return;
    }
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = outer_schema},
        .input = nullptr,
        .subplans = {},
        .filter = nullptr,
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = dummy_plan(scalar_schema),
            .strategy = planner::PlanSubPlanStrategy::kScalar,
            .input_expression = nullptr,
            .arguments = std::move(arguments),
            .parameters = std::move(mappings),
            .uses_inherited_parameters = false,
        }
    );
    GeneratedChildFactory factory{scalar_schema, 1U, 1U};
    auto executor = execution::SubPlanEvalState::create(plan, std::move(input).value(), factory);
    auto context = execution::ExecContext::create(parameters.value(), {});
    run_subplan(
        state,
        std::move(executor),
        std::move(context),
        outer_rows,
        "failed to initialize scalar subplan benchmark"
    );
}

void benchmark_exists(benchmark::State& state, const planner::PlanSubPlanStrategy strategy) {
    const bool correlated = state.range(0) != 0;
    constexpr std::size_t outer_rows = 4096U;
    const planner::PlanSchema outer_schema = support::integer_schema(1U);
    const planner::PlanSchema child_schema = support::integer_schema(1U);

    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        state.SkipWithError("failed to construct EXISTS parameter namespace");
        return;
    }
    std::vector<planner::PlanExpressionPtr> arguments;
    std::vector<planner::PlanParameterReference> mappings;
    if (correlated) {
        auto parameter = parameters->allocate_executor_parameter(types::kInt64Type);
        if (!parameter) {
            state.SkipWithError("failed to allocate correlated EXISTS parameter");
            return;
        }
        arguments.push_back(support::input_column(0U));
        mappings.push_back(parameter.value());
    }

    auto input = support::row_source(outer_schema, outer_rows, outer_rows);
    if (!input) {
        state.SkipWithError("failed to construct EXISTS outer input");
        return;
    }
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = outer_schema},
        .input = nullptr,
        .subplans = {},
        .filter = nullptr,
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = dummy_plan(child_schema),
            .strategy = strategy,
            .input_expression = nullptr,
            .arguments = std::move(arguments),
            .parameters = std::move(mappings),
            .uses_inherited_parameters = false,
        }
    );
    GeneratedChildFactory factory{child_schema, 1U, 1U};
    auto executor = execution::SubPlanEvalState::create(plan, std::move(input).value(), factory);
    auto context = execution::ExecContext::create(parameters.value(), {});
    run_subplan(
        state,
        std::move(executor),
        std::move(context),
        outer_rows,
        "failed to initialize EXISTS subplan benchmark"
    );
}

void exists_subplan(benchmark::State& state) {
    benchmark_exists(state, planner::PlanSubPlanStrategy::kExists);
}

void not_exists_subplan(benchmark::State& state) {
    benchmark_exists(state, planner::PlanSubPlanStrategy::kNotExists);
}

void benchmark_membership(benchmark::State& state, const planner::PlanSubPlanStrategy strategy) {
    constexpr std::size_t outer_rows = 4096U;
    constexpr std::size_t child_rows = 64U;
    constexpr std::size_t outer_key_cardinality = 128U;
    const planner::PlanSchema outer_schema = support::integer_schema(1U);
    const planner::PlanSchema child_schema = support::integer_schema(1U);
    auto input = support::row_source(outer_schema, outer_rows, outer_key_cardinality);
    if (!input) {
        state.SkipWithError("failed to construct membership outer input");
        return;
    }
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = outer_schema},
        .input = nullptr,
        .subplans = {},
        .filter = nullptr,
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = dummy_plan(child_schema),
            .strategy = strategy,
            .input_expression = support::input_column(0U),
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    GeneratedChildFactory factory{child_schema, child_rows, child_rows};
    auto executor = execution::SubPlanEvalState::create(plan, std::move(input).value(), factory);
    run_subplan(
        state,
        std::move(executor),
        support::empty_context(),
        outer_rows,
        "failed to initialize membership subplan benchmark"
    );
}

void in_subplan(benchmark::State& state) {
    benchmark_membership(state, planner::PlanSubPlanStrategy::kIn);
}

void not_in_subplan(benchmark::State& state) {
    benchmark_membership(state, planner::PlanSubPlanStrategy::kNotIn);
}

// NOLINTBEGIN(cert-err58-cpp)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(scalar_subplan)
    ->Name("execution/scalar_subplan")
    ->ArgName("correlated")
    ->Args({0})
    ->Args({1});
BENCHMARK(exists_subplan)
    ->Name("execution/exists_subplan")
    ->ArgName("correlated")
    ->Args({0})
    ->Args({1});
BENCHMARK(not_exists_subplan)
    ->Name("execution/not_exists_subplan")
    ->ArgName("correlated")
    ->Args({0})
    ->Args({1});
BENCHMARK(in_subplan)->Name("execution/in_subplan");
BENCHMARK(not_in_subplan)->Name("execution/not_in_subplan");
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cert-err58-cpp)

}  // namespace
}  // namespace db::benchmarks
