#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <span>

#include "server_application.hpp"

auto main(const int argc, char* argv[]) -> int {
    try {
        const std::span<char*> arguments{argv, static_cast<std::size_t>(argc)};
        auto options = db::server::parse_server_options(arguments);
        if (!options) {
            std::cerr << "ERROR: " << options.error().to_string() << '\n';
            return 2;
        }
        auto application = db::server::ServerApplication::open(options.value());
        if (!application) {
            std::cerr << "ERROR: " << application.error().to_string() << '\n';
            return 1;
        }
        auto endpoint = application->local_endpoint();
        if (!endpoint) {
            std::cerr << "ERROR: " << endpoint.error().to_string() << '\n';
            static_cast<void>(application->shutdown());
            return 1;
        }
        std::cerr << "DB TCP v3 listening on " << endpoint->to_string() << '\n';

        const auto served = application->serve_forever();
        const auto shutdown = application->shutdown();
        if (!served) {
            std::cerr << "ERROR: " << served.error().to_string() << '\n';
            return 1;
        }
        if (!shutdown) {
            std::cerr << "ERROR: " << shutdown.error().to_string() << '\n';
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
