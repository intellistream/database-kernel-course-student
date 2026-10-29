#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/result.hpp"
#include "db/execution/modify_table.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::benchmarks {
namespace {

auto integer_schema(const std::size_t column_count) -> planner::PlanSchema {
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
    return schema;
}

class RowSourceState final : public execution::PlanState {
public:
    RowSourceState(execution::TupleSlot slot, std::vector<types::Value> row, const std::size_t rows)
        : PlanState{std::move(slot)},
          row_{std::move(row)},
          rows_{rows} {}

protected:
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
        store_output_values(
            row_,
            base::Rid{
                base::PageId{static_cast<std::uint32_t>(position_ / 256U + 1U)},
                base::SlotId{static_cast<std::uint16_t>(position_ % 256U)},
            }
        );
        ++position_;
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

private:
    std::vector<types::Value> row_;
    std::size_t rows_;
    std::size_t position_{0U};
};

class RowSourceFactory final : public execution::PlanStateFactory {
public:
    RowSourceFactory(planner::PlanSchema schema, const std::size_t rows)
        : schema_{std::move(schema)},
          rows_{rows} {}

    auto create(const planner::Plan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>> override {
        static_cast<void>(plan);
        auto descriptor = execution::TupleDescriptor::from_plan_schema(schema_);
        if (!descriptor) {
            return base::fail(std::move(descriptor).error());
        }
        std::vector<types::Value> row;
        row.reserve(schema_.columns.size());
        for (std::size_t index = 0U; index < schema_.columns.size(); ++index) {
            row.push_back(types::Value::integer(static_cast<std::int64_t>(index)));
        }
        auto slot = execution::TupleSlot::create(std::move(descriptor).value());
        if (!slot) {
            return base::fail(std::move(slot).error());
        }
        return std::unique_ptr<execution::PlanState>{new RowSourceState{
            std::move(slot).value(),
            std::move(row),
            rows_,
        }};
    }

private:
    planner::PlanSchema schema_;
    std::size_t rows_;
};

class CountingModifyPort final : public execution::ModifyTablePort {
public:
    auto insert(const execution::ModifyTarget& target, std::span<const types::Value> values)
        -> base::Result<base::Rid> override {
        static_cast<void>(target);
        benchmark::DoNotOptimize(values.data());
        ++inserts_;
        return base::Rid{base::PageId{1U}, base::SlotId{1U}};
    }

    auto update(
        const execution::ModifyTarget& target,
        base::Rid rid,
        std::span<const types::Value> old_values,
        std::span<const types::Value> new_values
    ) -> base::Result<execution::ModifyUpdateResult> override {
        static_cast<void>(target);
        benchmark::DoNotOptimize(rid);
        benchmark::DoNotOptimize(old_values.data());
        benchmark::DoNotOptimize(new_values.data());
        bool values_unchanged =
            std::equal(old_values.begin(), old_values.end(), new_values.begin(), new_values.end());
        benchmark::DoNotOptimize(values_unchanged);
        ++updates_;
        return execution::ModifyUpdateResult{
            .rid = rid,
            .index_maintenance_required = !values_unchanged,
        };
    }

    auto delete_row(
        const execution::ModifyTarget& target,
        base::Rid rid,
        std::span<const types::Value> old_values
    ) -> base::Status override {
        static_cast<void>(target);
        static_cast<void>(rid);
        benchmark::DoNotOptimize(old_values.data());
        ++deletes_;
        return {};
    }

    auto mutations() const noexcept -> std::size_t {
        return inserts_ + updates_ + deletes_;
    }

private:
    std::size_t inserts_{0U};
    std::size_t updates_{0U};
    std::size_t deletes_{0U};
};

auto target_relation(const planner::PlanSchema& schema) -> planner::PlanRelation {
    return planner::PlanRelation{
        .oid = base::Oid{1U},
        .name = "bench",
        .alias = std::nullopt,
        .schema = schema,
    };
}

auto query_source(const planner::PlanSchema& schema) -> planner::PlanPtr {
    return std::make_unique<planner::Plan>(planner::ValuesPlan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .rows = {},
    });
}

auto input_column(const std::size_t index) -> planner::PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        types::kInt64Type,
        planner::PlanInputColumnReference{
            .column = planner::PlanColumnIndex{static_cast<std::uint32_t>(index)}
        }
    );
}

