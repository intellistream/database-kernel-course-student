#include "db/access/btree_relation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/storage/page.hpp"
#include "db/transaction/xid.hpp"

#include "btree_concurrency.hpp"
#include "btree_insert.hpp"
#include "btree_key_internal.hpp"
#include "btree_logging.hpp"

namespace db::access {
namespace {

[[maybe_unused]] auto relation_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view detail
) -> base::Error {
    return {code, "B-tree relation " + std::string{operation} + ": " + std::string{detail}};
}

[[maybe_unused]] auto metapage_address(const base::RelationId relation_id) noexcept
    -> storage::PageAddress {
    return storage::PageAddress{
        .relation_id = relation_id,
        .page_id = base::PageId{0},
    };
}

[[maybe_unused]] auto load_u16_le(const base::Bytes bytes, const std::size_t offset) noexcept
    -> std::uint16_t {
    const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
    const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
    return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
}

[[maybe_unused]] auto load_u32_le(const base::Bytes bytes, const std::size_t offset) noexcept
    -> std::uint32_t {
    return std::to_integer<std::uint32_t>(bytes[offset])
           | (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U)
           | (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U)
           | (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[maybe_unused]] auto load_u64_le(const base::Bytes bytes, const std::size_t offset) noexcept
    -> std::uint64_t {
    const auto low = static_cast<std::uint64_t>(load_u32_le(bytes, offset));
    const auto high = static_cast<std::uint64_t>(load_u32_le(bytes, offset + 4U));
    return low | (high << 32U);
}

[[maybe_unused]] auto
admit_metapage_format(const storage::Page& page, const BtreeKeyLayout& key_layout) -> base::Status {
    constexpr std::uint32_t legacy_magic = 0x334d'5442U;
    constexpr std::uint16_t legacy_version = 3U;
    constexpr std::size_t format_offset = storage::kPageHeaderSize;
    const auto bytes = base::Bytes{page.bytes()};
    const auto magic = load_u32_le(bytes, format_offset);
    const auto version = load_u16_le(bytes, format_offset + 4U);
    const auto header_size = load_u16_le(bytes, format_offset + 6U);
    if (magic == kBtreeMetapageMagic && version == kBtreeMetapageVersion
        && header_size == kBtreeMetapageHeaderSize) {
        const auto type_signature = load_u64_le(bytes, format_offset + 20U);
        const auto column_count = load_u16_le(bytes, format_offset + 28U);
        const auto maximum_key_size = load_u16_le(bytes, format_offset + 30U);
        if (type_signature == key_layout.type_signature()
            && column_count == key_layout.column_count()
            && maximum_key_size == key_layout.maximum_encoded_size()) {
            return {};
        }
        return base::fail(relation_error(
            base::ErrorCode::kInvalidArgument,
            "open",
            "the requested key layout does not match the persisted relation"
        ));
    }
    if (magic == legacy_magic && version == legacy_version
        && header_size == kBtreeMetapageHeaderSize) {
        return base::fail(relation_error(
            base::ErrorCode::kUnsupported,
            "open",
            "BTM3/BTP4 index format requires an index rebuild"
        ));
    }
    return base::fail(relation_error(
        base::ErrorCode::kCorruption,
        "open",
        "metapage format identifier, version, or header size is invalid"
    ));
}

}  // namespace

BtreeRelation::BtreeRelation(
    const base::RelationId relation_id,
    BtreeKeyLayout key_layout,
    const bool is_unique,
    BtreeMutationLogPort& mutation_log,
    std::shared_ptr<State> state
) noexcept
    : relation_id_{relation_id},
      key_layout_{std::move(key_layout)},
      is_unique_{is_unique},
      mutation_log_{&mutation_log},
      relation_state_{std::move(state)} {}

BtreeRelation::BtreeRelation(BtreeRelation&& other) noexcept
    : relation_id_{std::exchange(other.relation_id_, base::kInvalidRelationId)},
      key_layout_{std::move(other.key_layout_)},
      is_unique_{std::exchange(other.is_unique_, false)},
      mutation_log_{std::exchange(other.mutation_log_, nullptr)},
      relation_state_{std::move(other.relation_state_)} {
    other.key_layout_ = BtreeKeyLayout{};
}

auto BtreeRelation::operator=(BtreeRelation&& other) noexcept -> BtreeRelation& {
    if (this != &other) {
        relation_id_ = std::exchange(other.relation_id_, base::kInvalidRelationId);
        key_layout_ = std::move(other.key_layout_);
        is_unique_ = std::exchange(other.is_unique_, false);
        mutation_log_ = std::exchange(other.mutation_log_, nullptr);
        relation_state_ = std::move(other.relation_state_);
        other.key_layout_ = BtreeKeyLayout{};
    }
    return *this;
}

[[maybe_unused]] auto BtreeRelation::create(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] BtreeKeyLayout key_layout,
    [[maybe_unused]] const bool is_unique,
    [[maybe_unused]] BtreeMutationLogPort& mutation_log
) -> base::Result<BtreeRelation> {
    // LAB-S03.Q08:R01-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q08:R01"});
    // LAB-S03.Q08:R01-END
}

