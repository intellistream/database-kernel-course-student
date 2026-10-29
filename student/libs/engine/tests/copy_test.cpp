#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <new>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/engine/database.hpp"
#include "db/engine/dispatch.hpp"
#include "db/engine/session.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/value.hpp"
#include "db/wal/reader.hpp"

namespace db::engine {
namespace {

auto test_config(const std::filesystem::path& path) -> DatabaseConfig {
    return DatabaseConfig{
        .data_directory = path,
        .buffer_pool_pages = 8U,
        .wal_ring_pages = 8U,
    };
}

class NoopExecution final : public DispatchExecutionPort {
public:
    auto execute(const DispatchPlan& plan, const std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> override {
        static_cast<void>(plan);
        static_cast<void>(parameter_values);
        return DispatchExecutionResult{};
    }
};

auto wal_record_count(const std::filesystem::path& path, const std::uint64_t system_identifier)
    -> base::Result<std::size_t> {
    auto reader = wal::WalReader::open(
        path / "wal",
        wal::WalReaderOptions{
            .system_identifier = system_identifier,
            .timeline = 1U,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    if (!reader) {
        return base::fail(std::move(reader).error());
    }
    std::size_t count = 0U;
    for (;;) {
        auto next = reader->read_next();
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next->has_value()) {
            return count;
        }
        ++count;
    }
}

TEST(CopyTest, StreamsTextRowsAndStopsAtCopyTerminator) {
    auto root = test_support::TestDirectory::create("engine-copy-text");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    auto created =
        database->dispatch(*session, "CREATE TABLE copy_text (id INT, label CHAR(32))", execution);
    ASSERT_TRUE(created) << created.error().to_string();

    std::istringstream input{"1|left\\|right\n2|\\N\n\\.\n3|ignored\n"};
    auto copied = database->copy_from(
        "COPY copy_text FROM STDIN WITH (DELIMITER '|', NULL '\\N')",
        input,
        *session
    );
    ASSERT_TRUE(copied) << copied.error().to_string();
    EXPECT_EQ(copied.value(), 2U);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, TextRowsPreserveEmptyTrailingFieldsAndCrLf) {
    auto root = test_support::TestDirectory::create("engine-copy-empty-fields");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(database->dispatch(
        *session,
        "CREATE TABLE copy_empty (id INT, middle CHAR(8), tail CHAR(8))",
        execution
    ));

    std::istringstream input{"1,,tail\r\n2,end,\r\n"};
    auto copied =
        database->copy_from("COPY copy_empty FROM STDIN WITH (DELIMITER ',')", input, *session);
    ASSERT_TRUE(copied) << copied.error().to_string();
    EXPECT_EQ(copied.value(), 2U);

    auto found = database->dispatch(*session, "SELECT middle, tail FROM copy_empty WHERE id = 2");
    ASSERT_TRUE(found) << found.error().to_string();
    ASSERT_EQ(found->result_sets.size(), 1U);
    ASSERT_EQ(found->result_sets.front().size(), 1U);
    const auto& values = found->result_sets.front().rows().front().values();
    ASSERT_EQ(values.size(), 2U);
    EXPECT_EQ(values[0], types::Value::character("end", types::TypeModifier{8}));
    EXPECT_EQ(values[1], types::Value::character("", types::TypeModifier{8}));

    std::istringstream malformed{"3,only\n"};
    auto rejected =
        database->copy_from("COPY copy_empty FROM STDIN WITH (DELIMITER ',')", malformed, *session);
    ASSERT_FALSE(rejected);
    EXPECT_NE(rejected.error().message().find("has 2 field(s), expected 3"), std::string::npos);

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, LoadIntoEmptyBuildsRootlessIndexesAfterBatchedHeapInsertion) {
    constexpr std::size_t k_row_count = 2'049U;
    auto root = test_support::TestDirectory::create("engine-copy-load-bulk");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(
        database->dispatch(*session, "CREATE TABLE load_bulk (id INT, label CHAR(16))", execution)
    );
    ASSERT_TRUE(database->dispatch(
        *session,
        "CREATE UNIQUE INDEX load_bulk_pk ON load_bulk (id)",
        execution
    ));
    ASSERT_TRUE(
        database->dispatch(*session, "CREATE INDEX load_bulk_label ON load_bulk (label)", execution)
    );
    ASSERT_TRUE(database->dispatch(*session, "CHECKPOINT", execution));
    auto records_before = wal_record_count(root->path(), database->system_identifier());
    ASSERT_TRUE(records_before) << records_before.error().to_string();

    std::string text;
    text.reserve(k_row_count * 16U);
    for (std::size_t index = 0U; index < k_row_count; ++index) {
        text.append(std::to_string(index));
        text.append(",label");
        text.append(std::to_string(index));
        text.push_back('\n');
    }
    std::istringstream input{text};
    auto loaded = database->load_into_empty_from(
        "COPY load_bulk FROM STDIN WITH (DELIMITER ',')",
        input,
        text.size(),
        *session
    );
    ASSERT_TRUE(loaded) << loaded.error().to_string();
    EXPECT_EQ(loaded.value(), k_row_count);
    auto records_after = wal_record_count(root->path(), database->system_identifier());
    ASSERT_TRUE(records_after) << records_after.error().to_string();
    // LOAD owns exactly one recovery checkpoint; heap, B+Tree, and transaction
    // mutations must not append per-row WAL records.
    EXPECT_EQ(records_after.value(), records_before.value() + 2U);

    auto found = database->dispatch(*session, "SELECT label FROM load_bulk WHERE id = 2048");
    ASSERT_TRUE(found) << found.error().to_string();
    ASSERT_EQ(found->result_sets.size(), 1U);
    ASSERT_EQ(found->result_sets.front().size(), 1U);
    EXPECT_EQ(
        found->result_sets.front().rows().front().values().front(),
        types::Value::character("label2048", types::TypeModifier{16})
    );
    auto count = database->dispatch(*session, "SELECT COUNT(*) FROM load_bulk");
    ASSERT_TRUE(count) << count.error().to_string();
    ASSERT_EQ(count->result_sets.size(), 1U);
    ASSERT_EQ(count->result_sets.front().size(), 1U);
    EXPECT_EQ(
        count->result_sets.front().rows().front().values().front(),
        types::Value::integer(2049)
    );

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());

    auto reopened = Database::open(test_config(root->path()));
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto reopened_session = reopened->new_session();
    ASSERT_TRUE(reopened_session) << reopened_session.error().to_string();
    auto reopened_found =
        reopened->dispatch(*reopened_session, "SELECT label FROM load_bulk WHERE id = 2048");
    ASSERT_TRUE(reopened_found) << reopened_found.error().to_string();
    ASSERT_EQ(reopened_found->result_sets.size(), 1U);
    ASSERT_EQ(reopened_found->result_sets.front().size(), 1U);
    EXPECT_EQ(
        reopened_found->result_sets.front().rows().front().values().front(),
        types::Value::character("label2048", types::TypeModifier{16})
    );
    ASSERT_TRUE(reopened_session->close());
    ASSERT_TRUE(reopened->close());
}

TEST(CopyTest, LoadIntoEmptyDefersUniqueValidationUntilTheWholeIndexBuild) {
    auto root = test_support::TestDirectory::create("engine-copy-load-duplicate");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE load_duplicate (id INT)", execution));
    ASSERT_TRUE(database->dispatch(
        *session,
        "CREATE UNIQUE INDEX load_duplicate_pk ON load_duplicate (id)",
        execution
    ));
    ASSERT_TRUE(session->begin());

    const std::string text{"1\n2\n1\n"};
    std::istringstream input{text};
    auto loaded =
        database
            ->load_into_empty_from("COPY load_duplicate FROM STDIN", input, text.size(), *session);
    ASSERT_FALSE(loaded);
    EXPECT_NE(loaded.error().message().find("duplicate key"), std::string::npos)
        << loaded.error().to_string();
    EXPECT_EQ(session->state(), SessionState::kFailedTransaction);
    ASSERT_TRUE(session->rollback());

    auto count = database->dispatch(*session, "SELECT COUNT(*) FROM load_duplicate");
    ASSERT_TRUE(count) << count.error().to_string();
    ASSERT_EQ(count->result_sets.size(), 1U);
    ASSERT_EQ(count->result_sets.front().size(), 1U);
    EXPECT_EQ(count->result_sets.front().rows().front().values().front(), types::Value::integer(0));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, LoadIntoEmptyRejectsNonemptyHeapInsteadOfFallingBackToWal) {
    auto root = test_support::TestDirectory::create("engine-copy-load-nonempty");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE load_nonempty (id INT)", execution));

    std::istringstream copied_input{"1\n"};
    ASSERT_TRUE(database->copy_from("COPY load_nonempty FROM STDIN", copied_input, *session));
    std::istringstream load_input{"2\n"};
    auto loaded =
        database->load_into_empty_from("COPY load_nonempty FROM STDIN", load_input, 2U, *session);
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_NE(loaded.error().message().find("physically empty"), std::string::npos);

    auto count = database->dispatch(*session, "SELECT COUNT(*) FROM load_nonempty");
    ASSERT_TRUE(count) << count.error().to_string();
    ASSERT_EQ(count->result_sets.size(), 1U);
    ASSERT_EQ(count->result_sets.front().size(), 1U);
    EXPECT_EQ(count->result_sets.front().rows().front().values().front(), types::Value::integer(1));
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, OrdinaryDispatchKeepsCopyOutsideThePlannerBoundary) {
    auto root = test_support::TestDirectory::create("engine-copy-dispatch-boundary");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE copy_boundary (id INT)", execution));

    auto rejected = database->dispatch(*session, "COPY copy_boundary FROM STDIN", execution);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_NE(rejected.error().message().find("streaming API"), std::string::npos);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, TypedRowsApplyColumnProjectionAndTypmods) {
    auto root = test_support::TestDirectory::create("engine-copy-typed");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(
        database->dispatch(*session, "CREATE TABLE copy_typed (id INT, label CHAR(4))", execution)
    );

    std::vector<std::vector<types::Value>> rows{
        {types::Value::character("long-value"), types::Value::integer(1)},
        {types::Value::character("beta"), types::Value::integer(2)},
    };
    std::size_t next = 0U;
    auto copied = database->copy_from_rows(
        "COPY copy_typed (label, id) FROM STDIN",
        [&rows, &next]() -> base::Result<std::optional<std::vector<types::Value>>> {
            if (next == rows.size()) {
                return std::optional<std::vector<types::Value>>{};
            }
            return std::optional<std::vector<types::Value>>{std::move(rows.at(next++))};
        },
        *session
    );
    ASSERT_TRUE(copied) << copied.error().to_string();
    EXPECT_EQ(copied.value(), 2U);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, TypeErrorFailsImplicitCommandAndLeavesNoOpenCommand) {
    auto root = test_support::TestDirectory::create("engine-copy-type-error");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE copy_error (id INT)", execution));

    std::vector<std::vector<types::Value>> rows{
        {types::Value::integer(1)},
        {types::Value::character("wrong")},
    };
    std::size_t next = 0U;
    auto copied = database->copy_from_rows(
        "COPY copy_error FROM STDIN",
        [&rows, &next]() -> base::Result<std::optional<std::vector<types::Value>>> {
            if (next == rows.size()) {
                return std::optional<std::vector<types::Value>>{};
            }
            return std::optional<std::vector<types::Value>>{std::move(rows.at(next++))};
        },
        *session
    );
    ASSERT_FALSE(copied);
    EXPECT_EQ(copied.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(session->state(), SessionState::kIdle);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, ConvertsTypedReaderExceptionsAndLeavesSessionReusable) {
    auto root = test_support::TestDirectory::create("engine-copy-reader-exception");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE copy_reader_error (id INT)", execution));

    auto internal = database->copy_from_rows(
        "COPY copy_reader_error FROM STDIN",
        []() -> base::Result<std::optional<std::vector<types::Value>>> {
            throw std::runtime_error{"typed reader failed"};
        },
        *session
    );
    ASSERT_FALSE(internal);
    EXPECT_EQ(internal.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(internal.error().message().find("typed reader failed"), std::string_view::npos);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    auto exhausted = database->copy_from_rows(
        "COPY copy_reader_error FROM STDIN",
        []() -> base::Result<std::optional<std::vector<types::Value>>> {
            throw std::bad_alloc{};
        },
        *session
    );
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    bool emitted = false;
    auto recovered = database->copy_from_rows(
        "COPY copy_reader_error FROM STDIN",
        [&emitted]() -> base::Result<std::optional<std::vector<types::Value>>> {
            if (emitted) {
                return std::optional<std::vector<types::Value>>{};
            }
            emitted = true;
            return std::optional<std::vector<types::Value>>{
                std::vector<types::Value>{types::Value::integer(7)}
            };
        },
        *session
    );
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    EXPECT_EQ(recovered.value(), 1U);

    auto count = database->dispatch(*session, "SELECT COUNT(*) FROM copy_reader_error");
    ASSERT_TRUE(count) << count.error().to_string();
    ASSERT_EQ(count->result_sets.size(), 1U);
    ASSERT_EQ(count->result_sets.front().size(), 1U);
    EXPECT_EQ(count->result_sets.front().rows().front().values().front(), types::Value::integer(1));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, UniqueIndexErrorMarksExplicitTransactionFailedUntilRollback) {
    auto root = test_support::TestDirectory::create("engine-copy-unique");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE copy_unique (id INT)", execution));
    ASSERT_TRUE(database->dispatch(
        *session,
        "CREATE UNIQUE INDEX copy_unique_id ON copy_unique (id)",
        execution
    ));
    ASSERT_TRUE(session->begin());

    std::vector<std::vector<types::Value>> rows{
        {types::Value::integer(1)},
        {types::Value::integer(1)},
    };
    std::size_t next = 0U;
    auto copied = database->copy_from_rows(
        "COPY copy_unique FROM STDIN",
        [&rows, &next]() -> base::Result<std::optional<std::vector<types::Value>>> {
            if (next == rows.size()) {
                return std::optional<std::vector<types::Value>>{};
            }
            return std::optional<std::vector<types::Value>>{std::move(rows.at(next++))};
        },
        *session
    );
    ASSERT_FALSE(copied);
    EXPECT_NE(copied.error().message().find("duplicate key"), std::string::npos);
    EXPECT_EQ(session->state(), SessionState::kFailedTransaction);
    ASSERT_TRUE(session->rollback());
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(CopyTest, LaterTypedRowErrorAbortsAnEarlierFlushedBatch) {
    auto root = test_support::TestDirectory::create("engine-copy-batch-rollback");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    NoopExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE copy_batch (id INT)", execution));

    std::size_t next = 0U;
    auto copied = database->copy_from_rows(
        "COPY copy_batch FROM STDIN",
        [&next]() -> base::Result<std::optional<std::vector<types::Value>>> {
            if (next == 1'000U) {
                return std::optional<std::vector<types::Value>>{
                    std::vector<types::Value>{types::Value::character("wrong")}
                };
            }
            if (next > 1'000U) {
                return std::optional<std::vector<types::Value>>{};
            }
            return std::optional<std::vector<types::Value>>{
                std::vector<types::Value>{types::Value::integer(static_cast<std::int64_t>(next++))}
            };
        },
        *session
    );
    ASSERT_FALSE(copied);
    EXPECT_NE(copied.error().message().find("expected int"), std::string::npos);
    EXPECT_EQ(session->state(), SessionState::kIdle);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

}  // namespace
}  // namespace db::engine
