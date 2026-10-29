#include <array>
#include <string_view>

#include <gtest/gtest.h>

#include "config.hpp"

namespace db::tpcc {
namespace {

TEST(TpccConfigTest, DefaultsToTheDebugRankedContract) {
    const std::array<std::string_view, 1> arguments{"tpcc"};
    auto options = parse_options(arguments);
    ASSERT_TRUE(options) << options.error().to_string();
    EXPECT_EQ(options->endpoint, io::TcpEndpoint::loopback(8765U));
    EXPECT_EQ(options->profile, BenchmarkProfile::kDebug);
    EXPECT_EQ(options->seed, 1U);
    EXPECT_FALSE(options->load_directory.has_value());
    EXPECT_FALSE(options->sql_stats);

    const BenchmarkConfig config = BenchmarkConfig::for_profile(options->profile);
    EXPECT_EQ(config.clients, 32U);
    EXPECT_EQ(config.warmup, std::chrono::seconds{10});
    EXPECT_EQ(config.ranked_rounds, 3U);
    EXPECT_EQ(config.ranked_duration, std::chrono::seconds{10});
    EXPECT_EQ(config.mix, (std::array<std::uint32_t, 5>{45U, 43U, 4U, 4U, 4U}));
}

TEST(TpccConfigTest, ParsesTheExplicitLocalBenchmarkInputs) {
    const std::array<std::string_view, 10> arguments{
        "tpcc",
        "--address",
        "127.0.0.1:9988",
        "--profile",
        "full",
        "--seed",
        "4294967295",
        "--load-dir",
        "/tmp/tpcc-cache",
        "--sql-stats",
    };
    auto options = parse_options(arguments);
    ASSERT_TRUE(options) << options.error().to_string();
    EXPECT_EQ(options->endpoint.port, 9988U);
    EXPECT_EQ(options->profile, BenchmarkProfile::kFull);
    EXPECT_EQ(options->seed, 4'294'967'295U);
    ASSERT_TRUE(options->load_directory.has_value());
    EXPECT_EQ(*options->load_directory, "/tmp/tpcc-cache");
    EXPECT_TRUE(options->sql_stats);
    const BenchmarkConfig config = BenchmarkConfig::for_profile(options->profile);
    EXPECT_EQ(config.warmup, std::chrono::seconds{30});
    EXPECT_EQ(config.ranked_duration, std::chrono::seconds{150});
}

TEST(TpccConfigTest, RejectsAmbiguousOrOutOfRangeInputsAtTheCliBoundary) {
    const std::array<std::string_view, 3> zero_port{"tpcc", "--address", "127.0.0.1:0"};
    EXPECT_FALSE(parse_options(zero_port));

    const std::array<std::string_view, 3> bad_seed{"tpcc", "--seed", "4294967296"};
    EXPECT_FALSE(parse_options(bad_seed));

    const std::array<std::string_view, 5> duplicate{
        "tpcc",
        "--profile",
        "quick",
        "--profile",
        "debug",
    };
    EXPECT_FALSE(parse_options(duplicate));

    const std::array<std::string_view, 2> missing_load_path{"tpcc", "--load-dir"};
    EXPECT_FALSE(parse_options(missing_load_path));

    const std::array<std::string_view, 3> empty_load_path{"tpcc", "--load-dir", ""};
    EXPECT_FALSE(parse_options(empty_load_path));

    const std::array<std::string_view, 5> duplicate_load_path{
        "tpcc",
        "--load-dir",
        "/tmp/one",
        "--load-dir",
        "/tmp/two",
    };
    EXPECT_FALSE(parse_options(duplicate_load_path));
}

}  // namespace
}  // namespace db::tpcc
