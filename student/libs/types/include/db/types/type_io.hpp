#pragma once

#include <string>
#include <string_view>

#include "db/base/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::types {

// Binds one validated type to its text parser. The object has no mutable
// state; parsing only consumes the caller-owned text view. parse_into() lets
// batch consumers retain a destination CHAR allocation across rows.
class PreparedTypeInput final {
public:
    static auto create(DataType data_type) -> base::Result<PreparedTypeInput>;

    auto parse(std::string_view text) const -> base::Result<Value>;
    auto parse_into(std::string_view text, Value& destination) const -> base::Status;
    auto null_value() const noexcept -> Value;
    auto data_type() const noexcept -> DataType;

private:
    using InputFunction = base::Result<Value> (*)(DataType, std::string_view);

    PreparedTypeInput(DataType data_type, InputFunction input) noexcept
        : data_type_{data_type},
          input_{input} {}

    DataType data_type_;
    InputFunction input_;
};

auto input_value(DataType data_type, std::string_view text) -> base::Result<Value>;
auto output_value(const Value& value) -> base::Result<std::string>;
auto apply_type_modifier(Value value, DataType target) -> base::Result<Value>;

}  // namespace db::types
