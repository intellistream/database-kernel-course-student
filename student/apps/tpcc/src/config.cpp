#include "config.hpp"

#include <charconv>
#include <string>

#include "db/base/error.hpp"

namespace db::tpcc {
namespace {

struct SeenOptions final {
    bool address{false};
    bool profile{false};
    bool seed{false};
    bool load_directory{false};
    bool sql_stats{false};
};

auto invalid_option(std::string message) -> base::detail::Failure {
    return base::fail(base::Error{base::ErrorCode::kInvalidArgument, std::move(message)});
}

auto require_value(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    const std::string_view option
) -> base::Result<std::string_view> {
    if (index + 1U >= arguments.size()) {
        return invalid_option(std::string{option}.append(" requires a value"));
    }
    ++index;
    return arguments[index];
}

auto mark_once(bool& seen, const std::string_view option) -> base::Status {
    if (seen) {
        return invalid_option(std::string{option}.append(" was specified more than once"));
    }
    seen = true;
    return {};
}

auto parse_profile(const std::string_view value) -> base::Result<BenchmarkProfile> {
    if (value == "quick") {
        return BenchmarkProfile::kQuick;
    }
    if (value == "debug") {
        return BenchmarkProfile::kDebug;
    }
    if (value == "full") {
        return BenchmarkProfile::kFull;
    }
    return invalid_option("--profile must be quick, debug, or full");
}

auto parse_seed(const std::string_view value) -> base::Result<std::uint32_t> {
    std::uint32_t seed = 0U;
    const auto parsed = std::from_chars(value.begin(), value.end(), seed);
    if (parsed.ec != std::errc{} || parsed.ptr != value.end()) {
        return invalid_option("--seed must be an unsigned 32-bit integer");
    }
    return seed;
}

auto apply_address(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    BenchmarkOptions& options,
    SeenOptions& seen
) -> base::Status {
    auto unique = mark_once(seen.address, "--address");
    if (!unique) {
        return unique;
    }
    auto value = require_value(arguments, index, "--address");
    if (!value) {
        return base::fail(std::move(value).error());
    }
    auto endpoint = io::TcpEndpoint::parse(value.value());
    if (!endpoint) {
        return base::fail(std::move(endpoint).error());
    }
    if (endpoint->port == 0U) {
        return invalid_option("--address must use a nonzero TCP port");
    }
    options.endpoint = endpoint.value();
    return {};
}

auto apply_profile(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    BenchmarkOptions& options,
    SeenOptions& seen
) -> base::Status {
    auto unique = mark_once(seen.profile, "--profile");
    if (!unique) {
        return unique;
    }
    auto value = require_value(arguments, index, "--profile");
    if (!value) {
        return base::fail(std::move(value).error());
    }
    auto profile = parse_profile(value.value());
    if (!profile) {
        return base::fail(std::move(profile).error());
    }
    options.profile = profile.value();
    return {};
}

auto apply_seed(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    BenchmarkOptions& options,
    SeenOptions& seen
) -> base::Status {
    auto unique = mark_once(seen.seed, "--seed");
    if (!unique) {
        return unique;
    }
    auto value = require_value(arguments, index, "--seed");
    if (!value) {
        return base::fail(std::move(value).error());
    }
    auto seed = parse_seed(value.value());
    if (!seed) {
        return base::fail(std::move(seed).error());
    }
    options.seed = seed.value();
    return {};
}

auto apply_load_directory(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    BenchmarkOptions& options,
    SeenOptions& seen
) -> base::Status {
    auto unique = mark_once(seen.load_directory, "--load-dir");
    if (!unique) {
        return unique;
    }
    auto value = require_value(arguments, index, "--load-dir");
    if (!value) {
        return base::fail(std::move(value).error());
    }
    if (value->empty()) {
        return invalid_option("--load-dir must not be empty");
    }
    options.load_directory.emplace(std::string{value.value()});
    return {};
}

auto apply_option(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    BenchmarkOptions& options,
    SeenOptions& seen
) -> base::Status {
    const std::string_view argument = arguments[index];
    if (argument == "--help" || argument == "-h") {
        options.show_help = true;
        return {};
    }
    if (argument == "--sql-stats") {
        auto unique = mark_once(seen.sql_stats, argument);
        if (!unique) {
            return unique;
        }
        options.sql_stats = true;
        return {};
    }
    if (argument == "--address") {
        return apply_address(arguments, index, options, seen);
    }
    if (argument == "--profile") {
        return apply_profile(arguments, index, options, seen);
    }
    if (argument == "--seed") {
        return apply_seed(arguments, index, options, seen);
    }
    if (argument == "--load-dir") {
        return apply_load_directory(arguments, index, options, seen);
    }
    return invalid_option(std::string{"unknown tpcc option: "}.append(argument));
}

}  // namespace

auto BenchmarkConfig::for_profile(const BenchmarkProfile profile) noexcept -> BenchmarkConfig {
    BenchmarkConfig config;
    switch (profile) {
        case BenchmarkProfile::kQuick:
            config.warmup = std::chrono::seconds{1};
            config.ranked_duration = std::chrono::seconds{1};
            break;
        case BenchmarkProfile::kDebug:
            config.warmup = std::chrono::seconds{10};
            config.ranked_duration = std::chrono::seconds{10};
            break;
        case BenchmarkProfile::kFull:
            config.warmup = std::chrono::seconds{30};
            config.ranked_duration = std::chrono::seconds{150};
            break;
    }
    return config;
}

auto parse_options(const std::span<const std::string_view> arguments)
    -> base::Result<BenchmarkOptions> {
    BenchmarkOptions options;
    SeenOptions seen;
    for (std::size_t index = 1U; index < arguments.size(); ++index) {
        auto applied = apply_option(arguments, index, options, seen);
        if (!applied) {
            return base::fail(std::move(applied).error());
        }
    }
    return options;
}

auto usage() noexcept -> std::string_view {
    return "Usage: tpcc [--address IPv4:PORT] [--profile quick|debug|full] "
           "[--seed U32] [--load-dir PATH] [--sql-stats]\n"
           "\n"
           "Optionally creates and loads the canonical schema from a complete grader\n"
           "TPC-C dataset cache, then runs the ranked workload. --load-dir requires an\n"
           "empty target database. The client does not generate data, control the server,\n"
           "or perform crash/consistency audits.\n";
}

}  // namespace db::tpcc
