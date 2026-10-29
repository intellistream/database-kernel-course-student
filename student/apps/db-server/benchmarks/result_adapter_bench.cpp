#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/api/database.hpp"
#include "db/api/result.hpp"
#include "db/base/result.hpp"
#include "db/protocol/session_port.hpp"
#include "db/types/value.hpp"

#include "../src/api_result_adapter.hpp"
#include "allocation_probe.hpp"

namespace db::server::benchmarks {
namespace {

using ValueRows = std::vector<std::vector<types::Value>>;

struct ResultShape final {
    std::size_t rows;
    std::size_t columns;
    std::size_t text_bytes;
};

auto copy_rows(const api::ResultSet& source) -> ValueRows {
    ValueRows rows;
    rows.reserve(source.size());
    for (const auto& row : source.rows()) {
        const auto values = row.values();
        rows.emplace_back(values.begin(), values.end());
    }
    return rows;
}

auto prepare_result(const ResultShape shape) -> base::Result<api::DispatchResult> {
    const auto [row_count, width, text_bytes] = shape;
    std::vector<api::ResultColumn> columns;
    columns.reserve(width);
    for (std::size_t column = 0U; column < width; ++column) {
        columns.push_back({
            .name = "c" + std::to_string(column),
            .data_type = text_bytes == 0U ? types::kInt64Type : types::kCharType,
        });
    }
    auto schema = api::ResultSchema::create(std::move(columns));
    if (!schema) {
        return base::fail(std::move(schema).error());
    }
    ValueRows rows;
    rows.reserve(row_count);
    for (std::size_t row = 0U; row < row_count; ++row) {
        std::vector<types::Value> values;
        values.reserve(width);
        for (std::size_t column = 0U; column < width; ++column) {
            values.push_back(
                text_bytes == 0U
                    ? types::Value::integer(static_cast<std::int64_t>((row + column) % 1'000U))
                    : types::Value::character(std::string(text_bytes, 'x'))
            );
        }
        rows.push_back(std::move(values));
    }
    auto result = api::ResultSet::create(std::move(schema).value(), std::move(rows));
    if (!result) {
        return base::fail(std::move(result).error());
    }
    api::DispatchResult dispatch;
    dispatch.statement_count = 1U;
    dispatch.row_count = row_count;
    dispatch.result_sets.push_back(std::move(result).value());
    return dispatch;
}

template <typename Prepare, typename Convert, typename Verify>
void measure_conversion(benchmark::State& state, Prepare prepare, Convert convert, Verify verify) {
    auto probe_input = prepare();
    begin_allocation_probe();
    auto probe_output = convert(std::move(probe_input));
    const auto allocations = end_allocation_probe();
    if (!verify(probe_output)) {
        state.SkipWithError("result conversion probe failed");
        return;
    }

    using Input = decltype(prepare());
    using Output = decltype(probe_output);
    const auto rows = static_cast<std::size_t>(state.range(0));
    // Amortize pause/resume overhead for small results without putting input
    // preparation or output destruction in the measured interval.
    const std::size_t batch = rows < 128U ? 32U : 1U;
    std::vector<Input> inputs;
    std::vector<Output> outputs;
    inputs.reserve(batch);
    outputs.reserve(batch);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        for (std::size_t index = 0U; index < batch; ++index) {
            inputs.push_back(prepare());
        }
        state.ResumeTiming();
        for (auto& input : inputs) {
            outputs.push_back(convert(std::move(input)));
            benchmark::DoNotOptimize(outputs.back());
        }
        state.PauseTiming();
        bool valid = true;
        for (const auto& output : outputs) {
            valid = valid && verify(output);
        }
        outputs.clear();
        inputs.clear();
        state.ResumeTiming();
        if (!valid) {
            state.SkipWithError("result conversion changed output shape or values");
            break;
        }
    }
    state.counters["allocations/result"] = static_cast<double>(allocations.calls);
    state.counters["allocated_bytes/result"] = static_cast<double>(allocations.bytes);
    state.counters["results/iteration"] = static_cast<double>(batch);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(batch * rows));
}

void result_conversion(benchmark::State& state, const int phase) {
    const auto rows = static_cast<std::size_t>(state.range(0));
    const auto width = static_cast<std::size_t>(state.range(1));
    const auto text_bytes = static_cast<std::size_t>(state.range(2));
    auto source = prepare_result({.rows = rows, .columns = width, .text_bytes = text_bytes});
    if (!source) {
        state.SkipWithError("could not prepare API result");
        return;
    }
    const auto& result = source->result_sets.front();
    auto columns = internal::map_api_columns(result.schema().columns());
    if (!columns) {
        state.SkipWithError("could not map result schema");
        return;
    }
    auto expected = protocol::ExecutionResult::query(columns.value(), copy_rows(result));
    if (!expected) {
        state.SkipWithError("could not prepare expected protocol result");
        return;
    }
    const auto verify_protocol = [&](const base::Result<protocol::ExecutionResult>& output) {
        return output && output.value() == expected.value();
    };
    if (phase == 0) {
        measure_conversion(
            state,
            [&] {
                return &result;
            },
            [](const api::ResultSet* input) {
                return copy_rows(*input);
            },
            [&](const ValueRows& output) {
                if (output.size() != rows) {
                    return false;
                }
                for (std::size_t row = 0U; row < rows; ++row) {
                    const auto values = result.rows()[row].values();
                    if (!std::equal(
                            output[row].begin(),
                            output[row].end(),
                            values.begin(),
                            values.end()
                        )) {
                        return false;
                    }
                }
                return true;
            }
        );
    } else if (phase == 1) {
        measure_conversion(
            state,
            [&] {
                return std::pair{columns.value(), copy_rows(result)};
            },
            [](auto input) {
                return protocol::ExecutionResult::query(
                    std::move(input.first),
                    std::move(input.second)
                );
            },
            verify_protocol
        );
    } else {
        measure_conversion(
            state,
            [&] {
                return source.value();
            },
            [](api::DispatchResult input) {
                return internal::map_api_dispatch_result({}, std::move(input));
            },
            verify_protocol
        );
    }
    state.counters["copied_values/result"] = phase == 0 ? static_cast<double>(rows * width) : 0.0;
    state.counters["copied_text_bytes/result"] =
        phase == 0 ? static_cast<double>(rows * width * text_bytes) : 0.0;
}

void result_shapes(benchmark::internal::Benchmark* benchmark) {
    for (const std::int64_t rows : {1, 128, 4'096}) {
        benchmark->Args({rows, 1, 0});
        benchmark->Args({rows, 64, 0});
        benchmark->Args({rows, 8, 128});
    }
}

// Google Benchmark registration owns a process-lifetime mutable pointer.
// NOLINTBEGIN(cert-err58-cpp,bugprone-throwing-static-initialization,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK_CAPTURE(result_conversion, copy_rows, 0)
    ->Name("server/result_copy_rows")
    ->ArgNames({"rows", "columns", "text_bytes"})
    ->Apply(result_shapes);
BENCHMARK_CAPTURE(result_conversion, protocol, 1)
    ->Name("server/result_protocol")
    ->ArgNames({"rows", "columns", "text_bytes"})
    ->Apply(result_shapes);
BENCHMARK_CAPTURE(result_conversion, adapter, 2)
    ->Name("server/result_adapter")
    ->ArgNames({"rows", "columns", "text_bytes"})
    ->Apply(result_shapes);
// NOLINTEND(cert-err58-cpp,bugprone-throwing-static-initialization,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::server::benchmarks
