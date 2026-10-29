#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/engine/database.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/value.hpp"

namespace db::engine {
namespace {

class UnionTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto root = test_support::TestDirectory::create("engine-union");
        ASSERT_TRUE(root);
        root_.emplace(std::move(*root));
        auto database = Database::create(
            DatabaseConfig{
                .data_directory = root_->path(),
                .buffer_pool_pages = 8U,
                .wal_ring_pages = 8U,
            }
        );
        ASSERT_TRUE(database) << database.error().to_string();
        database_.emplace(std::move(*database));
        auto session = database_->new_session();
        ASSERT_TRUE(session);
        session_.emplace(std::move(*session));
    }

    void TearDown() override {
        if (session_) {
            EXPECT_TRUE(session_->close());
        }
        if (database_) {
            EXPECT_TRUE(database_->close());
        }
    }

    void ok(const std::string_view sql) {
        auto result = database_->dispatch(*session_, sql);
        ASSERT_TRUE(result) << sql << ": " << result.error().to_string();
    }

    void expect_rows(
        const std::string_view sql,
        const std::vector<std::vector<types::Value>>& expected
    ) {
        auto result = database_->dispatch(*session_, sql);
        ASSERT_TRUE(result) << sql << ": " << result.error().to_string();
        ASSERT_EQ(result->result_sets.size(), 1U) << sql;
        const auto rows = result->result_sets.front().rows();
        ASSERT_EQ(rows.size(), expected.size()) << sql;
        for (std::size_t i = 0U; i < rows.size(); ++i) {
            EXPECT_EQ(
                std::vector<types::Value>(rows[i].values().begin(), rows[i].values().end()),
                expected[i]
            ) << sql
              << " row " << i;
        }
    }

    void expect_ints(const std::string_view sql, const std::vector<std::int64_t>& expected) {
        std::vector<std::vector<types::Value>> rows;
        for (const auto number : expected) {
            rows.push_back({types::Value::integer(number)});
        }
        expect_rows(sql, rows);
    }

    std::optional<test_support::TestDirectory> root_;
    std::optional<Database> database_;
    std::optional<Session> session_;
};

TEST_F(UnionTest, PreservesBagsAndDeduplicatesWholeRowsIncludingNulls) {
    ok("CREATE TABLE bag (id INT, tag INT)");
    ok("INSERT INTO bag VALUES (1, 1), (1, 2), (1, 1), (NULL, 1), (NULL, 1)");
    expect_ints(
        "SELECT COUNT(*) FROM (SELECT id, tag FROM bag UNION ALL SELECT id, tag FROM bag) AS u",
        {10}
    );
    expect_rows(
        "SELECT id, tag FROM bag UNION DISTINCT SELECT id, tag FROM bag ORDER BY id NULLS LAST, "
        "tag",
        {
            {types::Value::integer(1), types::Value::integer(1)},
            {types::Value::integer(1), types::Value::integer(2)},
            {types::Value::null(types::kInt64Type), types::Value::integer(1)},
        }
    );
    expect_ints("SELECT id FROM bag WHERE id = 9 UNION SELECT tag FROM bag WHERE tag = 9", {});
}

TEST_F(UnionTest, RespectsAssociationParenthesesAndCompleteSetWindows) {
    expect_ints("SELECT 1 AS n UNION ALL SELECT 1 UNION SELECT 2 ORDER BY n", {1, 2});
    expect_ints("SELECT 1 AS n UNION ALL (SELECT 1 UNION SELECT 2) ORDER BY n", {1, 1, 2});
    expect_ints(
        "SELECT 4 AS n UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 1 ORDER BY 1 LIMIT 2 "
        "OFFSET 1",
        {2, 3}
    );
    ok("CREATE TABLE nums (n INT)");
    ok("INSERT INTO nums VALUES (1), (2), (3), (4)");
    expect_ints(
        "(SELECT n FROM nums ORDER BY n DESC LIMIT 2 OFFSET 1) UNION ALL (SELECT n FROM nums ORDER "
        "BY n LIMIT 1) ORDER BY n",
        {1, 2, 3}
    );
    expect_ints(
        "SELECT n FROM (SELECT n FROM nums UNION SELECT n FROM nums) AS q WHERE n > 2 ORDER BY n",
        {3, 4}
    );
}

TEST_F(UnionTest, ChoosesCommonTypesBeforeDistinctAndKeepsLeftNames) {
    expect_rows(
        "SELECT 1 AS answer UNION SELECT 1.0 ORDER BY answer",
        {{types::Value::floating(1.0F)}}
    );
    auto named =
        database_->dispatch(*session_, "SELECT 1 AS first_name UNION ALL SELECT 2 AS other_name");
    ASSERT_TRUE(named) << named.error().to_string();
    EXPECT_EQ(named->result_sets.front().schema().columns()[0].name, "first_name");
    auto unnamed = database_->dispatch(*session_, "SELECT 1 UNION SELECT 2");
    ASSERT_TRUE(unnamed);
    EXPECT_EQ(unnamed->result_sets.front().schema().columns()[0].name, "column_1");
    expect_rows(
        "SELECT NULL AS n UNION SELECT 7 ORDER BY n NULLS LAST",
        {{types::Value::integer(7)}, {types::Value::null(types::kInt64Type)}}
    );
    expect_rows("SELECT NULL UNION SELECT NULL", {{types::Value::null(types::kCharType)}});
    expect_rows(
        "(SELECT NULL AS n ORDER BY n LIMIT 1) UNION SELECT 7 ORDER BY n NULLS LAST",
        {{types::Value::integer(7)}, {types::Value::null(types::kInt64Type)}}
    );
    expect_rows("SELECT '8' UNION SELECT 8", {{types::Value::integer(8)}});
    expect_rows(
        "SELECT CAST('x' AS CHAR(1)) AS c UNION ALL SELECT CAST('abcd' AS CHAR(4)) ORDER BY c",
        {
            {types::Value::character("abcd", types::TypeModifier{4})},
            {types::Value::character("x", types::TypeModifier{4})},
        }
    );
    expect_rows(
        "SELECT 'x' AS c UNION SELECT CAST('x' AS CHAR(4))",
        {{types::Value::character("x")}}
    );
    // FLOAT structural identity includes sign bits even though SQL compares the zeros equal.
    expect_ints("SELECT COUNT(*) FROM (SELECT 0.0 AS n UNION SELECT -0.0) AS z", {2});
}

