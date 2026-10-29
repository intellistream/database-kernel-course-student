#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/result.hpp"
#include "db/execution/hash_join.hpp"
#include "db/execution/merge_join.hpp"
#include "db/execution/nested_loop.hpp"
#include "db/execution/plan_state.hpp"
#include "db/execution/subquery_scan.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::benchmarks {
namespace {

auto integer_schema(const std::string& prefix, const std::size_t column_count)
    -> planner::PlanSchema {
    planner::PlanSchema schema;
    schema.columns.reserve(column_count);
    for (std::size_t index = 0U; index < column_count; ++index) {
        schema.columns.push_back(
            planner::PlanOutputColumn{
                .name = prefix + std::to_string(index),
                .data_type = types::kInt64Type,
                .origin = std::nullopt,
            }
        );
    }
    return schema;
}

auto joined_schema(const planner::PlanSchema& outer, const planner::PlanSchema& inner)
    -> planner::PlanSchema {
    planner::PlanSchema result;
    result.columns.reserve(outer.columns.size() + inner.columns.size());
    result.columns.insert(result.columns.end(), outer.columns.begin(), outer.columns.end());
    result.columns.insert(result.columns.end(), inner.columns.begin(), inner.columns.end());
    return result;
}

class GeneratedRowsState final : public execution::PlanState {
public:
    GeneratedRowsState(execution::TupleSlot slot, const std::size_t columns, const std::size_t rows)
        : PlanState{std::move(slot)},
          columns_{columns},
          rows_{rows} {}

private:
    auto do_open(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        position_ = 0U;
        return {};
    }

    auto do_next(execution::ExecContext& context) -> base::Result<bool> override {
        static_cast<void>(context);
        if (position_ >= rows_) {
            return false;
        }
        std::vector<types::Value> values;
        values.reserve(columns_);
        values.push_back(types::Value::integer(static_cast<std::int64_t>(position_)));
        for (std::size_t column = 1U; column < columns_; ++column) {
            values.push_back(types::Value::integer(static_cast<std::int64_t>(column)));
        }
        ++position_;
        store_output_values(std::move(values));
        return true;
    }

    auto do_rescan(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        position_ = 0U;
        return {};
    }

    auto do_close(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        return {};
    }

    std::size_t columns_;
    std::size_t rows_;
    std::size_t position_{0U};
};

class ParameterRowState final : public execution::PlanState {
public:
    ParameterRowState(
        execution::TupleSlot slot,
        const std::size_t columns,
        planner::PlanParameterReference parameter
    )
        : PlanState{std::move(slot)},
          columns_{columns},
          parameter_{parameter} {}

private:
    auto do_open(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        emitted_ = false;
        return {};
    }

    auto do_next(execution::ExecContext& context) -> base::Result<bool> override {
        if (emitted_) {
            return false;
        }
        auto key = context.parameter(parameter_);
        if (!key) {
            return base::fail(std::move(key).error());
        }
        std::vector<types::Value> values;
        values.reserve(columns_);
        values.push_back(key->get());
        for (std::size_t column = 1U; column < columns_; ++column) {
            values.push_back(types::Value::integer(static_cast<std::int64_t>(column)));
        }
        emitted_ = true;
        store_output_values(std::move(values));
        return true;
    }

    auto do_rescan(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        emitted_ = false;
        return {};
    }

    auto do_close(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        return {};
    }

    std::size_t columns_;
    planner::PlanParameterReference parameter_;
    bool emitted_{false};
};

auto row_source(const planner::PlanSchema& schema, const std::size_t rows)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    auto descriptor = execution::TupleDescriptor::from_plan_schema(schema);
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    auto slot = execution::TupleSlot::create(std::move(descriptor).value());
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    return std::unique_ptr<execution::PlanState>{new GeneratedRowsState{
        std::move(slot).value(),
        schema.columns.size(),
        rows,
    }};
}

