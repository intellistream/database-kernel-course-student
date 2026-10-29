#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/wal/record.hpp"

#include "btree_wal_internal.hpp"

namespace db::recovery {
namespace {

auto payload_error(const bool decoding, const std::string_view operation, std::string message)
    -> base::Error {
    return base::Error{
        decoding ? base::ErrorCode::kCorruption : base::ErrorCode::kInvalidArgument,
        std::move(message),
    }
        .with_context("recovery.btree_wal", std::string{operation});
}

auto compatible_role(
    const access::BtreeWalOperation operation,
    const access::BtreeWalPageRole role
) noexcept -> bool {
    switch (operation) {
        case access::BtreeWalOperation::kInitializeMetadata:
            return role == access::BtreeWalPageRole::kMetadata;
        case access::BtreeWalOperation::kInsertLeaf:
        case access::BtreeWalOperation::kInsertUpper:
        case access::BtreeWalOperation::kDelete:
        case access::BtreeWalOperation::kVacuum:
            return role == access::BtreeWalPageRole::kPrimary;
        case access::BtreeWalOperation::kFinishSplit:
            return role == access::BtreeWalPageRole::kPrimary
                   || role == access::BtreeWalPageRole::kSplitChild;
        case access::BtreeWalOperation::kSplit:
            return role == access::BtreeWalPageRole::kSplitLeft
                   || role == access::BtreeWalPageRole::kSplitRight
                   || role == access::BtreeWalPageRole::kSplitSibling;
        case access::BtreeWalOperation::kSplitFinish:
            return role == access::BtreeWalPageRole::kSplitLeft
                   || role == access::BtreeWalPageRole::kSplitRight
                   || role == access::BtreeWalPageRole::kSplitSibling
                   || role == access::BtreeWalPageRole::kSplitChild;
        case access::BtreeWalOperation::kNewRoot:
            return role == access::BtreeWalPageRole::kNewRoot
                   || role == access::BtreeWalPageRole::kSplitLeft
                   || role == access::BtreeWalPageRole::kSplitRight
                   || role == access::BtreeWalPageRole::kMetadata
                   || role == access::BtreeWalPageRole::kSplitChild;
        case access::BtreeWalOperation::kBuildPage:
            return role == access::BtreeWalPageRole::kBuildPage
                   || role == access::BtreeWalPageRole::kNewRoot
                   || role == access::BtreeWalPageRole::kMetadata;
    }
    return false;
}

struct BorrowedDeltaPatch final {
    std::uint16_t offset{0U};
    base::Bytes bytes;
};

template <typename PatchAt>
auto encode_payload(
    const access::BtreeWalOperation operation,
    const access::BtreeWalPageRole role,
    const std::size_t patch_count,
    PatchAt patch_at
) -> base::Result<std::vector<std::byte>> {
    auto encoded_size = kBtreeWalPayloadHeaderSize + sizeof(std::uint16_t);
    for (std::size_t index = 0U; index < patch_count; ++index) {
        encoded_size += sizeof(std::uint16_t) * 2U + patch_at(index).bytes.size();
    }

    std::vector<std::byte> encoded(encoded_size);
    base::LittleEndianWriter writer{encoded};
    auto status = writer.write_u32(kBtreeWalPayloadMagic);
    if (status) {
        status = writer.write_u16(kBtreeWalPayloadVersion);
    }
    if (status) {
        status = writer.write_u16(static_cast<std::uint16_t>(kBtreeWalPayloadHeaderSize));
    }
    if (status) {
        status = writer.write_u8(static_cast<std::uint8_t>(operation));
    }
    if (status) {
        status = writer.write_u8(static_cast<std::uint8_t>(role));
    }
    if (status) {
        status = writer.write_u16(0U);
    }
    if (status) {
        status = writer.write_u16(static_cast<std::uint16_t>(patch_count));
    }
    for (std::size_t index = 0U; status && index < patch_count; ++index) {
        const auto patch = patch_at(index);
        status = writer.write_u16(patch.offset);
        if (status) {
            status = writer.write_u16(static_cast<std::uint16_t>(patch.bytes.size()));
        }
        if (status) {
            status = writer.write_bytes(patch.bytes);
        }
    }
    if (!status || writer.position() != encoded.size()) {
        return base::fail(
            payload_error(false, "encode", "B-tree WAL encoder did not fill its exact output")
        );
    }
    return encoded;
}

auto decode_operation_tag(const std::uint8_t tag, const bool decoding)
    -> base::Result<access::BtreeWalOperation> {
    switch (tag) {
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kInitializeMetadata):
            return access::BtreeWalOperation::kInitializeMetadata;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kInsertLeaf):
            return access::BtreeWalOperation::kInsertLeaf;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kInsertUpper):
            return access::BtreeWalOperation::kInsertUpper;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kSplit):
            return access::BtreeWalOperation::kSplit;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kDelete):
            return access::BtreeWalOperation::kDelete;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kNewRoot):
            return access::BtreeWalOperation::kNewRoot;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kVacuum):
            return access::BtreeWalOperation::kVacuum;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kBuildPage):
            return access::BtreeWalOperation::kBuildPage;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kFinishSplit):
            return access::BtreeWalOperation::kFinishSplit;
        case static_cast<std::uint8_t>(access::BtreeWalOperation::kSplitFinish):
            return access::BtreeWalOperation::kSplitFinish;
        default:
            return base::fail(payload_error(decoding, "decode", "unknown B-tree WAL operation"));
    }
}

