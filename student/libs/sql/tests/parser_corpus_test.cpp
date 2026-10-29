#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/sql/binder.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/parser.hpp"
#include "db/types/data_type.hpp"

namespace db::sql {
namespace {

constexpr std::string_view kCorpusHeader{
    "id\tdisposition\tdb_ast_shape\tcontract\treference\tsql_escaped",
};
constexpr std::size_t kCorpusColumnCount = 6U;
constexpr std::size_t kExpectedRowCount = 91U;

struct CorpusRow final {
    std::string id;
    std::string disposition;
    std::string shape;
    std::string contract;
    std::string reference;
    std::string sql;
};

struct RequiredRow final {
    std::string_view id;
    std::string_view disposition;
    std::string_view shape;
};

auto has_expected_source_root(const SourceStatement& statement, const std::string_view shape)
    -> bool {
    if (shape == "script.batch.query") {
        return std::holds_alternative<SourceSelectStatement>(statement.node);
    }
    if (shape.starts_with("query.")) {
        return std::holds_alternative<SourceSelectStatement>(statement.node);
    }
    if (shape == "dml.insert_values" || shape == "dml.insert_query") {
        return std::holds_alternative<SourceInsertStatement>(statement.node);
    }
    if (shape == "dml.update") {
        return std::holds_alternative<SourceUpdateStatement>(statement.node);
    }
    if (shape == "dml.delete") {
        return std::holds_alternative<SourceDeleteStatement>(statement.node);
    }
    if (shape == "ddl.create_table") {
        return std::holds_alternative<SourceCreateTableStatement>(statement.node);
    }
    if (shape == "ddl.drop_table") {
        return std::holds_alternative<SourceDropTableStatement>(statement.node);
    }
    if (shape == "ddl.create_index") {
        return std::holds_alternative<SourceCreateIndexStatement>(statement.node);
    }
    if (shape == "ddl.drop_index") {
        return std::holds_alternative<SourceDropIndexStatement>(statement.node);
    }
    if (shape == "transaction.begin" || shape == "transaction.commit"
        || shape == "transaction.rollback") {
        return std::holds_alternative<SourceTransactionStatement>(statement.node);
    }
    if (shape == "transaction.set_isolation" || shape == "transaction.set_repeatable_read") {
        return std::holds_alternative<SourceSetTransactionStatement>(statement.node);
    }
    if (shape == "utility.copy_from") {
        return std::holds_alternative<SourceCopyFromStatement>(statement.node);
    }
    if (shape == "utility.explain") {
        return std::holds_alternative<SourceExplainStatement>(statement.node);
    }
    if (shape == "utility.show_tables") {
        return std::holds_alternative<SourceShowTablesStatement>(statement.node);
    }
    if (shape == "utility.vacuum") {
        return std::holds_alternative<SourceVacuumStatement>(statement.node);
    }
    if (shape == "utility.analyze") {
        return std::holds_alternative<SourceAnalyzeStatement>(statement.node);
    }
    if (shape == "utility.checkpoint") {
        return std::holds_alternative<SourceCheckpointStatement>(statement.node);
    }
    return false;
}

class CorpusCatalogReadPort final : public CatalogReadPort {
public:
    auto generation() const noexcept -> std::uint64_t override {
        return 1U;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<CatalogTable>> override {
        for (const std::string_view table_name :
             {"t", "a", "b", "u", "items", "CamelTable", "table", "数据"}) {
            if (name == table_name) {
                return std::optional<CatalogTable>{CatalogTable{
                    .oid = base::Oid{1U},
                    .name = std::string{table_name},
                    .columns = {
                        {.name = "id", .data_type = types::kInt64Type},
                        {.name = "v", .data_type = types::kInt64Type},
                        {.name = "k", .data_type = types::kInt64Type},
                        {.name = "tenant", .data_type = types::kInt64Type},
                        {.name = "name", .data_type = types::kCharType},
                        {.name = "a", .data_type = types::kInt64Type},
                        {.name = "b", .data_type = types::kInt64Type},
                    },
                }};
            }
        }
        return std::optional<CatalogTable>{};
    }
};

auto split_tabs(const std::string_view line) -> std::vector<std::string> {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const auto end = line.find('\t', begin);
        if (end == std::string_view::npos) {
            fields.emplace_back(line.substr(begin));
            break;
        }
        fields.emplace_back(line.substr(begin, end - begin));
        begin = end + 1U;
    }
    return fields;
}

constexpr auto valid_id(const std::string_view id) noexcept -> bool {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](const char character) constexpr {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')
               || character == '_';
    });
}