auto parameter_row_source(
    const planner::PlanSchema& schema,
    const planner::PlanParameterReference parameter
) -> base::Result<std::unique_ptr<execution::PlanState>> {
    auto descriptor = execution::TupleDescriptor::from_plan_schema(schema);
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    auto slot = execution::TupleSlot::create(std::move(descriptor).value());
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    return std::unique_ptr<execution::PlanState>{new ParameterRowState{
        std::move(slot).value(),
        schema.columns.size(),
        parameter,
    }};
}

auto empty_context() -> base::Result<execution::ExecContext> {
    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    return execution::ExecContext::create(parameters.value(), {});
}

auto parameter_context(planner::PlanParameterReference* const parameter)
    -> base::Result<execution::ExecContext> {
    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    auto allocated = parameters->allocate_executor_parameter(types::kInt64Type);
    if (!allocated) {
        return base::fail(std::move(allocated).error());
    }
    *parameter = allocated.value();
    return execution::ExecContext::create(parameters.value(), {});
}

template <typename State>
void run_join(
    benchmark::State& benchmark_state,
    base::Result<std::unique_ptr<State>> executor,
    base::Result<execution::ExecContext> context,
    const std::size_t output_rows,
    const char* const name
) {
    if (!executor || !context || !executor.value()->open(context.value())) {
        benchmark_state.SkipWithError("failed to initialize join benchmark");
        return;
    }

    bool first = true;
    for (auto iteration : benchmark_state) {
        static_cast<void>(iteration);
        if (!first) {
            benchmark_state.PauseTiming();
            auto rescanned = executor.value()->rescan(context.value());
            benchmark_state.ResumeTiming();
            if (!rescanned) {
                benchmark_state.SkipWithError("failed to rescan join");
                return;
            }
        }
        first = false;
        for (std::size_t row = 0U; row < output_rows; ++row) {
            auto next = executor.value()->next(context.value());
            if (!next || !next.value()) {
                benchmark_state.SkipWithError(name);
                return;
            }
        }
        auto exhausted = executor.value()->next(context.value());
        if (!exhausted || exhausted.value()) {
            benchmark_state.SkipWithError("join benchmark did not exhaust");
            return;
        }
    }
    benchmark_state.SetItemsProcessed(
        benchmark_state.iterations() * static_cast<std::int64_t>(output_rows)
    );
}

void hash_join_handoff(benchmark::State& state) {
    const auto column_count = static_cast<std::size_t>(state.range(0));
    const auto row_count = static_cast<std::size_t>(state.range(1));
    const planner::PlanSchema outer_schema = integer_schema("outer_", column_count);
    const planner::PlanSchema inner_schema = integer_schema("inner_", column_count);
    auto outer = row_source(outer_schema, row_count);
    auto inner = row_source(inner_schema, row_count);
    planner::HashJoinPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{.output = joined_schema(outer_schema, inner_schema)},
        .outer = nullptr,
        .inner = nullptr,
        .join_columns =
            {
                planner::PlanJoinColumnPair{
                    .outer_column = planner::PlanColumnIndex{0U},
                    .inner_column = planner::PlanColumnIndex{0U},
                },
            },
        .residual_filter = nullptr,
    };
    if (!outer || !inner) {
        state.SkipWithError("failed to construct hash-join inputs");
        return;
    }
    run_join(
        state,
        execution::HashJoinState::create(plan, std::move(outer).value(), std::move(inner).value()),
        empty_context(),
        row_count,
        "hash join ended early"
    );
}

void merge_join_handoff(benchmark::State& state) {
    const auto column_count = static_cast<std::size_t>(state.range(0));
    const auto row_count = static_cast<std::size_t>(state.range(1));
    const planner::PlanSchema outer_schema = integer_schema("outer_", column_count);
    const planner::PlanSchema inner_schema = integer_schema("inner_", column_count);
    auto outer = row_source(outer_schema, row_count);
    auto inner = row_source(inner_schema, row_count);
    planner::MergeJoinPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{.output = joined_schema(outer_schema, inner_schema)},
        .outer = nullptr,
        .inner = nullptr,
        .join_columns = {{
            .outer_column = planner::PlanColumnIndex{0U},
            .inner_column = planner::PlanColumnIndex{0U},
        }},
        .residual_filter = nullptr,
    };
    if (!outer || !inner) {
        state.SkipWithError("failed to construct merge-join inputs");
        return;
    }
    run_join(
        state,
        execution::MergeJoinState::create(plan, std::move(outer).value(), std::move(inner).value()),
        empty_context(),
        row_count,
        "merge join ended early"
    );
}

