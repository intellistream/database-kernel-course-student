#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/protocol/request_dispatcher.hpp"
#include "db/protocol/response_encoder.hpp"
#include "db/protocol/session_port.hpp"
#include "db/types/value.hpp"

namespace db::benchmarks {
namespace {

class CountingSink final : public protocol::ResponseSink {
public:
    auto write_encoded_frame(const base::Bytes frame) -> base::Status override {
        bytes += frame.size();
        ++frames;
        return {};
    }

    std::size_t bytes{0U};
    std::size_t frames{0U};
};

auto query_result(const std::size_t row_count) -> base::Result<protocol::ExecutionResult> {
    std::vector<std::vector<types::Value>> rows;
    rows.reserve(row_count);
    for (std::size_t row = 0U; row < row_count; ++row) {
        rows.push_back({
            types::Value::integer(static_cast<std::int64_t>(row % 1'000U)),
            types::Value::floating(static_cast<float>(row % 100U) / 10.0F),
            types::Value::character("benchmark-row"),
        });
    }
    return protocol::ExecutionResult::query(
        {
            protocol::ColumnMetadata{
                .name = "id",
                .data_type = protocol::WireDataType::kInt32,
            },
            protocol::ColumnMetadata{
                .name = "score",
                .data_type = protocol::WireDataType::kFloat32,
            },
            protocol::ColumnMetadata{
                .name = "label",
                .data_type = protocol::WireDataType::kChar,
            },
        },
        std::move(rows)
    );
}

void encode_query_result(benchmark::State& state) {
    const auto row_count = static_cast<std::size_t>(state.range(0));
    auto execution = query_result(row_count);
    if (!execution) {
        state.SkipWithError("failed to construct protocol result benchmark");
        return;
    }
    const protocol::RequestResponse response{
        protocol::StreamResponse{.execution = std::move(execution).value()}
    };
    protocol::ResponseEncoder encoder;
    CountingSink sink;
    std::uint64_t bytes = 0U;

    for (auto iteration : state) {
        static_cast<void>(iteration);
        sink.bytes = 0U;
        sink.frames = 0U;
        auto encoded = encoder.emit(response, sink);
        if (!encoded) {
            state.SkipWithError("protocol result encoding failed");
            break;
        }
        bytes += sink.bytes;
        benchmark::DoNotOptimize(sink.frames);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(row_count));
    state.SetBytesProcessed(static_cast<std::int64_t>(bytes));
}

// Google Benchmark's required registration macro owns a process-lifetime
// object through a mutable static pointer.
// NOLINTBEGIN(cert-err58-cpp)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(encode_query_result)->Name("protocol/result_encode")->ArgName("rows")->Arg(1)->Arg(1'024);
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cert-err58-cpp)

}  // namespace
}  // namespace db::benchmarks