auto query_modify_plan(const planner::PlanSchema& schema, const planner::ModifyOperation operation)
    -> planner::ModifyTablePlan {
    planner::ModifyTablePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = {}},
        .operation = operation,
        .target = target_relation(schema),
        .target_columns = {},
        .assignments = {},
        .source = planner::PlanQueryModificationSource{
            .input = query_source(schema),
        },
    };
    if (operation == planner::ModifyOperation::kInsert) {
        plan.target_columns.reserve(schema.columns.size());
        for (std::size_t index = 0U; index < schema.columns.size(); ++index) {
            plan.target_columns.emplace_back(static_cast<std::uint32_t>(index));
        }
    } else if (operation == planner::ModifyOperation::kUpdate) {
        const std::size_t target_column = schema.columns.size() - 1U;
        plan.assignments.push_back(
            planner::PlanAssignment{
                .target_column =
                    planner::PlanColumnIndex{static_cast<std::uint32_t>(target_column)},
                .expression = input_column(0U),
            }
        );
    }
    return plan;
}

auto empty_context() -> base::Result<execution::ExecContext> {
    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    return execution::ExecContext::create(parameters.value(), {});
}

void modify_rows(benchmark::State& state, const planner::ModifyOperation operation) {
    const auto column_count = static_cast<std::size_t>(state.range(0));
    const auto row_count = static_cast<std::size_t>(state.range(1));
    const planner::PlanSchema schema = integer_schema(column_count);
    RowSourceFactory factory{schema, row_count};
    CountingModifyPort port;
    const auto plan = query_modify_plan(schema, operation);
    auto executor = execution::ModifyTableState::create(plan, factory, port);
    auto context = empty_context();
    if (!executor || !context || !executor.value()->open(context.value())) {
        state.SkipWithError("failed to initialize ModifyTable benchmark");
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
                state.SkipWithError("failed to rescan ModifyTable benchmark");
                break;
            }
        }
        first = false;
        for (std::size_t row = 0U; row < row_count; ++row) {
            auto next = executor.value()->next(context.value());
            if (!next || !next.value()) {
                state.SkipWithError("ModifyTable benchmark ended early");
                return;
            }
        }
        auto exhausted = executor.value()->next(context.value());
        if (!exhausted || exhausted.value()) {
            state.SkipWithError("ModifyTable benchmark did not exhaust");
            return;
        }
    }
    const auto expected_mutations = static_cast<std::size_t>(state.iterations()) * row_count;
    if (port.mutations() != expected_mutations) {
        state.SkipWithError("ModifyTable benchmark produced the wrong mutation count");
        return;
    }
    benchmark::DoNotOptimize(port.mutations());
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(row_count));
}

void insert_rows(benchmark::State& state) {
    modify_rows(state, planner::ModifyOperation::kInsert);
}

void update_rows(benchmark::State& state) {
    modify_rows(state, planner::ModifyOperation::kUpdate);
}

void delete_rows(benchmark::State& state) {
    modify_rows(state, planner::ModifyOperation::kDelete);
}

// NOLINTBEGIN(cert-err58-cpp)
// NOLINTBEGIN(bugprone-throwing-static-initialization)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(insert_rows)
    ->Name("execution/modify_insert_query")
    ->ArgsProduct({{64}, {4096}})
    ->ArgNames({"columns", "rows"});
BENCHMARK(update_rows)
    ->Name("execution/modify_update")
    ->ArgsProduct({{64}, {4096}})
    ->ArgNames({"columns", "rows"});
BENCHMARK(delete_rows)
    ->Name("execution/modify_delete")
    ->ArgsProduct({{64}, {4096}})
    ->ArgNames({"columns", "rows"});
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(bugprone-throwing-static-initialization)
// NOLINTEND(cert-err58-cpp)

}  // namespace
}  // namespace db::benchmarks
