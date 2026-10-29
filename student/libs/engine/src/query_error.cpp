#include "query_error.hpp"

#include <utility>

namespace db::engine::detail {

auto query_error(const base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("engine.query", std::move(operation));
}

}  // namespace db::engine::detail
