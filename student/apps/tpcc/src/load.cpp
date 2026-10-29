#include "load.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/protocol/rmdb_client.hpp"

#include "load_contract.hpp"

namespace db::tpcc {
namespace {

using Clock = std::chrono::steady_clock;

inline constexpr base::ExceptionBoundaryPolicy kLoadBoundary{
    .component = "tpcc.load",
    .allocation_message = "TPC-C loader could not allocate bounded client state",
    .exception_prefix = "unexpected exception escaped the TPC-C load phase: ",
    .unknown_exception_message = "unknown exception escaped the TPC-C load phase",
};

auto load_error(std::string message) -> base::detail::Failure {
    return base::fail(
        base::Error{base::ErrorCode::kExecution, std::move(message)}
            .with_context("tpcc.load", "execute load phase")
    );
}

auto require_time(const Clock::time_point deadline, const std::string_view operation)
    -> base::Status {
    if (Clock::now() >= deadline) {
        return load_error("TPC-C load deadline expired before " + std::string{operation});
    }
    return {};
}

auto remaining_timeout(const Clock::time_point deadline) -> std::chrono::milliseconds {
    auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    if (timeout <= std::chrono::milliseconds::zero()) {
        return std::chrono::milliseconds{1};
    }
    return timeout;
}

auto connect_loader(const BenchmarkOptions& options, const Clock::time_point deadline)
    -> base::Result<protocol::RmdbClient> {
    auto time = require_time(deadline, "loader connection");
    if (!time) {
        return base::fail(std::move(time).error());
    }
    return protocol::RmdbClient::connect(
        options.endpoint,
        protocol::RmdbClientOptions{
            .io_timeout = remaining_timeout(deadline),
            .maximum_response_bytes = protocol::kMaximumRmdbFramePayloadSize,
        }
    );
}

auto execute(
    protocol::RmdbClient& client,
    const std::string_view sql,
    const Clock::time_point deadline,
    const std::string_view operation
) -> base::Result<protocol::RmdbResponse> {
    auto time = require_time(deadline, operation);
    if (!time) {
        return base::fail(std::move(time).error());
    }
    auto response = client.execute(sql);
    if (!response) {
        return base::fail(
            std::move(response).error().with_context("tpcc.load", std::string{operation})
        );
    }
    auto completion_time = require_time(deadline, operation);
    if (!completion_time) {
        return base::fail(std::move(completion_time).error());
    }
    return response;
}

auto execute_command(
    protocol::RmdbClient& client,
    const std::string_view sql,
    const Clock::time_point deadline,
    const std::string_view operation
) -> base::Status {
    auto response = execute(client, sql, deadline, operation);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (response->terminal.kind != protocol::RmdbTerminalKind::kCommandOk) {
        return load_error(
            "TPC-C " + std::string{operation} + " failed: " + response->terminal.diagnostic
        );
    }
    return {};
}

auto create_schema(protocol::RmdbClient& client, const Clock::time_point deadline) -> base::Status {
    for (const LoadTableContract& table : kLoadTables) {
        auto created = execute_command(
            client,
            table.create_sql,
            deadline,
            "create table " + std::string{table.name}
        );
        if (!created) {
            return created;
        }
    }
    for (const LoadIndexContract& index : kLoadIndexes) {
        auto created = execute_command(
            client,
            index.create_sql,
            deadline,
            "create index " + std::string{index.name}
        );
        if (!created) {
            return created;
        }
    }
    return {};
}

auto load_tables(
    protocol::RmdbClient& client,
    const LoadDatasetSource& source,
    const Clock::time_point deadline,
    std::ostream& output
) -> base::Status {
    for (const LoadTableSource& table : source.tables) {
        const std::string sql =
            "load " + table.csv_path.string() + " into " + std::string{table.table} + ";";
        auto loaded =
            execute_command(client, sql, deadline, "load table " + std::string{table.table});
        if (!loaded) {
            return loaded;
        }
        output << "[load] TPC-C table=" << table.table << " rows=" << table.row_count << '\n';
        output.flush();
    }
    return {};
}

auto read_count(
    protocol::RmdbClient& client,
    const LoadTableSource& table,
    const Clock::time_point deadline
) -> base::Result<std::uint64_t> {
    const std::string operation = "validate table " + std::string{table.table};
    const std::string sql = "select COUNT(*) from " + std::string{table.table} + ";";
    auto response = execute(client, sql, deadline, operation);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (response->terminal.kind != protocol::RmdbTerminalKind::kResultEnd
        || response->rows.size() != 1U || response->rows.front().size() != 1U) {
        return load_error(
            "TPC-C cardinality query returned a malformed result for " + std::string{table.table}
        );
    }
    const std::int64_t* const value = response->rows.front().front().as_integer();
    if (value == nullptr || *value < 0) {
        return load_error(
            "TPC-C cardinality query returned a non-count value for " + std::string{table.table}
        );
    }
    return static_cast<std::uint64_t>(*value);
}

auto validate_cardinalities(
    protocol::RmdbClient& client,
    const LoadDatasetSource& source,
    const Clock::time_point deadline
) -> base::Status {
    for (const LoadTableSource& table : source.tables) {
        auto actual = read_count(client, table, deadline);
        if (!actual) {
            return base::fail(std::move(actual).error());
        }
        if (actual.value() != table.row_count) {
            return load_error(
                "TPC-C loaded cardinality mismatch for " + std::string{table.table} + ": expected "
                + std::to_string(table.row_count) + ", got " + std::to_string(actual.value())
            );
        }
    }
    return {};
}

auto run_load_phase_impl(
    const BenchmarkOptions& options,
    const BenchmarkConfig& config,
    const Clock::time_point task_deadline,
    std::ostream& output
) -> base::Status {
    if (!options.load_directory.has_value()) {
        return {};
    }
    const Clock::time_point load_deadline =
        std::min(task_deadline, Clock::now() + config.load_timeout);
    output << "[stage] TPC-C inspect/create/load dataset=" << options.load_directory->string()
           << '\n';
    output.flush();

    auto source = inspect_load_dataset(*options.load_directory, config.scale);
    if (!source) {
        return base::fail(std::move(source).error());
    }
    auto time = require_time(load_deadline, "schema creation");
    if (!time) {
        return base::fail(std::move(time).error());
    }
    auto client = connect_loader(options, load_deadline);
    if (!client) {
        return base::fail(std::move(client).error());
    }
    auto schema = create_schema(*client, load_deadline);
    if (!schema) {
        return base::fail(std::move(schema).error());
    }
    auto loaded = load_tables(*client, *source, load_deadline, output);
    if (!loaded) {
        return base::fail(std::move(loaded).error());
    }
    auto cardinalities = validate_cardinalities(*client, *source, load_deadline);
    if (!cardinalities) {
        return base::fail(std::move(cardinalities).error());
    }
    auto closed = client->close();
    if (!closed) {
        return base::fail(std::move(closed).error());
    }
    output << "[pass] TPC-C load tables=" << source->tables.size()
           << " rows=" << source->total_rows() << '\n';
    return {};
}

}  // namespace

auto run_load_phase(
    const BenchmarkOptions& options,
    const BenchmarkConfig& config,
    const std::chrono::steady_clock::time_point task_deadline,
    std::ostream& output
) -> base::Status {
    return base::invoke_exception_boundary(kLoadBoundary, "run load phase", [&] {
        return run_load_phase_impl(options, config, task_deadline, output);
    });
}

}  // namespace db::tpcc
