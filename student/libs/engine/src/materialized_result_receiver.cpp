#include "materialized_result_receiver.hpp"

#include <memory>
#include <utility>

namespace db::engine::detail {

MaterializedResultReceiver::MaterializedResultReceiver(ResultSchema schema)
    : schema_{std::make_shared<const ResultSchema>(std::move(schema))} {}

void MaterializedResultReceiver::receive(std::vector<types::Value> values) {
    rows_.push_back(ResultRow{schema_, std::move(values)});
}

auto MaterializedResultReceiver::finish() && noexcept -> ResultSet {
    return ResultSet{std::move(schema_), std::move(rows_)};
}

}  // namespace db::engine::detail
