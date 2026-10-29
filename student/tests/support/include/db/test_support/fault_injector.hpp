#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::test_support {

class FaultInjector final {
public:
    auto arm(std::string point, const std::size_t fail_on_invocation, db::base::Error error)
        -> db::base::Status {
        if (point.empty() || fail_on_invocation == 0) {
            return db::base::fail(
                db::base::Error{
                    db::base::ErrorCode::kInvalidArgument,
                    "fault point and invocation must be valid",
                }
            );
        }
        const std::scoped_lock lock{mutex_};
        rules_.insert_or_assign(
            std::move(point),
            Rule{.fail_on_invocation = fail_on_invocation, .observed = 0, .error = std::move(error)}
        );
        return {};
    }

    auto check(const std::string_view point) -> db::base::Status {
        const std::scoped_lock lock{mutex_};
        const auto rule = rules_.find(std::string{point});
        if (rule == rules_.end()) {
            return {};
        }
        ++rule->second.observed;
        if (rule->second.observed != rule->second.fail_on_invocation) {
            return {};
        }
        return db::base::fail(
            rule->second.error.with_context("fault_injection", std::string{point})
        );
    }

    auto observed(const std::string_view point) const -> std::size_t {
        const std::scoped_lock lock{mutex_};
        const auto rule = rules_.find(std::string{point});
        return rule == rules_.end() ? 0 : rule->second.observed;
    }

    void clear() {
        const std::scoped_lock lock{mutex_};
        rules_.clear();
    }

private:
    struct Rule final {
        std::size_t fail_on_invocation;
        std::size_t observed;
        db::base::Error error;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Rule> rules_;
};

}  // namespace db::test_support
