#pragma once

#include <memory>
#include <vector>

#include "db/engine/result.hpp"
#include "db/types/value.hpp"

namespace db::engine::detail {

// Engine-private counterpart to PostgreSQL's DestReceiver. The executor has
// already established the output descriptor and every PlanState publishes
// values through that immutable contract, so this receiver owns rows without
// repeating public ResultSet shape/type validation.
class MaterializedResultReceiver final {
public:
    explicit MaterializedResultReceiver(ResultSchema schema);

    MaterializedResultReceiver(const MaterializedResultReceiver&) = delete;
    auto operator=(const MaterializedResultReceiver&) -> MaterializedResultReceiver& = delete;
    MaterializedResultReceiver(MaterializedResultReceiver&&) noexcept = default;
    auto operator=(MaterializedResultReceiver&&) noexcept -> MaterializedResultReceiver& = default;
    ~MaterializedResultReceiver() = default;

    void receive(std::vector<types::Value> values);
    auto finish() && noexcept -> ResultSet;

private:
    std::shared_ptr<const ResultSchema> schema_;
    std::vector<ResultRow> rows_;
};

}  // namespace db::engine::detail