constexpr auto valid_shape(const std::string_view shape) noexcept -> bool {
    return !shape.empty()
           && std::all_of(shape.begin(), shape.end(), [](const char character) constexpr {
                  return (character >= 'a' && character <= 'z')
                         || (character >= '0' && character <= '9') || character == '.'
                         || character == '_';
              });
}

constexpr auto valid_contract(const std::string_view contract) noexcept -> bool {
    return valid_id(contract);
}

constexpr auto valid_sql_escaping(const std::string_view sql) noexcept -> bool {
    if (sql == "\\0") {
        return true;
    }
    if (sql.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < sql.size(); ++index) {
        const char character = sql.at(index);
        if (character == '\t' || character == '\r' || character == '\n') {
            return false;
        }
        if (character != '\\') {
            continue;
        }
        if (index + 1U >= sql.size()) {
            return false;
        }
        ++index;
        const char escaped = sql.at(index);
        if (escaped != '\\' && escaped != 'n' && escaped != 'r' && escaped != 't') {
            return false;
        }
    }
    return true;
}

auto unescape_sql(const std::string_view escaped) -> std::string {
    if (escaped == "\\0") {
        return {};
    }
    std::string result;
    result.reserve(escaped.size());
    for (std::size_t index = 0U; index < escaped.size(); ++index) {
        const char character = escaped.at(index);
        if (character != '\\') {
            result.push_back(character);
            continue;
        }
        ++index;
        const char escaped_character = escaped.at(index);
        switch (escaped_character) {
            case '\\':
                result.push_back('\\');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                ADD_FAILURE() << "invalid corpus escape";
                return {};
        }
    }
    return result;
}

auto read_corpus() -> std::vector<CorpusRow> {
    std::ifstream input{DB_SQL_PARSER_CORPUS_PATH};
    EXPECT_TRUE(input.is_open()) << DB_SQL_PARSER_CORPUS_PATH;
    std::string header;
    EXPECT_TRUE(std::getline(input, header));
    EXPECT_EQ(header, kCorpusHeader);

    std::vector<CorpusRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        const auto fields = split_tabs(line);
        if (fields.size() != kCorpusColumnCount) {
            ADD_FAILURE() << "invalid corpus row";
            return {};
        }
        rows.push_back(
            CorpusRow{
                .id = fields.at(0U),
                .disposition = fields.at(1U),
                .shape = fields.at(2U),
                .contract = fields.at(3U),
                .reference = fields.at(4U),
                .sql = fields.at(5U),
            }
        );
    }
    return rows;
}