void nested_loop_parameter_handoff(benchmark::State& state) {
    const auto column_count = static_cast<std::size_t>(state.range(0));
    const auto row_count = static_cast<std::size_t>(state.range(1));
    const planner::PlanSchema outer_schema = integer_schema("outer_", column_count);
    const planner::PlanSchema inner_schema = integer_schema("inner_", column_count);
    planner::PlanParameterReference parameter;
    auto context = parameter_context(&parameter);
    auto outer = row_source(outer_schema, row_count);
    auto inner = parameter_row_source(inner_schema, parameter);
    planner::NestedLoopPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{.output = joined_schema(outer_schema, inner_schema)},
        .outer = nullptr,
        .inner = nullptr,
        .join_filter = nullptr,
        .parameters = {{
            .parameter = parameter,
            .outer_column = planner::PlanColumnIndex{0U},
        }},
    };
    if (!outer || !inner) {
        state.SkipWithError("failed to construct nested-loop inputs");
        return;
    }
    run_join(
        state,
        execution::NestedLoopState::create(
            plan,
            std::move(outer).value(),
            std::move(inner).value()
        ),
        std::move(context),
        row_count,
        "nested-loop join ended early"
    );
}

void subquery_scan_handoff(benchmark::State& state) {
    const auto column_count = static_cast<std::size_t>(state.range(0));
    const auto row_count = static_cast<std::size_t>(state.range(1));
    const planner::PlanSchema schema = integer_schema("column_", column_count);
    auto input = row_source(schema, row_count);
    if (!input) {
        state.SkipWithError("failed to construct subquery-scan input");
        return;
    }
    planner::SubqueryScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .subquery = nullptr,
        .alias = "bench",
        .filter = nullptr,
    };
    auto executor = execution::SubqueryScanState::create(plan, std::move(input).value());
    auto context = empty_context();
    if (!executor || !context || !executor.value()->open(context.value())) {
        state.SkipWithError("failed to initialize subquery-scan benchmark");
        return;
    }

    bool first = true;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        if (!first) {
            state.PauseTiming();
            auto rescanned = executor.value()->rescan(context.value());
            state.ResumeTiming();
            if (!rescanned) {
                state.SkipWithError("failed to rescan subquery scan");
                break;
            }
        }
        first = false;
        for (std::size_t row = 0U; row < row_count; ++row) {
            auto next = executor.value()->next(context.value());
            if (!next || !next.value()) {
                state.SkipWithError("subquery scan ended early");
                return;
            }
        }
        auto exhausted = executor.value()->next(context.value());
        if (!exhausted || exhausted.value()) {
            state.SkipWithError("subquery scan did not exhaust");
            return;
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(row_count));
}

// NOLINTBEGIN(cert-err58-cpp)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(hash_join_handoff)
    ->Name("execution/hash_join_handoff")
    ->ArgsProduct({{64}, {4096}})
    ->ArgNames({"columns", "rows"});
BENCHMARK(merge_join_handoff)
    ->Name("execution/merge_join_handoff")
    ->ArgsProduct({{64}, {4096}})
    ->ArgNames({"columns", "rows"});
BENCHMARK(nested_loop_parameter_handoff)
    ->Name("execution/nested_loop_parameter_handoff")
    ->ArgsProduct({{64}, {4096}})
    ->ArgNames({"columns", "rows"});
BENCHMARK(subquery_scan_handoff)
    ->Name("execution/subquery_scan_handoff")
    ->ArgsProduct({{64}, {4096}})
    ->ArgNames({"columns", "rows"});
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cert-err58-cpp)

}  // namespace
}  // namespace db::benchmarks