auto decode_role_tag(const std::uint8_t tag, const bool decoding)
    -> base::Result<access::BtreeWalPageRole> {
    switch (tag) {
        case static_cast<std::uint8_t>(access::BtreeWalPageRole::kPrimary):
            return access::BtreeWalPageRole::kPrimary;
        case static_cast<std::uint8_t>(access::BtreeWalPageRole::kSplitLeft):
            return access::BtreeWalPageRole::kSplitLeft;
        case static_cast<std::uint8_t>(access::BtreeWalPageRole::kSplitRight):
            return access::BtreeWalPageRole::kSplitRight;
        case static_cast<std::uint8_t>(access::BtreeWalPageRole::kSplitSibling):
            return access::BtreeWalPageRole::kSplitSibling;
        case static_cast<std::uint8_t>(access::BtreeWalPageRole::kNewRoot):
            return access::BtreeWalPageRole::kNewRoot;
        case static_cast<std::uint8_t>(access::BtreeWalPageRole::kMetadata):
            return access::BtreeWalPageRole::kMetadata;
        case static_cast<std::uint8_t>(access::BtreeWalPageRole::kBuildPage):
            return access::BtreeWalPageRole::kBuildPage;
        case static_cast<std::uint8_t>(access::BtreeWalPageRole::kSplitChild):
            return access::BtreeWalPageRole::kSplitChild;
        default:
            return base::fail(payload_error(decoding, "decode", "unknown B-tree WAL page role"));
    }
}

}  // namespace

