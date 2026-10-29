#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/result.hpp"
#include "db/execution/limit.hpp"
#include "db/execution/operators.hpp"
#include "db/execution/scan_port.hpp"
#include "db/execution/subquery_scan.hpp"
#include "db/execution/unique.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

#include "execution_bench_support.hpp"

namespace db::benchmarks {
namespace {

namespace support = execution_support;

class GeneratedSeqPort final : public execution::SeqScanPort {
public:
    GeneratedSeqPort(execution::TupleDescriptorHandle descriptor, const std::size_t rows)
        : descriptor_{std::move(descriptor)},
          rows_{rows} {}

    auto configure(execution::SeqScanRequest request) -> base::Status override {
        static_cast<void>(request);
        return {};
    }

    auto open(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        position_ = 0U;
        return {};
    }

    auto next(execution::ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<execution::Tuple>> override {
        static_cast<void>(context);
        if (position_ >= rows_) {
            return std::optional<execution::Tuple>{};
        }
        recycled_values.clear();
        recycled_values.reserve(descriptor_->size());
        for (std::size_t column = 0U; column < descriptor_->size(); ++column) {
            recycled_values.push_back(
                types::Value::integer(static_cast<std::int64_t>(column == 0U ? position_ : column))
            );
        }
        const base::Rid rid{
            base::PageId{static_cast<std::uint32_t>(position_ / 256U + 1U)},
            base::SlotId{static_cast<std::uint16_t>(position_ % 256U)},
        };
        ++position_;
        auto tuple = execution::Tuple::create(descriptor_, std::move(recycled_values), rid);
        if (!tuple) {
            return base::fail(std::move(tuple).error());
        }
        return std::optional<execution::Tuple>{std::move(tuple).value()};
    }

    auto rescan(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        position_ = 0U;
        return {};
    }

    auto close(execution::ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        return {};
    }

private:
    execution::TupleDescriptorHandle descriptor_;
    std::size_t rows_;
    std::size_t position_{0U};
};

auto passthrough_chain(
    const planner::PlanSchema& schema,
    const std::size_t rows,
    const std::size_t depth
) -> base::Result<std::unique_ptr<execution::PlanState>> {
    auto current = support::row_source(schema, rows);
    if (!current) {
        return base::fail(std::move(current).error());
    }
    for (std::size_t level = 0U; level < depth; ++level) {
        planner::SubqueryScanPlan plan{
            .metadata = planner::PlanOperatorMetadata{.output = schema},
            .subquery = nullptr,
            .alias = "bench",
            .filter = nullptr,
        };
        auto wrapped = execution::SubqueryScanState::create(plan, std::move(current).value());
        if (!wrapped) {
            return base::fail(std::move(wrapped).error());
        }
        current = std::unique_ptr<execution::PlanState>{std::move(wrapped).value()};
    }
    return current;
}

auto projection_state(
    const planner::PlanSchema& schema,
    const std::size_t rows,
    const bool reverse_columns = false
) -> base::Result<std::unique_ptr<execution::PlanState>> {
    auto input = support::row_source(schema, rows);
    if (!input) {
        return base::fail(std::move(input).error());
    }
    planner::ResultPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .projection = {},
    };
    plan.projection.reserve(schema.columns.size());
    for (std::size_t output = 0U; output < schema.columns.size(); ++output) {
        const std::size_t column = reverse_columns ? schema.columns.size() - output - 1U : output;
        plan.projection.push_back(
            planner::PlanProjectionItem{
                .expression = support::input_column(static_cast<std::uint32_t>(column)),
                .output_name = schema.columns[output].name,
            }
        );
    }
    auto executor = execution::ResultState::create(plan, std::move(input).value());
    if (!executor) {
        return base::fail(std::move(executor).error());
    }
    return std::unique_ptr<execution::PlanState>{std::move(executor).value()};
}

auto compacting_projection_state(const std::size_t input_columns, const std::size_t rows)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    const planner::PlanSchema input_schema = support::integer_schema(input_columns);
    auto input = support::row_source(input_schema, rows);
    if (!input) {
        return base::fail(std::move(input).error());
    }
    planner::PlanSchema output_schema;
    output_schema.columns.reserve((input_columns + 1U) / 2U);
    planner::ResultPlan plan{
        .metadata = planner::PlanOperatorMetadata{},
        .input = nullptr,
        .projection = {},
    };
    plan.projection.reserve((input_columns + 1U) / 2U);
    for (std::size_t column = 0U; column < input_columns; column += 2U) {
        output_schema.columns.push_back(input_schema.columns[column]);
        plan.projection.push_back(
            planner::PlanProjectionItem{
                .expression = support::input_column(static_cast<std::uint32_t>(column)),
                .output_name = input_schema.columns[column].name,
            }
        );
    }
    plan.metadata.output = std::move(output_schema);
    auto executor = execution::ResultState::create(plan, std::move(input).value());
    if (!executor) {
        return base::fail(std::move(executor).error());
    }
    return std::unique_ptr<execution::PlanState>{std::move(executor).value()};
}

auto seq_scan_state(const planner::PlanSchema& schema, const std::size_t rows)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    auto descriptor = execution::TupleDescriptor::from_plan_schema(schema);
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    auto port = std::make_unique<GeneratedSeqPort>(std::move(descriptor).value(), rows);
    planner::SeqScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{1U},
                .name = "bench",
                .alias = std::nullopt,
                .schema = schema,
            },
        .required_columns = std::nullopt,
        .filter = nullptr,
    };
    auto executor = execution::SeqScanState::create(plan, std::move(port));
    if (!executor) {
        return base::fail(std::move(executor).error());
    }
    return std::unique_ptr<execution::PlanState>{std::move(executor).value()};
}

