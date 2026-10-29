#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/base/error.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/engine/database.hpp"
#include "db/engine/dispatch.hpp"
#include "db/engine/session.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::benchmarks {
namespace {

constexpr std::size_t kInputRows = 8'192U;
constexpr std::size_t kGroups = 256U;
constexpr std::size_t kLargeTableRows = 1'000'000U;
constexpr std::size_t kLargeTableGroups = 4'096U;
constexpr std::size_t kLargeTableSelectedGroup = 17U;
constexpr std::size_t kLargeTableBufferPoolPages = 512U;
constexpr std::size_t kLargeTablePayloadWidth = 24U;
constexpr std::uint64_t kBytesPerMebibyte = std::uint64_t{1'024U} * 1'024U;

struct QuerySpec final {
    std::string_view sql;
    std::size_t expected_rows;
    std::size_t measured_input_rows;
    bool parameterized;
};

struct LargeTableResults final {
    engine::DispatchResult point_lookup;
    engine::DispatchResult group_lookup;
    engine::DispatchResult count;
};

auto benchmark_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)};
}

auto write_large_table_source(const std::filesystem::path& path) -> base::Result<std::uint64_t> {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output.is_open()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kIo,
                "cannot open the large-table benchmark source",
            }
        );
    }
    for (std::size_t row = 0U; row < kLargeTableRows; ++row) {
        output << row << ',' << row % kLargeTableGroups << ",payload" << row << '\n';
    }
    output.close();
    if (output.fail()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kIo,
                "cannot write the large-table benchmark source",
            }
        );
    }

    std::error_code error;
    const auto source_size = std::filesystem::file_size(path, error);
    if (error) {
        return base::fail(
            base::Error{
                base::ErrorCode::kIo,
                "cannot inspect the large-table benchmark source",
                error,
            }
        );
    }
    if (source_size > std::numeric_limits<std::uint64_t>::max()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kOutOfRange,
                "large-table benchmark source size is not representable",
            }
        );
    }
    return static_cast<std::uint64_t>(source_size);
}

auto expect_result_shape(
    const engine::DispatchResult& result,
    const std::size_t rows,
    const std::size_t columns,
    const std::string_view operation
) -> base::Status {
    if (result.result_sets.size() != 1U || result.result_sets.front().size() != rows
        || result.result_sets.front().schema().size() != columns) {
        return base::fail(benchmark_error(
            "large-table " + std::string{operation} + " produced an unexpected result shape"
        ));
    }
    return {};
}

auto expect_single_value(
    const engine::DispatchResult& result,
    const types::Value& expected,
    const std::string_view operation
) -> base::Status {
    if (auto shape = expect_result_shape(result, 1U, 1U, operation); !shape) {
        return base::fail(std::move(shape).error());
    }
    if (result.result_sets.front().rows().front().values().front() != expected) {
        return base::fail(benchmark_error(
            "large-table " + std::string{operation} + " produced an unexpected value"
        ));
    }
    return {};
}

auto validate_large_table_results(const LargeTableResults& results) -> base::Status {
    const auto last_row = kLargeTableRows - 1U;
    if (auto status = expect_single_value(
            results.point_lookup,
            types::Value::character(
                "payload" + std::to_string(last_row),
                types::TypeModifier{static_cast<std::uint16_t>(kLargeTablePayloadWidth)}
            ),
            "point lookup"
        );
        !status) {
        return status;
    }

    const auto expected_group_rows =
        ((kLargeTableRows - 1U - kLargeTableSelectedGroup) / kLargeTableGroups) + 1U;
    if (auto status =
            expect_result_shape(results.group_lookup, expected_group_rows, 1U, "group lookup");
        !status) {
        return status;
    }
    for (const auto& row : results.group_lookup.result_sets.front().rows()) {
        const auto* identifier = row.values().front().as_integer();
        if (identifier == nullptr || *identifier < 0
            || static_cast<std::size_t>(*identifier) % kLargeTableGroups
                   != kLargeTableSelectedGroup) {
            return base::fail(benchmark_error(
                "large-table group lookup produced a row outside the selected group"
            ));
        }
    }

    return expect_single_value(
        results.count,
        types::Value::integer(static_cast<std::int64_t>(kLargeTableRows)),
        "full-table count"
    );
}