namespace detail {
namespace {

auto role_from_schema(
    const std::span<const access::BtreeWalPageRole> schema,
    const std::size_t block_index,
    const std::size_t block_count
) -> base::Result<access::BtreeWalPageRole> {
    if (block_count != schema.size() || block_index >= schema.size()) {
        return base::fail(payload_error(true, "validate record", "invalid B-tree WAL block role"));
    }
    return schema[block_index];
}

}  // namespace

auto btree_wal_info(const access::BtreeWalOperation operation) -> base::Result<std::uint8_t> {
    switch (operation) {
        case access::BtreeWalOperation::kInitializeMetadata:
            return wal::opcode::btree::kMetadata;
        case access::BtreeWalOperation::kInsertLeaf:
            return wal::opcode::btree::kInsertLeaf;
        case access::BtreeWalOperation::kInsertUpper:
            return wal::opcode::btree::kInsertUpper;
        case access::BtreeWalOperation::kSplit:
            return wal::opcode::btree::kSplit;
        case access::BtreeWalOperation::kDelete:
            return wal::opcode::btree::kDelete;
        case access::BtreeWalOperation::kNewRoot:
            return wal::opcode::btree::kNewRoot;
        case access::BtreeWalOperation::kVacuum:
            return wal::opcode::btree::kVacuum;
        case access::BtreeWalOperation::kBuildPage:
            return wal::opcode::btree::kBuildPage;
        case access::BtreeWalOperation::kFinishSplit:
            return wal::opcode::btree::kFinishSplit;
        case access::BtreeWalOperation::kSplitFinish:
            return wal::opcode::btree::kSplitFinish;
    }
    return base::fail(payload_error(false, "map opcode", "unknown B-tree WAL operation"));
}

auto btree_wal_operation(const std::uint8_t info) -> base::Result<access::BtreeWalOperation> {
    switch (info) {
        case wal::opcode::btree::kMetadata:
            return access::BtreeWalOperation::kInitializeMetadata;
        case wal::opcode::btree::kInsertLeaf:
            return access::BtreeWalOperation::kInsertLeaf;
        case wal::opcode::btree::kInsertUpper:
            return access::BtreeWalOperation::kInsertUpper;
        case wal::opcode::btree::kSplit:
            return access::BtreeWalOperation::kSplit;
        case wal::opcode::btree::kDelete:
            return access::BtreeWalOperation::kDelete;
        case wal::opcode::btree::kNewRoot:
            return access::BtreeWalOperation::kNewRoot;
        case wal::opcode::btree::kVacuum:
            return access::BtreeWalOperation::kVacuum;
        case wal::opcode::btree::kBuildPage:
            return access::BtreeWalOperation::kBuildPage;
        case wal::opcode::btree::kFinishSplit:
            return access::BtreeWalOperation::kFinishSplit;
        case wal::opcode::btree::kSplitFinish:
            return access::BtreeWalOperation::kSplitFinish;
        default:
            return base::fail(payload_error(true, "map opcode", "unknown B-tree WAL info code"));
    }
}

auto validate_btree_payload(const BtreeWalPayload& payload, const bool decoding) -> base::Status {
    if (!compatible_role(payload.operation, payload.role) || payload.patches.empty()) {
        return base::fail(payload_error(
            decoding,
            "validate payload",
            "B-tree WAL operation, page role, or patch list is invalid"
        ));
    }
    return {};
}

auto expected_btree_role(
    const access::BtreeWalOperation operation,
    const std::size_t block_index,
    const std::size_t block_count
) -> base::Result<access::BtreeWalPageRole> {
    using Role = access::BtreeWalPageRole;
    static constexpr std::array kMetadata{Role::kMetadata};
    static constexpr std::array kPrimary{Role::kPrimary};
    static constexpr std::array kSplit{Role::kSplitLeft, Role::kSplitRight};
    static constexpr std::array kSplitWithSibling{
        Role::kSplitLeft,
        Role::kSplitRight,
        Role::kSplitSibling,
    };
    static constexpr std::array kFinishSplit{Role::kPrimary, Role::kSplitChild};
    static constexpr std::array kSplitFinish{
        Role::kSplitLeft,
        Role::kSplitRight,
        Role::kSplitChild,
    };
    static constexpr std::array kSplitFinishWithSibling{
        Role::kSplitLeft,
        Role::kSplitRight,
        Role::kSplitSibling,
        Role::kSplitChild,
    };
    static constexpr std::array kFirstRoot{Role::kNewRoot, Role::kMetadata};
    static constexpr std::array kSplitRoot{
        Role::kSplitLeft,
        Role::kSplitRight,
        Role::kNewRoot,
        Role::kMetadata,
    };
    static constexpr std::array kSplitRootWithChild{
        Role::kSplitLeft,
        Role::kSplitRight,
        Role::kNewRoot,
        Role::kMetadata,
        Role::kSplitChild,
    };
    static constexpr std::array kBuildPage{Role::kBuildPage};
    static constexpr std::array kPublishBuild{Role::kNewRoot, Role::kMetadata};
    switch (operation) {
        case access::BtreeWalOperation::kInitializeMetadata:
            return role_from_schema(kMetadata, block_index, block_count);
        case access::BtreeWalOperation::kInsertLeaf:
        case access::BtreeWalOperation::kInsertUpper:
        case access::BtreeWalOperation::kDelete:
        case access::BtreeWalOperation::kVacuum:
            return role_from_schema(kPrimary, block_index, block_count);
        case access::BtreeWalOperation::kFinishSplit:
            return role_from_schema(kFinishSplit, block_index, block_count);
        case access::BtreeWalOperation::kSplit:
            return block_count == kSplit.size()
                       ? role_from_schema(kSplit, block_index, block_count)
                       : role_from_schema(kSplitWithSibling, block_index, block_count);
        case access::BtreeWalOperation::kSplitFinish:
            return block_count == kSplitFinish.size()
                       ? role_from_schema(kSplitFinish, block_index, block_count)
                       : role_from_schema(kSplitFinishWithSibling, block_index, block_count);
        case access::BtreeWalOperation::kNewRoot:
            if (block_count == kFirstRoot.size()) {
                return role_from_schema(kFirstRoot, block_index, block_count);
            }
            return block_count == kSplitRoot.size()
                       ? role_from_schema(kSplitRoot, block_index, block_count)
                       : role_from_schema(kSplitRootWithChild, block_index, block_count);
        case access::BtreeWalOperation::kBuildPage:
            return block_count == kBuildPage.size()
                       ? role_from_schema(kBuildPage, block_index, block_count)
                       : role_from_schema(kPublishBuild, block_index, block_count);
    }
    return base::fail(payload_error(true, "validate record", "unknown B-tree WAL operation"));
}

auto expected_btree_will_initialize(
    const access::BtreeWalOperation operation,
    const access::BtreeWalPageRole role
) noexcept -> bool {
    return operation == access::BtreeWalOperation::kInitializeMetadata
           || (operation == access::BtreeWalOperation::kBuildPage
               && role == access::BtreeWalPageRole::kBuildPage)
           || role == access::BtreeWalPageRole::kSplitRight
           || (role == access::BtreeWalPageRole::kNewRoot
               && operation == access::BtreeWalOperation::kNewRoot);
}

}  // namespace detail