TEST(ParserCorpusTest, FixesDbAstProjectionAndStableRejectBoundary) {
    std::ifstream input{DB_SQL_PARSER_CORPUS_PATH};
    ASSERT_TRUE(input.is_open()) << DB_SQL_PARSER_CORPUS_PATH;

    std::string header;
    ASSERT_TRUE(std::getline(input, header));
    EXPECT_EQ(header, kCorpusHeader);

    std::map<std::string, CorpusRow, std::less<>> rows;
    std::map<std::string, std::size_t, std::less<>> disposition_counts;
    std::string line;
    std::size_t line_number = 1U;
    while (std::getline(input, line)) {
        ++line_number;
        ASSERT_FALSE(line.empty()) << "blank corpus line " << line_number;
        const auto fields = split_tabs(line);
        ASSERT_EQ(fields.size(), kCorpusColumnCount) << "corpus line " << line_number;

        const CorpusRow row{
            .id = fields.at(0),
            .disposition = fields.at(1),
            .shape = fields.at(2),
            .contract = fields.at(3),
            .reference = fields.at(4),
            .sql = fields.at(5),
        };
        EXPECT_TRUE(valid_id(row.id)) << "corpus line " << line_number;
        EXPECT_TRUE(
            row.disposition == "accept" || row.disposition == "utility"
            || row.disposition == "unsupported" || row.disposition == "parse_error"
        ) << "corpus line "
          << line_number;
        EXPECT_TRUE(valid_shape(row.shape)) << "corpus line " << line_number;
        EXPECT_TRUE(valid_contract(row.contract)) << "corpus line " << line_number;
        EXPECT_TRUE(row.reference.starts_with("reference:")) << "corpus line " << line_number;
        EXPECT_TRUE(valid_sql_escaping(row.sql)) << "corpus line " << line_number;

        if (row.disposition == "parse_error" || row.disposition == "unsupported") {
            EXPECT_EQ(row.shape, "none") << "corpus line " << line_number;
        } else {
            EXPECT_NE(row.shape, "none") << "corpus line " << line_number;
        }

        ++disposition_counts[row.disposition];
        const auto [iterator, inserted] = rows.emplace(row.id, row);
        EXPECT_TRUE(inserted) << "duplicate corpus id " << iterator->first;
    }

    EXPECT_EQ(rows.size(), kExpectedRowCount);
    EXPECT_EQ(disposition_counts["accept"], 51U);
    EXPECT_EQ(disposition_counts["utility"], 7U);
    EXPECT_EQ(disposition_counts["unsupported"], 26U);
    EXPECT_EQ(disposition_counts["parse_error"], 7U);

    constexpr std::array k_required_rows{
        RequiredRow{"select_constant", "accept", "query.select"},
        RequiredRow{"union", "accept", "query.union"},
        RequiredRow{"identifier_case_preserved", "accept", "query.select"},
        RequiredRow{"left_join_on", "accept", "query.join.left_on"},
        RequiredRow{"insert_select", "accept", "dml.insert_query"},
        RequiredRow{"snapshot_isolation", "utility", "transaction.set_repeatable_read"},
        RequiredRow{"copy_text_options", "accept", "utility.copy_from"},
        RequiredRow{"top_level_values", "unsupported", "none"},
        RequiredRow{"explain_analyze", "unsupported", "none"},
        RequiredRow{"copy_csv", "unsupported", "none"},
        RequiredRow{"unicode_parse_error", "parse_error", "none"},
    };
    for (const RequiredRow& required : k_required_rows) {
        const auto iterator = rows.find(required.id);
        ASSERT_NE(iterator, rows.end()) << required.id;
        EXPECT_EQ(iterator->second.disposition, required.disposition) << required.id;
        EXPECT_EQ(iterator->second.shape, required.shape) << required.id;
    }
}

TEST(ParserCorpusTest, ExecutesEveryDbParserDisposition) {
    const std::vector<CorpusRow> rows = read_corpus();
    ASSERT_EQ(rows.size(), kExpectedRowCount);
    CorpusCatalogReadPort catalog;
    for (const CorpusRow& row : rows) {
        const auto parsed = parse_sql(unescape_sql(row.sql));
        if (row.disposition == "accept" || row.disposition == "utility") {
            ASSERT_TRUE(parsed) << row.id << ": " << parsed.error().to_string();
            if (row.shape == "script.empty") {
                EXPECT_TRUE(parsed->statements.empty()) << row.id;
            } else {
                ASSERT_FALSE(parsed->statements.empty()) << row.id;
                EXPECT_TRUE(has_expected_source_root(parsed->statements.front(), row.shape))
                    << row.id << ": " << row.shape;
            }
            continue;
        }

        if (row.disposition == "parse_error") {
            ASSERT_FALSE(parsed) << row.id;
            EXPECT_EQ(parsed.error().code(), base::ErrorCode::kParse)
                << row.id << ": " << parsed.error().to_string();
            continue;
        }

        if (!parsed) {
            EXPECT_EQ(parsed.error().code(), base::ErrorCode::kUnsupported)
                << row.id << ": " << parsed.error().to_string();
            continue;
        }
        const auto bound = bind_script(catalog, *parsed);
        if (bound) {
            ADD_FAILURE() << row.id << ": expected a stable unsupported rejection";
            continue;
        }
        EXPECT_EQ(bound.error().code(), base::ErrorCode::kUnsupported)
            << row.id << ": " << bound.error().to_string();
    }
}

}  // namespace
}  // namespace db::sql