[[maybe_unused]] auto BtreeRelation::open(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] BtreeKeyLayout key_layout,
    [[maybe_unused]] const bool is_unique,
    [[maybe_unused]] BtreeMutationLogPort& mutation_log
) -> base::Result<BtreeRelation> {
    // LAB-S03.Q08:R02-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q08:R02"});
    // LAB-S03.Q08:R02-END
}

auto BtreeRelation::valid() const noexcept -> bool {
    return relation_id_ != base::kInvalidRelationId && key_layout_.valid()
           && mutation_log_ != nullptr && relation_state_ != nullptr;
}

auto BtreeRelation::relation_id() const noexcept -> base::RelationId {
    return relation_id_;
}

auto BtreeRelation::key_layout() const noexcept -> const BtreeKeyLayout& {
    return key_layout_;
}

auto BtreeRelation::is_unique() const noexcept -> bool {
    return is_unique_;
}

auto BtreeRelation::mutation_log() const noexcept -> BtreeMutationLogPort& {
    return *mutation_log_;
}

auto BtreeRelation::relation_state() const noexcept
    -> const std::shared_ptr<BtreeRelation::State>& {
    return relation_state_;
}

[[maybe_unused]] auto BtreeRelation::root([[maybe_unused]] storage::BufferPool& pool) const
    -> base::Result<std::optional<BtreeRoot>> {
    // LAB-S03.Q02:R14-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q02:R14"});
    // LAB-S03.Q02:R14-END
}

[[maybe_unused]] auto
BtreeRelation::create_empty_root([[maybe_unused]] storage::BufferPool& pool) const
    -> base::Result<BtreeRoot> {
    // LAB-S03.Q03:R45-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q03:R45"});
    // LAB-S03.Q03:R45-END
}

[[maybe_unused]] auto BtreeRelation::insert(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const std::span<const types::Value> values,
    [[maybe_unused]] const base::Rid heap_rid
) const -> base::Status {
    // LAB-S03.Q08:R03-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q08:R03"});
    // LAB-S03.Q08:R03-END
}

[[maybe_unused]] auto BtreeRelation::insert(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const std::span<const types::Value> values,
    [[maybe_unused]] const base::Rid heap_rid,
    [[maybe_unused]] const BtreeInsertContext context
) const -> base::Status {
    // LAB-S03.Q08:R04-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q08:R04"});
    // LAB-S03.Q08:R04-END
}

[[maybe_unused]] auto BtreeRelation::insert_impl(
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] const std::span<const types::Value> values,
    [[maybe_unused]] const base::Rid heap_rid,
    [[maybe_unused]] const BtreeInsertContext* const context
) const -> base::Status {
    // LAB-S03.Q08:R05-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q08:R05"});
    // LAB-S03.Q08:R05-END
}

}  // namespace db::access
