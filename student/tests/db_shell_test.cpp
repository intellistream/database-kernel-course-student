#include <filesystem>
#include <istream>
#include <new>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/shell.hpp"
#include "db/test_support/test_directory.hpp"

namespace db::shell {
namespace {

class ThrowingInputBuffer final : public std::streambuf {
protected:
    auto underflow() -> int_type override {
        throw std::runtime_error{"shell input adapter failed"};
    }
};

class AllocationFailingOutputBuffer final : public std::streambuf {
protected:
    auto xsputn(const char* data, const std::streamsize size) -> std::streamsize override {
        static_cast<void>(data);
        static_cast<void>(size);
        throw std::bad_alloc{};
    }
};

TEST(ShellInputTest, CompletesOnlyOnUnquotedTerminator) {
    EXPECT_FALSE(statement_complete("SELECT ';'"));
    EXPECT_TRUE(statement_complete("SELECT ';';"));
    EXPECT_TRUE(statement_complete("SELECT 'it''s';"));
    EXPECT_FALSE(statement_complete("SELECT 'unfinished;"));
    EXPECT_TRUE(statement_complete("SELECT 1; -- semicolon is significant\n"));
    EXPECT_FALSE(statement_complete("SELECT 1 /* unfinished;"));
    EXPECT_TRUE(statement_complete("SELECT $$a;b$$;"));
}

TEST(ShellInputTest, RecognizesOnlyStandaloneQuitCommand) {
    EXPECT_TRUE(is_quit_command("\\q"));
    EXPECT_TRUE(is_quit_command("  \\q  \n"));
    EXPECT_FALSE(is_quit_command("SELECT '\\q';"));
}

TEST(ShellInputTest, ParsesOneOptionalNonemptyDataDirectory) {
    std::string executable{"db"};
    std::string directory{"test-data"};
    std::vector<char*> default_arguments{executable.data()};
    auto defaults = parse_shell_options(default_arguments);
    ASSERT_TRUE(defaults) << defaults.error().to_string();
    EXPECT_EQ(defaults->data_directory, "db-data");

    std::vector<char*> explicit_arguments{executable.data(), directory.data()};
    auto explicit_options = parse_shell_options(explicit_arguments);
    ASSERT_TRUE(explicit_options) << explicit_options.error().to_string();
    EXPECT_EQ(explicit_options->data_directory, directory);

    std::string empty;
    std::vector<char*> empty_argument{executable.data(), empty.data()};
    auto rejected_empty = parse_shell_options(empty_argument);
    ASSERT_FALSE(rejected_empty);
    EXPECT_EQ(rejected_empty.error().code(), base::ErrorCode::kInvalidArgument);

    std::vector<char*> missing_executable{nullptr};
    auto rejected_missing = parse_shell_options(missing_executable);
    ASSERT_FALSE(rejected_missing);
    EXPECT_EQ(rejected_missing.error().code(), base::ErrorCode::kInvalidArgument);

    std::vector<char*> extra_argument{executable.data(), directory.data(), directory.data()};
    auto rejected_extra = parse_shell_options(extra_argument);
    ASSERT_FALSE(rejected_extra);
    EXPECT_EQ(rejected_extra.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(ShellOutputTest, PrintsRawStringsAndResultRows) {
    const auto value = api::Value::character("ready");
    auto schema = api::ResultSchema::create({api::ResultColumn{
        .name = "label",
        .data_type = value.data_type(),
    }});
    ASSERT_TRUE(schema);
    auto result = api::ResultSet::create(
        std::move(schema).value(),
        std::vector<std::vector<api::Value>>{{value}}
    );
    ASSERT_TRUE(result);

    std::ostringstream output;
    print_result_set(result.value(), output);
    EXPECT_EQ(output.str(), "label\nready\n(1 rows)\n");
}

TEST(ShellIntegrationTest, RecoversAfterErrorAndQuitsWithCleanShutdown) {
    auto root = test_support::TestDirectory::create("db-shell");
    ASSERT_TRUE(root);
    auto shell = Shell::open(ShellOptions{.data_directory = root->path()});
    ASSERT_TRUE(shell) << shell.error().to_string();

    std::istringstream input{
        "CREATE TABLE shell_table (id INT);\n"
        "SHOW TABLES;\n"
        "SELECT missing_column;\n"
        "\\q\n",
    };
    std::ostringstream output;
    std::ostringstream errors;
    auto status = shell->run(input, output, errors);
    ASSERT_TRUE(status) << status.error().to_string();
    EXPECT_NE(output.str().find("DB SQL shell"), std::string::npos);
    EXPECT_NE(output.str().find("shell_table"), std::string::npos);
    EXPECT_NE(errors.str().find("ERROR:"), std::string::npos);
}

TEST(ShellIntegrationTest, MoveTransfersTheOpaqueApplicationState) {
    auto root = test_support::TestDirectory::create("db-shell-move");
    ASSERT_TRUE(root);
    auto opened = Shell::open(ShellOptions{.data_directory = root->path()});
    ASSERT_TRUE(opened) << opened.error().to_string();

    std::optional<Shell> destination;
    {
        Shell source = std::move(opened).value();
        destination.emplace(std::move(source));

        std::istringstream moved_input{"\\q\n"};
        std::ostringstream moved_output;
        std::ostringstream moved_errors;
        // Calling the documented inert source is the behavior under test.
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        auto moved_status = source.run(moved_input, moved_output, moved_errors);
        ASSERT_FALSE(moved_status);
        EXPECT_EQ(moved_status.error().code(), base::ErrorCode::kUnavailable);
    }
    ASSERT_TRUE(destination.has_value());

    std::istringstream input{"\\q\n"};
    std::ostringstream output;
    std::ostringstream errors;
    auto status = destination->run(input, output, errors);
    ASSERT_TRUE(status) << status.error().to_string();
}

TEST(ShellIntegrationTest, ContainsStreamExceptionsAndRemainsReusable) {
    auto root = test_support::TestDirectory::create("db-shell-stream-exception");
    ASSERT_TRUE(root);
    auto shell = Shell::open(ShellOptions{.data_directory = root->path()});
    ASSERT_TRUE(shell) << shell.error().to_string();

    ThrowingInputBuffer input_buffer;
    std::istream throwing_input{&input_buffer};
    throwing_input.exceptions(std::ios::badbit);
    std::ostringstream output;
    std::ostringstream errors;
    auto internal = shell->run(throwing_input, output, errors);
    ASSERT_FALSE(internal);
    EXPECT_EQ(internal.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        internal.error().message().find("shell input adapter failed"),
        std::string_view::npos
    );

    AllocationFailingOutputBuffer output_buffer;
    std::ostream allocation_failing_output{&output_buffer};
    allocation_failing_output.exceptions(std::ios::badbit);
    std::istringstream unused_input{"\\q\n"};
    auto exhausted = shell->run(unused_input, allocation_failing_output, errors);
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code(), base::ErrorCode::kResourceExhausted);

    std::istringstream recovered_input{"\\q\n"};
    std::ostringstream recovered_output;
    auto recovered = shell->run(recovered_input, recovered_output, errors);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
}

}  // namespace
}  // namespace db::shell