auto encode_btree_wal_payload(const BtreeWalPayload& payload)
    -> base::Result<std::vector<std::byte>> {
    auto valid = detail::validate_btree_payload(payload, false);
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    const auto patches = payload.patches.patches();
    return encode_payload(payload.operation, payload.role, patches.size(), [patches](const auto i) {
        return BorrowedDeltaPatch{
            .offset = patches[i].offset,
            .bytes = base::Bytes{patches[i].bytes},
        };
    });
}

auto detail::encode_btree_wal_page_change(
    const access::BtreeWalOperation operation,
    const access::BtreeWalPageRole role,
    const base::Bytes after_page,
    const std::span<const access::BtreePageChangeRange> changed_ranges
) -> base::Result<std::vector<std::byte>> {
    return encode_payload(operation, role, changed_ranges.size(), [=](const auto index) {
        const auto range = changed_ranges[index];
        return BorrowedDeltaPatch{
            .offset = range.offset,
            .bytes = after_page.subspan(range.offset, range.length),
        };
    });
}

auto decode_btree_wal_payload(const std::uint8_t info, const base::Bytes bytes)
    -> base::Result<BtreeWalPayload> {
    base::LittleEndianReader reader{bytes};
    auto magic = reader.read_u32();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    auto operation_tag = reader.read_u8();
    auto role_tag = reader.read_u8();
    auto reserved = reader.read_u16();
    if (!magic || !version || !header_size || !operation_tag || !role_tag || !reserved
        || magic.value() != kBtreeWalPayloadMagic || version.value() != kBtreeWalPayloadVersion
        || header_size.value() != kBtreeWalPayloadHeaderSize || reserved.value() != 0U) {
        return base::fail(payload_error(true, "decode", "invalid B-tree WAL payload header"));
    }
    auto operation = decode_operation_tag(operation_tag.value(), true);
    auto role = decode_role_tag(role_tag.value(), true);
    auto info_operation = detail::btree_wal_operation(info);
    if (!operation || !role || !info_operation) {
        if (!operation) {
            return base::fail(std::move(operation).error());
        }
        if (!role) {
            return base::fail(std::move(role).error());
        }
        return base::fail(std::move(info_operation).error());
    }
    if (operation.value() != info_operation.value()) {
        return base::fail(payload_error(true, "decode", "B-tree WAL opcode and payload disagree"));
    }
    auto patch_bytes = reader.read_bytes(reader.remaining());
    if (!patch_bytes) {
        return base::fail(std::move(patch_bytes).error());
    }
    auto patches = wal::decode_page_delta(patch_bytes.value());
    if (!patches) {
        return base::fail(std::move(patches).error());
    }
    BtreeWalPayload payload{
        .operation = operation.value(),
        .role = role.value(),
        .patches = std::move(patches).value(),
    };
    auto valid = detail::validate_btree_payload(payload, true);
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    return payload;
}

}  // namespace db::recovery
