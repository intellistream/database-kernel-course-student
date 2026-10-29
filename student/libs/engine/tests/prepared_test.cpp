#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/engine/database.hpp"
#include "db/engine/dispatch.hpp"
#include "db/engine/session.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/value.hpp"

namespace db::engine {
namespace {

auto test_config(const std::filesystem::path& path) -> DatabaseConfig {
    return DatabaseConfig{
        .data_directory = path,
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
}

class RecordingExecution final : public DispatchExecutionPort {
public:
    auto execute(const DispatchPlan& plan, const std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> override {
        ++calls_;
        generations_.push_back(plan.catalog_generation());
        values_.emplace_back(parameter_values.begin(), parameter_values.end());
        return DispatchExecutionResult{.row_count = 1U, .affected_row_count = 2U};
    }

    auto calls() const noexcept -> std::size_t {
        return calls_;
    }

    auto generations() const noexcept -> const std::vector<std::uint64_t>& {
        return generations_;
    }

    auto values() const noexcept -> const std::vector<std::vector<types::Value>>& {
        return values_;
    }

private:
    std::size_t calls_{0U};
    std::vector<std::uint64_t> generations_;
    std::vector<std::vector<types::Value>> values_;
};

class ResultExecution final : public DispatchExecutionPort {
public:
    auto execute(const DispatchPlan& plan, const std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> override {
        static_cast<void>(plan);
        static_cast<void>(parameter_values);
        auto schema = ResultSchema::create({ResultColumn{
            .name = "answer",
            .data_type = types::kInt64Type,
        }});
        if (!schema) {
            return base::fail(std::move(schema).error());
        }
        auto result_set = ResultSet::create(
            std::move(schema).value(),
            std::vector<std::vector<types::Value>>{{types::Value::integer(42)}}
        );
        if (!result_set) {
            return base::fail(std::move(result_set).error());
        }
        source_values_ = result_set->rows().front().values().data();
        return DispatchExecutionResult{
            .row_count = result_set->size(),
            .affected_row_count = 0U,
            .result_set = std::move(result_set).value(),
        };
    }

    auto source_values() const noexcept -> const types::Value* {
        return source_values_;
    }

private:
    const types::Value* source_values_{nullptr};
};

class ThrowingExecution final : public DispatchExecutionPort {
public:
    auto execute(const DispatchPlan& plan, const std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> override {
        static_cast<void>(plan);
        static_cast<void>(parameter_values);
        throw std::runtime_error{"prepared execution adapter failed"};
    }
};

void expect_inactive(PreparedStatement& statement) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(statement.valid());
    EXPECT_EQ(statement.kind(), DispatchStatementKind::kScript);
    EXPECT_EQ(statement.catalog_generation(), 0U);
    EXPECT_EQ(statement.parameter_count(), 0U);
    EXPECT_TRUE(statement.parameter_types().empty());
    EXPECT_TRUE(statement.result_columns().empty());
}

TEST(PreparedTest, PreparesSingleQueryAndReusesItsImmutablePlan) {
    auto root = test_support::TestDirectory::create("engine-prepared-reuse");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;
    const std::array<types::DataType, 1U> parameter_types{types::kInt64Type};

    auto prepared = database->prepare("SELECT $1", parameter_types);
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    EXPECT_TRUE(prepared->valid());
    EXPECT_EQ(prepared->kind(), DispatchStatementKind::kQuery);
    EXPECT_EQ(prepared->parameter_count(), 1U);
    EXPECT_EQ(prepared->parameter_types()[0], types::kInt64Type);
    EXPECT_NE(prepared->catalog_generation(), 0U);
    ASSERT_EQ(prepared->result_columns().size(), 1U);
    EXPECT_EQ(prepared->result_columns()[0].data_type, types::kInt64Type);
    EXPECT_FALSE(prepared->result_columns()[0].name.empty());

    const std::array<types::Value, 1U> first{types::Value::integer(7)};
    const auto first_result = database->execute_prepared(*session, *prepared, execution, first);
    ASSERT_TRUE(first_result) << first_result.error().to_string();
    EXPECT_EQ(first_result->statement_count, 1U);
    EXPECT_EQ(first_result->row_count, 1U);
    EXPECT_EQ(first_result->affected_row_count, 2U);

    const std::array<types::Value, 1U> second{types::Value::integer(9)};
    const auto second_result = database->execute_prepared(*session, *prepared, execution, second);
    ASSERT_TRUE(second_result) << second_result.error().to_string();
    EXPECT_EQ(execution.calls(), 2U);
    ASSERT_EQ(execution.values().size(), 2U);
    EXPECT_EQ(execution.values()[0][0], types::Value::integer(7));
    EXPECT_EQ(execution.values()[1][0], types::Value::integer(9));
    EXPECT_EQ(execution.generations()[0], prepared->catalog_generation());
    EXPECT_EQ(execution.generations()[1], prepared->catalog_generation());
    EXPECT_EQ(session->state(), SessionState::kIdle);

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(PreparedTest, TransfersResultSetRowStorageWithoutDeepCopy) {
    auto root = test_support::TestDirectory::create("engine-prepared-result-move");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    auto prepared = database->prepare("SELECT 42");
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    ResultExecution execution;

    auto result = database->execute_prepared(*session, *prepared, execution);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.size(), 1U);
    ASSERT_EQ(result->result_sets.front().size(), 1U);
    const auto values = result->result_sets.front().rows().front().values();
    ASSERT_EQ(values.size(), 1U);
    ASSERT_EQ(values.data(), execution.source_values());
    EXPECT_EQ(values.front(), types::Value::integer(42));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(PreparedTest, AppliesParameterTypmodsWithoutMutatingCallerValues) {
    auto root = test_support::TestDirectory::create("engine-prepared-typmod");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;
    const types::DataType char_four{types::kCharTypeId, types::TypeModifier{4}};
    const std::array<types::DataType, 1U> parameter_types{char_four};
    auto prepared = database->prepare("SELECT $1", parameter_types);
    ASSERT_TRUE(prepared) << prepared.error().to_string();

    const std::array<types::Value, 1U> input{types::Value::character("你ab")};
    const auto result = database->execute_prepared(*session, *prepared, execution, input);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(execution.values().size(), 1U);
    ASSERT_EQ(execution.values()[0].size(), 1U);
    EXPECT_EQ(execution.values()[0][0], types::Value::character("你a", char_four.modifier()));
    EXPECT_EQ(input[0], types::Value::character("你ab"));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(PreparedTest, MoveConstructionLeavesAnUnavailableInertSource) {
    auto root = test_support::TestDirectory::create("engine-prepared-move");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;
    auto prepared = database->prepare("SELECT 1");
    ASSERT_TRUE(prepared) << prepared.error().to_string();

    PreparedStatement source = std::move(prepared).value();
    PreparedStatement moved{std::move(source)};
    EXPECT_TRUE(moved.valid());

    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inactive(source);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    auto rejected = database->execute_prepared(*session, source, execution);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(execution.calls(), 0U);

    auto executed = database->execute_prepared(*session, moved, execution);
    ASSERT_TRUE(executed) << executed.error().to_string();
    EXPECT_EQ(execution.calls(), 1U);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(PreparedTest, RejectsParameterCountAndBaseTypeThenRecoversImplicitSession) {
    auto root = test_support::TestDirectory::create("engine-prepared-parameters");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;
    const std::array<types::DataType, 1U> parameter_types{types::kInt64Type};
    auto prepared = database->prepare("SELECT $1", parameter_types);
    ASSERT_TRUE(prepared) << prepared.error().to_string();

    const std::array<types::Value, 1U> wrong_type{types::Value::floating(1.0F)};
    auto rejected_type = database->execute_prepared(*session, *prepared, execution, wrong_type);
    ASSERT_FALSE(rejected_type);
    EXPECT_EQ(rejected_type.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_NE(rejected_type.error().message().find("parameter $1"), std::string_view::npos);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    const std::array<types::Value, 0U> wrong_count{};
    auto rejected_count = database->execute_prepared(*session, *prepared, execution, wrong_count);
    ASSERT_FALSE(rejected_count);
    EXPECT_EQ(rejected_count.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    const std::array<types::Value, 1U> valid{types::Value::integer(11)};
    auto recovered = database->execute_prepared(*session, *prepared, execution, valid);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    EXPECT_EQ(execution.calls(), 1U);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(PreparedTest, ConvertsExecutionPortExceptionAndLeavesSessionReusable) {
    auto root = test_support::TestDirectory::create("engine-prepared-port-exception");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    auto prepared = database->prepare("SELECT 1");
    ASSERT_TRUE(prepared) << prepared.error().to_string();

    ThrowingExecution throwing_execution;
    auto rejected = database->execute_prepared(*session, *prepared, throwing_execution);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        rejected.error().message().find("prepared execution adapter failed"),
        std::string_view::npos
    );
    EXPECT_EQ(session->state(), SessionState::kIdle);

    RecordingExecution recovered_execution;
    auto recovered = database->execute_prepared(*session, *prepared, recovered_execution);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    EXPECT_EQ(recovered_execution.calls(), 1U);

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(PreparedTest, ExplicitFailureBlocksLaterExecutionUntilRollback) {
    auto root = test_support::TestDirectory::create("engine-prepared-explicit-failure");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;
    const std::array<types::DataType, 1U> parameter_types{types::kInt64Type};
    auto prepared = database->prepare("SELECT $1", parameter_types);
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    ASSERT_TRUE(session->begin());

    const std::array<types::Value, 1U> wrong{types::Value::character("bad")};
    auto rejected = database->execute_prepared(*session, *prepared, execution, wrong);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(session->state(), SessionState::kFailedTransaction);

    const std::array<types::Value, 1U> valid{types::Value::integer(3)};
    auto blocked = database->execute_prepared(*session, *prepared, execution, valid);
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code(), base::ErrorCode::kTransactionState);
    EXPECT_EQ(execution.calls(), 0U);
    ASSERT_TRUE(session->rollback());
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(PreparedTest, RejectsScriptsUtilitiesAndInvalidPreparedTypes) {
    auto root = test_support::TestDirectory::create("engine-prepared-rejections");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();

    auto multiple = database->prepare("SELECT 1; SELECT 2");
    ASSERT_FALSE(multiple);
    EXPECT_EQ(multiple.error().code(), base::ErrorCode::kUnsupported);
    auto utility = database->prepare("BEGIN");
    ASSERT_FALSE(utility);
    EXPECT_EQ(utility.error().code(), base::ErrorCode::kUnsupported);
    const std::array<types::DataType, 1U> unknown{types::kUnknownType};
    auto invalid_type = database->prepare("SELECT $1", unknown);
    ASSERT_FALSE(invalid_type);
    EXPECT_EQ(invalid_type.error().code(), base::ErrorCode::kUnsupported);

    ASSERT_TRUE(database->close());
}

TEST(PreparedTest, RejectsStaleCatalogAndForeignSessionWithoutReplanning) {
    auto first_root = test_support::TestDirectory::create("engine-prepared-stale-first");
    ASSERT_TRUE(first_root);
    auto second_root = test_support::TestDirectory::create("engine-prepared-stale-second");
    ASSERT_TRUE(second_root);
    auto first = Database::create(test_config(first_root->path()));
    ASSERT_TRUE(first) << first.error().to_string();
    auto second = Database::create(test_config(second_root->path()));
    ASSERT_TRUE(second) << second.error().to_string();
    auto first_session = first->new_session();
    ASSERT_TRUE(first_session) << first_session.error().to_string();
    auto second_session = second->new_session();
    ASSERT_TRUE(second_session) << second_session.error().to_string();
    RecordingExecution execution;

    ASSERT_TRUE(
        first->dispatch(*first_session, "CREATE TABLE prepared_target (id INT)", execution)
    );
    auto prepared = first->prepare("SELECT id FROM prepared_target");
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    ASSERT_EQ(prepared->result_columns().size(), 1U);
    EXPECT_EQ(prepared->result_columns()[0].name, "id");
    EXPECT_EQ(prepared->result_columns()[0].data_type, types::kInt64Type);
    auto foreign = first->execute_prepared(*second_session, *prepared, execution);
    ASSERT_FALSE(foreign);
    EXPECT_EQ(foreign.error().code(), base::ErrorCode::kUnavailable);

    ASSERT_TRUE(first->dispatch(*first_session, "CREATE TABLE catalog_change (id INT)", execution));
    auto stale = first->execute_prepared(*first_session, *prepared, execution);
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(execution.calls(), 0U);
    EXPECT_EQ(first_session->state(), SessionState::kIdle);

    ASSERT_TRUE(first_session->close());
    ASSERT_TRUE(second_session->close());
    ASSERT_TRUE(first->close());
    ASSERT_TRUE(second->close());
}

}  // namespace
}  // namespace db::engine
