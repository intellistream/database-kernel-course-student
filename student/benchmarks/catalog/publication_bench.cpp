#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/store.hpp"
#include "db/types/data_type.hpp"

namespace db::benchmarks {
namespace {

constexpr catalog::RowSizeLimit kRowLimit{.maximum_encoded_size = 4'036U};

class MemoryPublication final : public catalog::CatalogPublicationPort {
public:
    auto publish(
        const std::filesystem::path& path,
        const base::Bytes image,
        const std::uint32_t permissions
    ) -> base::Status override {
        static_cast<void>(path);
        static_cast<void>(permissions);
        benchmark::DoNotOptimize(image.data());
        benchmark::ClobberMemory();
        return {};
    }
};

auto table_schema() -> base::Result<catalog::Schema> {
    std::vector<catalog::Column> columns;
    columns.reserve(8U);
    for (std::size_t index = 0U; index < 8U; ++index) {
        columns.push_back(
            catalog::Column{
                .name = "column_" + std::to_string(index),
                .data_type = types::kInt64Type,
            }
        );
    }
    return catalog::Schema::create(std::move(columns), kRowLimit);
}

void publish_table(benchmark::State& state) {
    const auto existing_tables = static_cast<std::size_t>(state.range(0));
    auto directory = benchmark_support::BenchmarkDirectory::create("catalog-publication");
    auto schema = table_schema();
    if (!directory || !schema) {
        state.SkipWithError("failed to initialize catalog publication benchmark");
        return;
    }
    const auto path = directory->join("catalog.cat");
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        auto store = catalog::CatalogStore::create(
            path,
            catalog::CatalogStoreOptions{
                .decode = catalog::CatalogDecodeOptions{.row_size_limit = kRowLimit},
            },
            std::make_unique<MemoryPublication>()
        );
        if (!store) {
            state.SkipWithError("failed to create in-memory catalog store");
            break;
        }
        for (std::size_t index = 0U; index < existing_tables; ++index) {
            auto created = store->create_table(
                catalog::CreateTableRequest{
                    .name = "table_" + std::to_string(index),
                    .schema = schema.value(),
                }
            );
            if (!created) {
                state.SkipWithError("failed to populate catalog publication benchmark");
                break;
            }
        }
        state.ResumeTiming();
        auto created = store->create_table(
            catalog::CreateTableRequest{
                .name = "measured_table",
                .schema = schema.value(),
            }
        );
        if (!created) {
            state.SkipWithError("catalog publication benchmark mutation failed");
            break;
        }
        benchmark::DoNotOptimize(created->value().oid().value());
    }
    state.SetItemsProcessed(state.iterations());
}

// Google Benchmark owns the process-lifetime registration object.
// NOLINTBEGIN(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(publish_table)->Name("catalog/publish_table")->ArgName("existing_tables")->Arg(64);
// NOLINTEND(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks
