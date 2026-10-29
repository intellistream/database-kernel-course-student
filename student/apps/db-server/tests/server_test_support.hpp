#pragma once

#include <optional>
#include <thread>
#include <utility>

#include "db/base/error.hpp"

#include "server_application.hpp"

namespace db::server::server_test {

// Test-only foreground lifecycle wrapper. It mirrors the application contract:
// request cooperative shutdown, join serve_forever(), then let the caller
// close the durable application explicitly.
class RunningServer final {
public:
    explicit RunningServer(ServerApplication& application)
        : application_{application},
          worker_{[this] {
              outcome_.emplace(application_.serve_forever());
          }} {}

    RunningServer(const RunningServer&) = delete;
    auto operator=(const RunningServer&) -> RunningServer& = delete;
    RunningServer(RunningServer&&) = delete;
    auto operator=(RunningServer&&) -> RunningServer& = delete;

    ~RunningServer() {
        application_.request_shutdown();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    auto stop() -> base::Status {
        application_.request_shutdown();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (!outcome_.has_value()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInternal,
                    "server worker ended without a status",
                }
            );
        }
        return std::move(outcome_).value();
    }

private:
    ServerApplication& application_;
    std::thread worker_;
    std::optional<base::Status> outcome_;
};

}  // namespace db::server::server_test