auto load_large_table(
    const engine::DatabaseConfig& config,
    const std::filesystem::path& source_path,
    const std::uint64_t source_size
) -> base::Status {
    auto created = engine::Database::create(config);
    if (!created) {
        return base::fail(std::move(created).error());
    }
    auto database = std::move(created).value();
    auto opened_session = database.new_session();
    if (!opened_session) {
        return base::fail(std::move(opened_session).error());
    }
    auto session = std::move(opened_session).value();

    for (const auto statement : {
             std::string_view{
                 "CREATE TABLE benchmark_large_rows "
                 "(id INT, grp INT, payload CHAR(24))",
             },
             std::string_view{
                 "CREATE UNIQUE INDEX benchmark_large_rows_id_idx "
                 "ON benchmark_large_rows (id)",
             },
             std::string_view{
                 "CREATE INDEX benchmark_large_rows_grp_idx ON benchmark_large_rows (grp)",
             },
         }) {
        auto dispatched = database.dispatch(session, statement);
        if (!dispatched) {
            return base::fail(std::move(dispatched).error());
        }
    }

    std::ifstream input{source_path, std::ios::binary};
    if (!input.is_open()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kIo,
                "cannot open the large-table benchmark source for LOAD",
            }
        );
    }
    auto loaded = database.load_into_empty_from(
        "COPY benchmark_large_rows FROM STDIN WITH (DELIMITER ',')",
        input,
        source_size,
        session
    );
    if (!loaded) {
        return base::fail(std::move(loaded).error());
    }
    if (loaded.value() != kLargeTableRows) {
        return base::fail(benchmark_error("large-table LOAD produced an unexpected row count"));
    }
    if (auto status = session.close(); !status) {
        return base::fail(std::move(status).error());
    }
    if (auto status = database.close(); !status) {
        return base::fail(std::move(status).error());
    }
    return {};
}

auto query_large_table(const engine::DatabaseConfig& config) -> base::Result<LargeTableResults> {
    auto reopened = engine::Database::open(config);
    if (!reopened) {
        return base::fail(std::move(reopened).error());
    }
    auto reopened_database = std::move(reopened).value();
    auto reopened_session_result = reopened_database.new_session();
    if (!reopened_session_result) {
        return base::fail(std::move(reopened_session_result).error());
    }
    auto reopened_session = std::move(reopened_session_result).value();

    const auto point_sql = "SELECT payload FROM benchmark_large_rows WHERE id = "
                           + std::to_string(kLargeTableRows - 1U);
    auto point_lookup = reopened_database.dispatch(reopened_session, point_sql);
    if (!point_lookup) {
        return base::fail(std::move(point_lookup).error());
    }
    auto group_lookup = reopened_database.dispatch(
        reopened_session,
        "SELECT id FROM benchmark_large_rows WHERE grp = 17"
    );
    if (!group_lookup) {
        return base::fail(std::move(group_lookup).error());
    }
    auto count =
        reopened_database.dispatch(reopened_session, "SELECT COUNT(*) FROM benchmark_large_rows");
    if (!count) {
        return base::fail(std::move(count).error());
    }

    LargeTableResults results{
        .point_lookup = std::move(point_lookup).value(),
        .group_lookup = std::move(group_lookup).value(),
        .count = std::move(count).value(),
    };
    if (auto status = reopened_session.close(); !status) {
        return base::fail(std::move(status).error());
    }
    if (auto status = reopened_database.close(); !status) {
        return base::fail(std::move(status).error());
    }
    return results;
}

auto run_large_table_lifecycle(
    const engine::DatabaseConfig& config,
    const std::filesystem::path& source_path,
    const std::uint64_t source_size
) -> base::Result<LargeTableResults> {
    if (auto status = load_large_table(config, source_path, source_size); !status) {
        return base::fail(std::move(status).error());
    }
    return query_large_table(config);
}

class E2eFixture final {
public:
    E2eFixture() = default;
    E2eFixture(const E2eFixture&) = delete;
    auto operator=(const E2eFixture&) -> E2eFixture& = delete;
    E2eFixture(E2eFixture&&) = delete;
    auto operator=(E2eFixture&&) -> E2eFixture& = delete;

