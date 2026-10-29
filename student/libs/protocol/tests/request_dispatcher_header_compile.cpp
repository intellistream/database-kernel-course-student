#include <type_traits>

#include "db/protocol/request_dispatcher.hpp"

static_assert(!std::is_copy_constructible_v<db::protocol::PreparedStatementPort>);
static_assert(!std::is_copy_constructible_v<db::protocol::SessionPort>);
static_assert(!std::is_copy_constructible_v<db::protocol::SessionFactoryPort>);
static_assert(!std::is_copy_constructible_v<db::protocol::RequestDispatcher>);
static_assert(std::is_nothrow_move_constructible_v<db::protocol::RequestDispatcher>);
static_assert(std::is_nothrow_move_assignable_v<db::protocol::RequestDispatcher>);
static_assert(db::protocol::is_known_wire_data_type(db::protocol::WireDataType::kInt32));
static_assert(db::protocol::wire_data_type_from_tag(0x03U) == db::protocol::WireDataType::kChar);
static_assert(
    db::protocol::wire_value_data_type(db::protocol::WireDataType::kInt32) == db::types::kInt64Type
);

namespace {

[[maybe_unused]] void compile_request_dispatcher_api() {
    db::protocol::ColumnMetadata column{
        .name = "value",
        .data_type = db::protocol::WireDataType::kInt32,
    };
    auto execution = db::protocol::ExecutionResult::query({column}, {});
    static_cast<void>(execution);
}

}  // namespace
