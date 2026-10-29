#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/sql/binder.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/parser.hpp"
#include "db/types/data_type.hpp"

namespace db::benchmarks {
namespace {

constexpr std::string_view kSimpleSelect = "SELECT id, label FROM items WHERE id = 42";
constexpr std::string_view kComplexSelect =
    "SELECT i.id, m.score + 1.5 AS adjusted "
    "FROM items AS i INNER JOIN metrics AS m ON i.id = m.id "
    "WHERE m.enabled = TRUE AND m.score >= 10 "
    "ORDER BY adjusted DESC LIMIT 25";
constexpr std::string_view kParameterizedSelect =
    "SELECT id, label FROM items WHERE id = $1 AND label = $2";
constexpr std::string_view kMultiStatement = "SELECT id FROM items WHERE id = 1;"
                                             "SELECT id FROM items WHERE id = 2;"
                                             "SELECT id FROM items WHERE id = 3;"
                                             "SELECT id FROM items WHERE id = 4;"
                                             "SELECT id FROM items WHERE id = 5;"
                                             "SELECT id FROM items WHERE id = 6;"
                                             "SELECT id FROM items WHERE id = 7;"
                                             "SELECT id FROM items WHERE id = 8";
constexpr std::string_view kUnsupportedSelect = "SELECT id FROM items WHERE label LIKE 'prefix%'";
constexpr std::string_view kWideMinimalSelect = "SELECT c63 FROM wide_items";
constexpr std::string_view kWideRepeatedSelect =
    "SELECT c63, c62, c61, c60, c59, c58, c57, c56, "
    "c55, c54, c53, c52, c51, c50, c49, c48 "
    "FROM wide_items WHERE c63 > c62 AND c61 > c60 ORDER BY c59";
constexpr std::string_view kWideDerivedSelect =
    "SELECT q.* FROM ("
    "SELECT f.* FROM wide_items AS a "
    "CROSS JOIN wide_items AS b CROSS JOIN wide_items AS c "
    "CROSS JOIN wide_items AS d CROSS JOIN wide_items AS e "
    "CROSS JOIN wide_items AS f"
    ") AS q";

class BenchmarkCatalog final : public sql::CatalogReadPort {
public:
    BenchmarkCatalog()
        : tables_{
              sql::CatalogTable{
                  .oid = base::Oid{17U},
                  .name = "items",
                  .columns =
                      {
                          {.name = "id", .data_type = types::kInt64Type},
                          {.name = "label", .data_type = types::kCharType},
                      },
              },
              sql::CatalogTable{
                  .oid = base::Oid{29U},
                  .name = "metrics",
                  .columns = {
                      {.name = "id", .data_type = types::kInt64Type},
                      {.name = "score", .data_type = types::kFloat32Type},
                      {.name = "enabled", .data_type = types::kBoolType},
                  },
              },
          } {}

    auto generation() const noexcept -> std::uint64_t override {
        return 41U;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<sql::CatalogTable>> override {
        for (const auto& table : tables_) {
            if (table.name == name) {
                return std::optional<sql::CatalogTable>{table};
            }
        }
        return std::optional<sql::CatalogTable>{};
    }

private:
    std::vector<sql::CatalogTable> tables_;
};

class WideBenchmarkCatalog final : public sql::CatalogReadPort {
public:
    WideBenchmarkCatalog() {
        table_.oid = base::Oid{37U};
        table_.name = "wide_items";
        table_.columns.reserve(64U);
        for (std::size_t index = 0U; index < 64U; ++index) {
            table_.columns.push_back(
                sql::CatalogColumn{
                    .name = "c" + std::to_string(index),
                    .data_type = types::kInt64Type,
                }
            );
        }
    }

