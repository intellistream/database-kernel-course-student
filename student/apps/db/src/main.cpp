#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <span>

#include "db/shell.hpp"

auto main(const int argc, char* argv[]) -> int {
    try {
        const std::span<char*> arguments{argv, static_cast<std::size_t>(argc)};
        auto options = db::shell::parse_shell_options(arguments);
        if (!options) {
            std::cerr << "ERROR: " << options.error().to_string() << '\n';
            return 2;
        }

        auto shell = db::shell::Shell::open(options.value());
        if (!shell) {
            std::cerr << "ERROR: " << shell.error().to_string() << '\n';
            return 1;
        }
        auto status = shell->run(std::cin, std::cout, std::cerr);
        if (!status) {
            std::cerr << "ERROR: " << status.error().to_string() << '\n';
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