    ~E2eFixture() {
        session_.reset();
        if (database_.has_value()) {
            static_cast<void>(database_->close());
            database_.reset();
        }
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(directory_, error));
    }

    auto initialize(benchmark::State& state, const std::size_t columns = 3U) -> bool {
        static std::atomic<std::uint64_t> next_directory{0U};
        const auto identifier = next_directory.fetch_add(1U, std::memory_order_relaxed);
        directory_ =
            std::filesystem::path{"/tmp"}
            / ("db-execution-e2e-" + std::to_string(::getpid()) + "-" + std::to_string(identifier));

        engine::DatabaseConfig config{
            .data_directory = directory_,
            .buffer_pool_pages = columns == 3U ? 512U : 4'096U,
            .recovery_page_cache_pages = 64U,
            .wal_ring_pages = 64U,
        };
        auto database = engine::Database::create(config);
        if (!database) {
            skip(state, database.error());
            return false;
        }
        database_.emplace(std::move(database).value());

        auto session = database_->new_session();
        if (!session) {
            skip(state, session.error());
            return false;
        }
        session_.emplace(std::move(session).value());

        std::string create_sql = "CREATE TABLE executor_rows (id INT, grp INT, payload INT";
        for (std::size_t column = 3U; column < columns; ++column) {
            create_sql += ", extra" + std::to_string(column) + " INT";
        }
        create_sql += ')';
        if (!dispatch_setup(state, create_sql)) {
            return false;
        }

        std::size_t next_row = 0U;
        auto copied = database_->copy_from_rows(
            "COPY executor_rows FROM STDIN",
            [&next_row, columns]() -> base::Result<std::optional<std::vector<types::Value>>> {
                if (next_row == kInputRows) {
                    return std::optional<std::vector<types::Value>>{};
                }
                const auto row = static_cast<std::int64_t>(next_row++);
                std::vector<types::Value> values;
                values.reserve(columns);
                values.push_back(types::Value::integer(row));
                values.push_back(types::Value::integer(row % static_cast<std::int64_t>(kGroups)));
                values.push_back(
                    types::Value::integer((row * 17) % static_cast<std::int64_t>(kInputRows))
                );
                for (std::size_t column = 3U; column < columns; ++column) {
                    values.push_back(types::Value::integer(row));
                }
                return std::optional<std::vector<types::Value>>{std::move(values)};
            },
            *session_
        );
        if (!copied) {
            skip(state, copied.error());
            return false;
        }
        if (copied.value() != kInputRows) {
            state.SkipWithError("COPY produced an unexpected row count");
            return false;
        }
        return dispatch_setup(
            state,
            "CREATE UNIQUE INDEX executor_rows_id_idx ON executor_rows (id)"
        );
    }

    auto database() noexcept -> engine::Database& {
        return *database_;
    }

    auto session() noexcept -> engine::Session& {
        return *session_;
    }

private:
    static void skip(benchmark::State& state, const base::Error& error) {
        const auto message = error.to_string();
        state.SkipWithError(message);
    }

    auto dispatch_setup(benchmark::State& state, const std::string_view sql) -> bool {
        auto result = database_->dispatch(*session_, sql);
        if (!result) {
            skip(state, result.error());
            return false;
        }
        return true;
    }

    std::filesystem::path directory_;
    std::optional<engine::Database> database_;
    std::optional<engine::Session> session_;
};

