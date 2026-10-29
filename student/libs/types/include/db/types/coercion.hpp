#pragma once

#include <cstdint>
#include <optional>

#include "db/base/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::types {

enum class CoercionContext : std::uint8_t {
    kImplicit,
    kAssignment,
    kExplicit,
};

// An immutable resolved operation. Callers may compare paths or ask whether
// one is a no-op relabel, but cannot manufacture an invalid method/function
// combination. The resolver owns all concrete builtin and text-I/O choices.
class CoercionPath final {
public:
    constexpr CoercionPath() noexcept = default;

    constexpr auto is_relabel() const noexcept -> bool {
        return operation_ == Operation::kRelabel;
    }

    constexpr auto operator==(const CoercionPath&) const -> bool = default;

private:
    enum class Operation : std::uint8_t {
        kRelabel,
        kViaIo,
        kIntToFloat,
        kFloatToInt,
        kIntToBool,
        kBoolToInt,
    };

    constexpr explicit CoercionPath(const Operation operation) noexcept
        : operation_{operation} {}

    Operation operation_{Operation::kRelabel};

    friend auto
    find_coercion_path(DataType source, DataType target, CoercionContext context) noexcept
        -> std::optional<CoercionPath>;
    friend auto apply_coercion(const Value& value, DataType target, CoercionPath path)
        -> base::Result<Value>;
};

auto find_coercion_path(DataType source, DataType target, CoercionContext context) noexcept
    -> std::optional<CoercionPath>;
auto apply_coercion(const Value& value, DataType target, CoercionPath path) -> base::Result<Value>;

}  // namespace db::types
