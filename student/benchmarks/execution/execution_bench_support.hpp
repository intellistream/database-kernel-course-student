#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/result.hpp"
#include "db/execution/exec_context.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::benchmarks::execution_support {

inline auto integer_schema(const std::size_t column_count) -> planner::PlanSchema {
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

inline auto input_column(const std::uint32_t index) -> planner::PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        types::kInt64Type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{index}}
    );
}

inline auto integer_literal(const std::int64_t value) -> planner::PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        types::kInt64Type,
        planner::PlanLiteralExpression{.value = types::Value::integer(value)}
    );
}

inline auto empty_context() -> base::Result<execution::ExecContext> {
    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    return execution::ExecContext::create(parameters.value(), {});
}

// A deterministic in-memory child for executor benchmarks. It refills storage
// reclaimed from its resident output slot, matching the ownership boundary of
// a real scan while excluding page decoding and buffer-pool work.
class GeneratedRowsState final : public execution::PlanState {
public:
    struct Config final {
        std::size_t columns{0U};
        std::size_t rows{0U};
        std::size_t key_cardinality{0U};
    };

    GeneratedRowsState(execution::TupleSlot slot, const Config config)
        : PlanState{std::move(slot)},
          columns_{config.columns},
          rows_{config.rows},
          key_cardinality_{config.key_cardinality} {}

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
        std::vector<types::Value> values = take_recycled_output_values();
        values.clear();
        values.reserve(columns_);
        const std::size_t cardinality = key_cardinality_ == 0U ? rows_ : key_cardinality_;
        const auto key = static_cast<std::int64_t>(position_ % cardinality);
        values.push_back(types::Value::integer(key));
        for (std::size_t column = 1U; column < columns_; ++column) {
            const auto value = column == 1U ? static_cast<std::int64_t>(rows_ - position_)
                                            : static_cast<std::int64_t>(column);
            values.push_back(types::Value::integer(value));
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

private:
    std::size_t columns_;
    std::size_t rows_;
    std::size_t key_cardinality_;
    std::size_t position_{0U};
};

inline auto row_source(
    const planner::PlanSchema& schema,
    const std::size_t rows,
    const std::size_t key_cardinality = 0U
) -> base::Result<std::unique_ptr<execution::PlanState>> {
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
        GeneratedRowsState::Config{
            .columns = schema.columns.size(),
            .rows = rows,
            .key_cardinality = key_cardinality,
        },
    }};
}

inline auto drain(
    benchmark::State& state,
    execution::PlanState& executor,
    execution::ExecContext& context,
    const std::size_t expected_rows
) -> bool {
    std::size_t rows = 0U;
    while (true) {
        auto next = executor.next(context);
        if (!next) {
            state.SkipWithError("executor next failed");
            return false;
        }
        if (!next.value()) {
            break;
        }
        const execution::Tuple* tuple = executor.output_slot().tuple();
        if (tuple == nullptr) {
            state.SkipWithError("executor reported a row without output");
            return false;
        }
        benchmark::DoNotOptimize(tuple);
        ++rows;
    }
    if (rows != expected_rows) {
        state.SkipWithError("executor produced an unexpected row count");
        return false;
    }
    return true;
}

inline auto rescan_outside_timing(
    benchmark::State& state,
    execution::PlanState& executor,
    execution::ExecContext& context
) -> bool {
    state.PauseTiming();
    auto rescanned = executor.rescan(context);
    state.ResumeTiming();
    if (!rescanned) {
        state.SkipWithError("executor rescan failed");
        return false;
    }
    return true;
}

}  // namespace db::benchmarks::execution_support