void prepared_query(benchmark::State& state, const QuerySpec spec) {
    E2eFixture fixture;
    if (!fixture.initialize(state)) {
        return;
    }

    const std::array<types::DataType, 1U> parameter_types{types::kInt64Type};
    const std::span<const types::DataType> types =
        spec.parameterized ? std::span<const types::DataType>{parameter_types}
                           : std::span<const types::DataType>{};
    auto prepared = fixture.database().prepare(spec.sql, types);
    if (!prepared) {
        state.SkipWithError(prepared.error().to_string());
        return;
    }

    const std::array<types::Value, 1U> parameter_values{
        types::Value::integer(static_cast<std::int64_t>(kInputRows / 2U)),
    };
    const std::span<const types::Value> values =
        spec.parameterized ? std::span<const types::Value>{parameter_values}
                           : std::span<const types::Value>{};
    auto begun = fixture.session().begin();
    if (!begun) {
        state.SkipWithError(begun.error().to_string());
        return;
    }

    auto warmup = fixture.database().execute_prepared(fixture.session(), prepared.value(), values);
    if (!warmup) {
        state.SkipWithError(warmup.error().to_string());
        return;
    }
    if (warmup->result_sets.size() != 1U
        || warmup->result_sets.front().size() != spec.expected_rows) {
        state.SkipWithError("prepared query produced an unexpected result shape");
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto executed =
            fixture.database().execute_prepared(fixture.session(), prepared.value(), values);
        if (!executed) {
            state.SkipWithError(executed.error().to_string());
            return;
        }
        benchmark::DoNotOptimize(executed->result_sets.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(spec.measured_input_rows)
    );
    state.counters["output_rows"] = static_cast<double>(spec.expected_rows);
    state.SetLabel("prepared SQL, active transaction, materialized ResultSet");
}

void prepared_index_lookup(benchmark::State& state) {
    prepared_query(
        state,
        QuerySpec{
            .sql = "SELECT payload FROM executor_rows WHERE id = $1",
            .expected_rows = 1U,
            .measured_input_rows = 1U,
            .parameterized = true,
        }
    );
}

void prepared_seq_scan(benchmark::State& state) {
    prepared_query(
        state,
        QuerySpec{
            .sql = "SELECT id, payload FROM executor_rows WHERE grp >= 0",
            .expected_rows = kInputRows,
            .measured_input_rows = kInputRows,
            .parameterized = false,
        }
    );
}

void prepared_grouped_aggregate(benchmark::State& state) {
    prepared_query(
        state,
        QuerySpec{
            .sql = "SELECT grp, COUNT(*) FROM executor_rows GROUP BY grp",
            .expected_rows = kGroups,
            .measured_input_rows = kInputRows,
            .parameterized = false,
        }
    );
}

void prepared_sort_limit(benchmark::State& state) {
    prepared_query(
        state,
        QuerySpec{
            .sql = "SELECT id FROM executor_rows ORDER BY payload, id LIMIT 100",
            .expected_rows = 100U,
            .measured_input_rows = kInputRows,
            .parameterized = false,
        }
    );
}

void prepared_dml_batch(benchmark::State& state, const std::string_view operation) {
    E2eFixture fixture;
    const auto columns = static_cast<std::size_t>(state.range(0));
    if (!fixture.initialize(state, columns)) {
        return;
    }
    // Each registered sample performs exactly one mutation of a fresh table.
    // INSERT reads a disjoint copy of the keys, UPDATE changes a non-indexed
    // column, and DELETE consumes the original rows. Setup and final commit
    // are excluded; execution still uses the real heap, index, MVCC and WAL.
    std::string sql;
    auto expected_rows = static_cast<std::int64_t>(kInputRows);
    auto expected_sum = static_cast<std::int64_t>(kInputRows * (kInputRows - 1U) / 2U);
    if (operation == "insert") {
        sql = "INSERT INTO executor_rows SELECT id + " + std::to_string(kInputRows)
              + ", grp, payload";
        for (std::size_t column = 3U; column < columns; ++column) {
            sql += ", extra" + std::to_string(column);
        }
        sql += " FROM executor_rows";
        expected_rows *= 2;
        expected_sum *= 2;
    } else if (operation == "update") {
        sql = "UPDATE executor_rows SET payload = payload + 1";
        expected_sum += static_cast<std::int64_t>(kInputRows);
    } else {
        sql = "DELETE FROM executor_rows";
        expected_rows = 0;
    }
    auto prepared = fixture.database().prepare(sql);
    if (!prepared) {
        state.SkipWithError(prepared.error().to_string());
        return;
    }
    auto begun = fixture.session().begin();
    if (!begun) {
        state.SkipWithError(begun.error().to_string());
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto result = fixture.database().execute_prepared(fixture.session(), prepared.value());
        state.PauseTiming();
        if (!result) {
            state.SkipWithError(result.error().to_string());
        } else if (result->affected_row_count != kInputRows) {
            state.SkipWithError("DML produced an unexpected affected row count");
        }
        state.ResumeTiming();
    }
    auto checked = fixture.database().dispatch(
        fixture.session(),
        "SELECT COUNT(*), SUM(payload) FROM executor_rows"
    );
    if (!checked) {
        state.SkipWithError(checked.error().to_string());
        return;
    }
    if (auto shape = expect_result_shape(*checked, 1U, 2U, "DML validation"); !shape) {
        state.SkipWithError(shape.error().to_string());
        return;
    }
    const auto values = checked->result_sets.front().rows().front().values();
    if (values[0] != types::Value::integer(expected_rows)
        || (expected_rows != 0 && values[1] != types::Value::integer(expected_sum))) {
        state.SkipWithError("DML produced unexpected stored values");
        return;
    }
    auto committed = fixture.session().commit();
    if (!committed) {
        state.SkipWithError(committed.error().to_string());
        return;
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kInputRows));
    state.counters["rows/batch"] = static_cast<double>(kInputRows);
    state.counters["columns"] = static_cast<double>(columns);
    state.SetLabel("prepared DML, fresh indexed table, real WAL; commit excluded");
}

