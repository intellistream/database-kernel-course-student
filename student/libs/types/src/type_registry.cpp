#include "db/types/type_registry.hpp"

#include <array>
#include <span>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/text.hpp"

namespace db::types {
namespace {

enum class StorageKind {
    kFixed,
    kTypmodBytes,
    kNotStorable,
};

struct TypeDescriptor final {
    TypeId id;
    std::string_view name;
    std::span<const std::string_view> aliases;
    StorageKind storage;
    std::size_t fixed_width;
};

constexpr std::array<std::string_view, 0> kNoAliases{};
constexpr std::array kBoolAliases{std::string_view{"boolean"}};
constexpr std::array kInt64Aliases{
    std::string_view{"integer"},
    std::string_view{"bigint"},
    std::string_view{"smallint"},
};
constexpr std::array kFloat32Aliases{
    std::string_view{"real"},
    std::string_view{"double"},
    std::string_view{"double precision"},
};
constexpr std::array kCharAliases{std::string_view{"character"}};
constexpr std::array kTimestampAliases{
    std::string_view{"datetime"},
    std::string_view{"timestamp without time zone"},
};

constexpr std::array kDescriptors{
    TypeDescriptor{
        kUnknownTypeId,
        "unknown",
        kNoAliases,
        StorageKind::kNotStorable,
        0,
    },
    TypeDescriptor{
        kBoolTypeId,
        "bool",
        kBoolAliases,
        StorageKind::kFixed,
        1,
    },
    TypeDescriptor{
        kInt64TypeId,
        "int",
        kInt64Aliases,
        StorageKind::kFixed,
        8,
    },
    TypeDescriptor{
        kFloat32TypeId,
        "float",
        kFloat32Aliases,
        StorageKind::kFixed,
        4,
    },
    TypeDescriptor{
        kCharTypeId,
        "char",
        kCharAliases,
        StorageKind::kTypmodBytes,
        0,
    },
    TypeDescriptor{
        kTimestampTypeId,
        "timestamp",
        kTimestampAliases,
        StorageKind::kFixed,
        8,
    },
};

auto find_descriptor(const TypeId id) noexcept -> const TypeDescriptor* {
    for (const auto& descriptor : kDescriptors) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

auto validate_type_modifier(const DataType data_type, const TypeDescriptor& descriptor)
    -> base::Status {
    const auto modifier = data_type.modifier().value();
    bool valid = false;
    if (data_type.id() == kCharTypeId) {
        valid = modifier == kNoTypeModifier.value()
                || (modifier >= 1 && std::cmp_less_equal(modifier, kMaxCharLength));
    } else if (data_type.id() == kTimestampTypeId) {
        valid = modifier == kNoTypeModifier.value()
                || (modifier >= 0 && std::cmp_less_equal(modifier, kMaxTimestampPrecision));
    } else {
        valid = data_type.modifier().is_none();
    }

    if (!valid) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "invalid type modifier " + std::to_string(modifier) + " for "
                    + std::string{descriptor.name},
            }
        );
    }
    return {};
}

}  // namespace

auto find_type_id(const std::string_view name) noexcept -> std::optional<TypeId> {
    for (const auto& descriptor : kDescriptors) {
        if (base::ascii_equal_ignoring_case(descriptor.name, name)) {
            return descriptor.id;
        }
        for (const auto alias : descriptor.aliases) {
            if (base::ascii_equal_ignoring_case(alias, name)) {
                return descriptor.id;
            }
        }
    }
    return std::nullopt;
}

auto type_name(const TypeId id) noexcept -> std::optional<std::string_view> {
    const auto* const descriptor = find_descriptor(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    return descriptor->name;
}

auto is_known_type(const TypeId id) noexcept -> bool {
    return find_descriptor(id) != nullptr;
}

auto validate_data_type(const DataType data_type) -> base::Status {
    const auto* const descriptor = find_descriptor(data_type.id());
    if (descriptor == nullptr) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "unregistered type id " + std::to_string(data_type.id().value()),
            }
        );
    }
    return validate_type_modifier(data_type, *descriptor);
}

auto storage_width(const DataType data_type) -> base::Result<std::size_t> {
    const auto* const descriptor = find_descriptor(data_type.id());
    if (descriptor == nullptr) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "unregistered type id " + std::to_string(data_type.id().value()),
            }
        );
    }
    auto validation = validate_type_modifier(data_type, *descriptor);
    if (!validation) {
        return base::fail(std::move(validation).error());
    }
    switch (descriptor->storage) {
        case StorageKind::kFixed:
            return descriptor->fixed_width;
        case StorageKind::kTypmodBytes:
            if (data_type.modifier().is_none()) {
                return base::fail(
                    base::Error{
                        base::ErrorCode::kUnsupported,
                        "unbounded char has no inline storage width",
                    }
                );
            }
            return static_cast<std::size_t>(data_type.modifier().value());
        case StorageKind::kNotStorable:
            return base::fail(
                base::Error{
                    base::ErrorCode::kUnsupported,
                    std::string{descriptor->name} + " is not storable",
                }
            );
    }
    return base::fail(
        base::Error{
            base::ErrorCode::kInvariantViolation,
            "unrecognized storage kind",
        }
    );
}

auto is_storable(const DataType data_type) -> bool {
    const auto width = storage_width(data_type);
    return width.has_value();
}

}  // namespace db::types