TEST_F(UnionTest, SupportsAggregateInsertPreparedAndCorrelatedBranches) {
    ok("CREATE TABLE numbers (n INT)");
    ok("INSERT INTO numbers VALUES (1), (2), (3)");
    expect_ints(
        "SELECT SUM(n) AS s FROM numbers UNION ALL SELECT COUNT(*) FROM numbers ORDER BY s",
        {3, 6}
    );
    ok("CREATE TABLE copies (n INT)");
    ok("INSERT INTO copies SELECT n FROM numbers UNION ALL SELECT n FROM numbers");
    expect_ints("SELECT n FROM copies ORDER BY n", {1, 1, 2, 2, 3, 3});
    const std::array types{types::kInt64Type, types::kInt64Type};
    auto prepared = database_->prepare("SELECT $1 AS n UNION ALL SELECT $2 ORDER BY n", types);
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    const std::array values{types::Value::integer(4), types::Value::integer(2)};
    auto result = database_->execute_prepared(*session_, *prepared, values);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.front().size(), 2U);
    EXPECT_EQ(result->result_sets.front().rows()[0].values()[0], types::Value::integer(2));
    expect_ints(
        "SELECT n FROM numbers WHERE EXISTS (SELECT n FROM numbers AS b WHERE b.n = numbers.n "
        "UNION SELECT n FROM numbers AS c WHERE c.n = numbers.n) ORDER BY n",
        {1, 2, 3}
    );
    expect_ints(
        "SELECT n FROM numbers WHERE EXISTS (SELECT 1 FROM numbers AS b WHERE b.n = 1 AND b.n = "
        "numbers.n UNION ALL SELECT 1 FROM numbers AS c WHERE c.n > 1 AND c.n = numbers.n) ORDER "
        "BY n",
        {1, 2, 3}
    );
    expect_ints(
        "SELECT n FROM numbers WHERE n IN (SELECT n FROM copies UNION SELECT 7) ORDER BY n",
        {1, 2, 3}
    );
}

TEST_F(UnionTest, ConvertsOnlyAfterTheCompleteBranchHasExecuted) {
    ok("CREATE TABLE precise (v INT)");
    ok("INSERT INTO precise VALUES (16777216), (16777217)");
    expect_rows(
        "(SELECT DISTINCT v FROM precise ORDER BY v) UNION ALL SELECT CAST(0 AS FLOAT) WHERE FALSE",
        {
            {types::Value::floating(16777216.0F)},
            {types::Value::floating(16777216.0F)},
        }
    );
    expect_rows(
        "(SELECT DISTINCT v FROM precise ORDER BY v LIMIT 1 OFFSET 1) UNION ALL SELECT CAST(0 AS "
        "FLOAT) WHERE FALSE",
        {
            {types::Value::floating(16777216.0F)},
        }
    );
}

TEST_F(UnionTest, RejectsInvalidSetShapesBeforeExecution) {
    for (const auto sql : {
             "SELECT 1 UNION SELECT 1, 2",
             "SELECT TRUE UNION SELECT 1",
             "SELECT CAST('1' AS CHAR(1)) UNION SELECT 1",
             "SELECT 'bad' UNION SELECT 1",
             "SELECT 1 AS n UNION SELECT 2 ORDER BY 0",
             "SELECT 1 AS n UNION SELECT 2 ORDER BY 2",
             "SELECT 1 AS n UNION SELECT 2 AS r ORDER BY r",
             "SELECT 1 AS n UNION SELECT 2 ORDER BY n + 1",
             "SELECT 1 INTERSECT SELECT 1",
             "SELECT 1 EXCEPT SELECT 1",
         }) {
        SCOPED_TRACE(sql);
        auto result = database_->dispatch(*session_, sql);
        EXPECT_FALSE(result);
        EXPECT_EQ(session_->state(), SessionState::kIdle);
    }
    auto explained = database_->dispatch(*session_, "EXPLAIN SELECT 1 UNION SELECT 2");
    ASSERT_TRUE(explained) << explained.error().to_string();
    std::string text;
    for (const auto& row : explained->result_sets.front().rows()) {
        text.append(*row.values()[0].as_string());
    }
    EXPECT_NE(text.find("Append"), std::string::npos);
    EXPECT_NE(text.find("Unique"), std::string::npos);
}

}  // namespace
}  // namespace db::engine