void large_table_lifecycle(benchmark::State& state) {
    auto directory_result = benchmark_support::BenchmarkDirectory::create("execution-e2e-large");
    if (!directory_result) {
        state.SkipWithError(directory_result.error().to_string());
        return;
    }
    auto directory = std::move(directory_result).value();
    const auto source_path = directory.join("large-table.csv");
    auto source_size_result = write_large_table_source(source_path);
    if (!source_size_result) {
        state.SkipWithError(source_size_result.error().to_string());
        return;
    }
    const auto source_size = source_size_result.value();
    const auto database_path = directory.join("database");
    const engine::DatabaseConfig config{
        .data_directory = database_path,
        .buffer_pool_pages = kLargeTableBufferPoolPages,
        .recovery_page_cache_pages = 64U,
        .wal_ring_pages = 64U,
    };

    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        std::error_code cleanup_error;
        static_cast<void>(std::filesystem::remove_all(database_path, cleanup_error));
        state.ResumeTiming();
        if (cleanup_error) {
            state.SkipWithError("cannot reset the large-table benchmark database");
            return;
        }

        auto results = run_large_table_lifecycle(config, source_path, source_size);
        if (!results) {
            state.SkipWithError(results.error().to_string());
            return;
        }

        state.PauseTiming();
        auto validation = validate_large_table_results(results.value());
        cleanup_error.clear();
        static_cast<void>(std::filesystem::remove_all(database_path, cleanup_error));
        state.ResumeTiming();
        if (!validation) {
            state.SkipWithError(validation.error().to_string());
            return;
        }
        if (cleanup_error) {
            state.SkipWithError("cannot clean the large-table benchmark database");
            return;
        }
    }

    const auto iterations = state.iterations();
    state.SetItemsProcessed(iterations * static_cast<std::int64_t>(kLargeTableRows));
    state.SetBytesProcessed(iterations * static_cast<std::int64_t>(source_size));
    state.counters["buffer_pool_pages"] = static_cast<double>(kLargeTableBufferPoolPages);
    state.counters["index_count"] = 2.0;
    state.counters["source_mib"] =
        static_cast<double>(source_size) / static_cast<double>(kBytesPerMebibyte);
    state.counters["table_rows"] = static_cast<double>(kLargeTableRows);
    state.SetLabel(
        "timed create + schema + LOAD/checkpoint + close/reopen + two index lookups + COUNT + "
        "close; 2 MiB buffer pool; OS page cache not dropped"
    );
}

// Google Benchmark registers its cases before main.
// NOLINTBEGIN(cert-err58-cpp,bugprone-throwing-static-initialization)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(prepared_index_lookup)->Name("execution/e2e_prepared_index_lookup");
BENCHMARK(prepared_seq_scan)->Name("execution/e2e_prepared_seq_scan");
BENCHMARK(prepared_grouped_aggregate)->Name("execution/e2e_prepared_grouped_aggregate");
BENCHMARK(prepared_sort_limit)->Name("execution/e2e_prepared_sort_limit");
BENCHMARK_CAPTURE(prepared_dml_batch, insert, "insert")
    ->Name("execution/e2e_prepared_insert_batch")
    ->Arg(3)
    ->Arg(64)
    ->Iterations(1)
    ->UseRealTime();
BENCHMARK_CAPTURE(prepared_dml_batch, update, "update")
    ->Name("execution/e2e_prepared_update_batch")
    ->Arg(3)
    ->Arg(64)
    ->Iterations(1)
    ->UseRealTime();
BENCHMARK_CAPTURE(prepared_dml_batch, delete_rows, "delete")
    ->Name("execution/e2e_prepared_delete_batch")
    ->Arg(3)
    ->Arg(64)
    ->Iterations(1)
    ->UseRealTime();
BENCHMARK(large_table_lifecycle)
    ->Name("execution/e2e_large_table_lifecycle")
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cert-err58-cpp,bugprone-throwing-static-initialization)

}  // namespace
}  // namespace db::benchmarks
