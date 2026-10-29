#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

#include "db/base/hash.hpp"
#include "db/base/identifier.hpp"

namespace db::types {

using db::base::TypeId;

// Type IDs are persisted identities. Existing values must never be
// renumbered or reused. ID 3 belonged to the removed FLOAT64 representation.
inline constexpr TypeId kUnknownTypeId{0};
inline constexpr TypeId kBoolTypeId{1};
inline constexpr TypeId kInt64TypeId{2};
inline constexpr TypeId kReservedFloat64TypeId{3};
inline constexpr TypeId kCharTypeId{4};
inline constexpr TypeId kTimestampTypeId{5};
inline constexpr TypeId kFloat32TypeId{6};
inline constexpr std::uint32_t kMaxCharLength = 4'048;
inline constexpr std::uint32_t kMaxTimestampPrecision = 6;

class TypeModifier final {
public:
    constexpr explicit TypeModifier(const std::int32_t value) noexcept
        : value_{value} {}

    constexpr auto value() const noexcept -> std::int32_t {
        return value_;
    }

    constexpr auto is_none() const noexcept -> bool {
        return value_ == -1;
    }

    constexpr auto operator==(const TypeModifier& other) const noexcept -> bool {
        return value_ == other.value_;
    }

    constexpr auto operator<=>(const TypeModifier& other) const noexcept {
        return value_ <=> other.value_;
    }

private:
    std::int32_t value_;
};

inline constexpr TypeModifier kNoTypeModifier{-1};

class DataType final {
public:
    constexpr explicit DataType(const TypeId id) noexcept
        : DataType{id, kNoTypeModifier} {}

    constexpr DataType(const TypeId id, const TypeModifier modifier) noexcept
        : id_{id},
          modifier_{modifier} {}

    constexpr auto id() const noexcept -> TypeId {
        return id_;
    }

    constexpr auto modifier() const noexcept -> TypeModifier {
        return modifier_;
    }

    constexpr auto operator==(const DataType& other) const noexcept -> bool {
        return id_ == other.id_ && modifier_ == other.modifier_;
    }

    constexpr auto operator<=>(const DataType& other) const noexcept {
        if (id_ != other.id_) {
            return id_ <=> other.id_;
        }
        return modifier_ <=> other.modifier_;
    }

private:
    TypeId id_;
    TypeModifier modifier_;
};

inline constexpr DataType kUnknownType{kUnknownTypeId};
inline constexpr DataType kBoolType{kBoolTypeId};
inline constexpr DataType kInt64Type{kInt64TypeId};
inline constexpr DataType kCharType{kCharTypeId};
inline constexpr DataType kTimestampType{kTimestampTypeId};
inline constexpr DataType kFloat32Type{kFloat32TypeId};

static_assert(sizeof(TypeModifier) == sizeof(std::int32_t));
static_assert(std::is_trivially_copyable_v<TypeModifier>);
static_assert(std::is_trivially_copyable_v<DataType>);

}  // namespace db::types

template <>
struct std::hash<db::types::TypeModifier> {
    auto operator()(const db::types::TypeModifier modifier) const noexcept -> std::size_t {
        return std::hash<std::int32_t>{}(modifier.value());
    }
};

template <>
struct std::hash<db::types::DataType> {
    auto operator()(const db::types::DataType type) const noexcept -> std::size_t {
        const auto id_hash = std::hash<db::base::TypeId>{}(type.id());
        const auto modifier_hash = std::hash<db::types::TypeModifier>{}(type.modifier());
        return db::base::combine_hash(id_hash, modifier_hash);
    }
};
