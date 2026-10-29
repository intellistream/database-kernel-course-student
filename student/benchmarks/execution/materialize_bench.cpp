#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/result.hpp"
#include "db/execution/aggregate.hpp"
#include "db/execution/sort.hpp"
#include "db/execution/topn.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"

#include "execution_bench_support.hpp"

namespace db::benchmarks {
namespace {

namespace support = execution_support;

auto count_star() -> planner::PlanAggregate {
    return planner::PlanAggregate{
        .kind = planner::PlanAggregateKind::kCount,
        .arguments = {},
        .distinct = false,
        .star = true,
        .output_name = "count",
        .data_type = types::kInt64Type,
        .aggregate_index = planner::PlanAggregateIndex{0U},
    };
}

auto aggregate_output_schema(const bool grouped) -> planner::PlanSchema {
    planner::PlanSchema schema;
    if (grouped) {
        schema.columns.push_back(
            planner::PlanOutputColumn{
                .name = "group_key",
                .data_type = types::kInt64Type,
                .origin = std::nullopt,
            }
        );
    }
    schema.columns.push_back(
        planner::PlanOutputColumn{
            .name = "count",
            .data_type = types::kInt64Type,
            .origin = std::nullopt,
        }
    );
    return schema;
}

auto aggregate_state(const std::size_t rows, const std::size_t groups)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    const planner::PlanSchema input_schema = support::integer_schema(2U);
    auto input = support::row_source(input_schema, rows, groups);
    if (!input) {
        return base::fail(std::move(input).error());
    }
    const bool grouped = groups != 0U;
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = aggregate_output_schema(grouped)},
        .input = nullptr,
        .group_by = {},
        .aggregates = {},
        .having = nullptr,
        .ordered_min = false,
    };
    if (grouped) {
        plan.group_by.push_back(support::input_column(0U));
    }
    plan.aggregates.push_back(count_star());
    auto executor = execution::AggregateState::create(plan, std::move(input).value());
    if (!executor) {
        return base::fail(std::move(executor).error());
    }
    return std::unique_ptr<execution::PlanState>{std::move(executor).value()};
}

auto ascending_key() -> planner::PlanSortItem {
    return planner::PlanSortItem{
        .expression = support::input_column(1U),
        .direction = planner::PlanSortDirection::kAscending,
        .null_order = planner::PlanNullOrder::kDefault,
    };
}

auto sort_state(const std::size_t rows) -> base::Result<std::unique_ptr<execution::PlanState>> {
    const planner::PlanSchema schema = support::integer_schema(2U);
    auto input = support::row_source(schema, rows, rows);
    if (!input) {
        return base::fail(std::move(input).error());
    }
    planner::SortPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .order_by = {},
    };
    plan.order_by.push_back(ascending_key());
    auto executor = execution::SortState::create(plan, std::move(input).value());
    if (!executor) {
        return base::fail(std::move(executor).error());
    }
    return std::unique_ptr<execution::PlanState>{std::move(executor).value()};
}

auto topn_state(const std::size_t rows, const std::size_t limit)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    const planner::PlanSchema schema = support::integer_schema(2U);
    auto input = support::row_source(schema, rows, rows);
    if (!input) {
        return base::fail(std::move(input).error());
    }
    planner::TopNPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .order_by = {},
        .limit = support::integer_literal(static_cast<std::int64_t>(limit)),
        .offset = support::integer_literal(0),
    };
    plan.order_by.push_back(ascending_key());
    auto executor = execution::TopNState::create(plan, std::move(input).value());
    if (!executor) {
        return base::fail(std::move(executor).error());
    }
    return std::unique_ptr<execution::PlanState>{std::move(executor).value()};
}

template <typename Factory>
void benchmark_rows(
    benchmark::State& state,
    Factory&& factory,
    const std::size_t input_rows,
    const std::size_t output_rows,
    const char* const initialization_error
) {
    auto executor = factory();
    auto context = support::empty_context();
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
        if (!support::drain(state, *executor.value(), context.value(), output_rows)) {
            return;
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(input_rows));
}

void aggregate_scalar_count(benchmark::State& state) {
    const auto rows = static_cast<std::size_t>(state.range(0));
    benchmark_rows(
        state,
        [rows] {
            return aggregate_state(rows, 0U);
        },
        rows,
        1U,
        "failed to initialize scalar aggregate"
    );
}

void aggregate_grouped_count(benchmark::State& state) {
    constexpr std::size_t input_rows = 16'384U;
    const auto groups = static_cast<std::size_t>(state.range(0));
    benchmark_rows(
        state,
        [groups] {
            return aggregate_state(input_rows, groups);
        },
        input_rows,
        groups,
        "failed to initialize grouped aggregate"
    );
}

void full_sort(benchmark::State& state) {
    const auto rows = static_cast<std::size_t>(state.range(0));
    benchmark_rows(
        state,
        [rows] {
            return sort_state(rows);
        },
        rows,
        rows,
        "failed to initialize full sort"
    );
}

void bounded_topn(benchmark::State& state) {
    const auto rows = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t output_limit = 100U;
    benchmark_rows(
        state,
        [rows] {
            return topn_state(rows, output_limit);
        },
        rows,
        output_limit,
        "failed to initialize bounded TopN"
    );
}

// NOLINTBEGIN(cert-err58-cpp)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(aggregate_scalar_count)
    ->Name("execution/aggregate_scalar_count")
    ->ArgName("rows")
    ->Args({4096})
    ->Args({65536});
BENCHMARK(aggregate_grouped_count)
    ->Name("execution/aggregate_grouped_count")
    ->ArgName("groups")
    ->Args({16})
    ->Args({256})
    ->Args({4096});
BENCHMARK(full_sort)->Name("execution/full_sort")->ArgName("rows")->Args({4096})->Args({65536});
BENCHMARK(bounded_topn)
    ->Name("execution/bounded_topn")
    ->ArgName("rows")
    ->Args({4096})
    ->Args({65536});
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cert-err58-cpp)

}  // namespace
}  // namespace db::benchmarks