auto limit_state(const planner::PlanSchema& schema, const std::size_t rows)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    auto input = support::row_source(schema, rows);
    if (!input) {
        return base::fail(std::move(input).error());
    }
    planner::LimitPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .limit = support::integer_literal(static_cast<std::int64_t>(rows)),
        .offset = nullptr,
    };
    auto executor = execution::LimitState::create(plan, std::move(input).value());
    if (!executor) {
        return base::fail(std::move(executor).error());
    }
    return std::unique_ptr<execution::PlanState>{std::move(executor).value()};
}

auto unique_state(const planner::PlanSchema& schema, const std::size_t rows)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    auto input = support::row_source(schema, rows, rows);
    if (!input) {
        return base::fail(std::move(input).error());
    }
    planner::UniquePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
    };
    auto executor = execution::UniqueState::create(plan, std::move(input).value());
    if (!executor) {
        return base::fail(std::move(executor).error());
    }
    return std::unique_ptr<execution::PlanState>{std::move(executor).value()};
}

template <typename Factory>
void benchmark_rows(
    benchmark::State& state,
    Factory&& factory,
    const std::size_t expected_rows,
    const char* const initialization_error
) {
    auto executor = std::forward<Factory>(factory)();
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
        if (!support::drain(state, *executor.value(), context.value(), expected_rows)) {
            return;
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(expected_rows));
}

void source_materialization(benchmark::State& state) {
    const auto columns = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t rows_per_scan = 4096U;
    const planner::PlanSchema schema = support::integer_schema(columns);
    benchmark_rows(
        state,
        [&schema] {
            return support::row_source(schema, rows_per_scan);
        },
        rows_per_scan,
        "failed to initialize generated source"
    );
}

void volcano_passthrough_chain(benchmark::State& state) {
    const auto depth = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t column_count = 8U;
    constexpr std::size_t rows_per_scan = 4096U;
    const planner::PlanSchema schema = support::integer_schema(column_count);
    benchmark_rows(
        state,
        [&schema, depth] {
            return passthrough_chain(schema, rows_per_scan, depth);
        },
        rows_per_scan,
        "failed to initialize passthrough chain"
    );
}

