#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "runner.hpp"

auto main(const int argc, char* argv[]) -> int {
    try {
        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        for (const char* const argument : std::span{argv, static_cast<std::size_t>(argc)}) {
            arguments.emplace_back(argument);
        }
        auto options = db::tpcc::parse_options(arguments);
        if (!options) {
            std::cerr << "ERROR: " << options.error().to_string() << '\n' << db::tpcc::usage();
            return 2;
        }
        if (options->show_help) {
            std::cout << db::tpcc::usage();
            return 0;
        }
        auto result = db::tpcc::run_benchmark(options.value(), std::cout);
        if (!result) {
            std::cerr << "ERROR: " << result.error().to_string() << '\n';
            return 1;
        }
        return 0;
    } catch (const std::exception& exception) {
        std::fputs("ERROR: unhandled exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("ERROR: unhandled non-standard exception\n", stderr);
        return 1;
    }
}