    auto generation() const noexcept -> std::uint64_t override {
        return 41U;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<sql::CatalogTable>> override {
        if (name == table_.name) {
            return std::optional<sql::CatalogTable>{table_};
        }
        return std::optional<sql::CatalogTable>{};
    }

private:
    sql::CatalogTable table_;
};

void skip_with_error(benchmark::State& state, const base::Error& error) {
    state.SkipWithError(error.to_string());
}

void parse_simple_select(benchmark::State& state) {
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto parsed = sql::parse_sql(kSimpleSelect);
        if (!parsed) {
            skip_with_error(state, parsed.error());
            return;
        }
        benchmark::DoNotOptimize(parsed->statements.data());
    }
    state.SetItemsProcessed(state.iterations());
}

void parse_complex_select(benchmark::State& state) {
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto parsed = sql::parse_sql(kComplexSelect);
        if (!parsed) {
            skip_with_error(state, parsed.error());
            return;
        }
        benchmark::DoNotOptimize(parsed->statements.data());
    }
    state.SetItemsProcessed(state.iterations());
}

void parse_multi_statement(benchmark::State& state) {
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto parsed = sql::parse_sql(kMultiStatement);
        if (!parsed) {
            skip_with_error(state, parsed.error());
            return;
        }
        benchmark::DoNotOptimize(parsed->statements.data());
    }
    state.SetItemsProcessed(state.iterations() * 8);
}

void parse_stable_rejection(benchmark::State& state) {
    const auto probe = sql::parse_sql(kUnsupportedSelect);
    if (probe || probe.error().code() != base::ErrorCode::kUnsupported) {
        state.SkipWithError("SQL rejection benchmark did not reach the unsupported boundary");
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto parsed = sql::parse_sql(kUnsupportedSelect);
        benchmark::DoNotOptimize(parsed);
    }
    state.SetItemsProcessed(state.iterations());
}

void bind_simple_select(benchmark::State& state) {
    BenchmarkCatalog catalog;
    auto parsed = sql::parse_sql(kSimpleSelect);
    if (!parsed) {
        skip_with_error(state, parsed.error());
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto bound = sql::bind_script(catalog, *parsed);
        if (!bound) {
            skip_with_error(state, bound.error());
            return;
        }
        benchmark::DoNotOptimize(bound->statements.data());
    }
    state.SetItemsProcessed(state.iterations());
}

void bind_complex_select(benchmark::State& state) {
    BenchmarkCatalog catalog;
    auto parsed = sql::parse_sql(kComplexSelect);
    if (!parsed) {
        skip_with_error(state, parsed.error());
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto bound = sql::bind_script(catalog, *parsed);
        if (!bound) {
            skip_with_error(state, bound.error());
            return;
        }
        benchmark::DoNotOptimize(bound->statements.data());
    }
    state.SetItemsProcessed(state.iterations());
}

void bind_parameterized_select(benchmark::State& state) {
    BenchmarkCatalog catalog;
    auto parsed = sql::parse_sql(kParameterizedSelect);
    if (!parsed) {
        skip_with_error(state, parsed.error());
        return;
    }
    const std::vector parameter_types{types::kInt64Type, types::kCharType};

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto bound = sql::bind_script_with_parameters(catalog, *parsed, parameter_types);
        if (!bound) {
            skip_with_error(state, bound.error());
            return;
        }
        benchmark::DoNotOptimize(bound->statements.data());
    }
    state.SetItemsProcessed(state.iterations());
}

void bind_wide_select(benchmark::State& state) {
    WideBenchmarkCatalog catalog;
    const std::string_view sql_text =
        state.range(0) == 1 ? kWideMinimalSelect : kWideRepeatedSelect;
    auto parsed = sql::parse_sql(sql_text);
    if (!parsed) {
        skip_with_error(state, parsed.error());
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto bound = sql::bind_script(catalog, *parsed);
        if (!bound) {
            skip_with_error(state, bound.error());
            return;
        }
        benchmark::DoNotOptimize(bound->statements.data());
    }
    state.SetItemsProcessed(state.iterations());
}

void bind_wide_derived_select(benchmark::State& state) {
    WideBenchmarkCatalog catalog;
    auto parsed = sql::parse_sql(kWideDerivedSelect);
    if (!parsed) {
        skip_with_error(state, parsed.error());
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto bound = sql::bind_script(catalog, *parsed);
        if (!bound) {
            skip_with_error(state, bound.error());
            return;
        }
        benchmark::DoNotOptimize(bound->statements.data());
    }
    state.SetItemsProcessed(state.iterations());
}

void parse_and_bind_complex_select(benchmark::State& state) {
    BenchmarkCatalog catalog;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto parsed = sql::parse_sql(kComplexSelect);
        if (!parsed) {
            skip_with_error(state, parsed.error());
            return;
        }
        auto bound = sql::bind_script(catalog, *parsed);
        if (!bound) {
            skip_with_error(state, bound.error());
            return;
        }
        benchmark::DoNotOptimize(bound->statements.data());
    }
    state.SetItemsProcessed(state.iterations());
}

// Google Benchmark owns the process-lifetime registration object.
// NOLINTBEGIN(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)
BENCHMARK(parse_simple_select)->Name("sql/parse_simple_select");
BENCHMARK(parse_complex_select)->Name("sql/parse_complex_select");
BENCHMARK(parse_multi_statement)->Name("sql/parse_multi_statement");
BENCHMARK(parse_stable_rejection)->Name("sql/parse_stable_rejection");
BENCHMARK(bind_simple_select)->Name("sql/bind_simple_select");
BENCHMARK(bind_complex_select)->Name("sql/bind_complex_select");
BENCHMARK(bind_parameterized_select)->Name("sql/bind_parameterized_select");
BENCHMARK(bind_wide_select)->Arg(1)->Name("sql/bind_wide_table_minimal");
BENCHMARK(bind_wide_select)->Arg(16)->Name("sql/bind_wide_table_repeated");
BENCHMARK(bind_wide_derived_select)->Name("sql/bind_wide_derived_select");
BENCHMARK(parse_and_bind_complex_select)->Name("sql/parse_and_bind_complex_select");
// NOLINTEND(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-owning-memory)

}  // namespace
}  // namespace db::benchmarks