void result_projection(benchmark::State& state) {
    const auto columns = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t rows_per_scan = 4096U;
    const planner::PlanSchema schema = support::integer_schema(columns);
    benchmark_rows(
        state,
        [&schema] {
            return projection_state(schema, rows_per_scan);
        },
        rows_per_scan,
        "failed to initialize Result projection"
    );
}

void result_reordered_projection(benchmark::State& state) {
    const auto columns = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t rows_per_scan = 4096U;
    const planner::PlanSchema schema = support::integer_schema(columns);
    benchmark_rows(
        state,
        [&schema] {
            return projection_state(schema, rows_per_scan, true);
        },
        rows_per_scan,
        "failed to initialize reordered Result projection"
    );
}

void result_compacting_projection(benchmark::State& state) {
    const auto columns = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t rows_per_scan = 4096U;
    benchmark_rows(
        state,
        [columns] {
            return compacting_projection_state(columns, rows_per_scan);
        },
        rows_per_scan,
        "failed to initialize compacting Result projection"
    );
}

void seq_scan_handoff(benchmark::State& state) {
    const auto columns = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t rows_per_scan = 4096U;
    const planner::PlanSchema schema = support::integer_schema(columns);
    benchmark_rows(
        state,
        [&schema] {
            return seq_scan_state(schema, rows_per_scan);
        },
        rows_per_scan,
        "failed to initialize sequential scan handoff"
    );
}

void limit_passthrough(benchmark::State& state) {
    const auto columns = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t rows_per_scan = 4096U;
    const planner::PlanSchema schema = support::integer_schema(columns);
    benchmark_rows(
        state,
        [&schema] {
            return limit_state(schema, rows_per_scan);
        },
        rows_per_scan,
        "failed to initialize LIMIT handoff"
    );
}

void unique_distinct(benchmark::State& state) {
    const auto columns = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t rows_per_scan = 4096U;
    const planner::PlanSchema schema = support::integer_schema(columns);
    benchmark_rows(
        state,
        [&schema] {
            return unique_state(schema, rows_per_scan);
        },
        rows_per_scan,
        "failed to initialize Unique handoff"
    );
}

// Google Benchmark requires these framework-owned static registration objects.
// NOLINTBEGIN(bugprone-throwing-static-initialization)
// NOLINTBEGIN(cert-err58-cpp)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(source_materialization)
    ->Name("execution/source_materialization")
    ->ArgName("columns")
    ->Args({1})
    ->Args({8})
    ->Args({64});
BENCHMARK(volcano_passthrough_chain)
    ->Name("execution/volcano_passthrough_chain")
    ->ArgName("depth")
    ->Args({1})
    ->Args({4});
BENCHMARK(result_projection)
    ->Name("execution/result_projection")
    ->ArgName("columns")
    ->Args({1})
    ->Args({8})
    ->Args({64});
BENCHMARK(result_reordered_projection)
    ->Name("execution/result_reordered_projection")
    ->ArgName("columns")
    ->Args({8})
    ->Args({64});
BENCHMARK(result_compacting_projection)
    ->Name("execution/result_compacting_projection")
    ->ArgName("input_columns")
    ->Args({8})
    ->Args({64});
BENCHMARK(seq_scan_handoff)
    ->Name("execution/seq_scan_handoff")
    ->ArgName("columns")
    ->Args({1})
    ->Args({8})
    ->Args({64});
BENCHMARK(limit_passthrough)
    ->Name("execution/limit_passthrough")
    ->ArgName("columns")
    ->Args({1})
    ->Args({64});
BENCHMARK(unique_distinct)
    ->Name("execution/unique_distinct")
    ->ArgName("columns")
    ->Args({1})
    ->Args({8});
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cert-err58-cpp)
// NOLINTEND(bugprone-throwing-static-initialization)

}  // namespace
}  // namespace db::benchmarks
