#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/access/heap_page.hpp"
#include "db/base/error.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/storage/page.hpp"
#include "db/wal/record.hpp"

#include "heap_wal_internal.hpp"

namespace db::recovery {
namespace {

constexpr std::size_t kInsertListHeaderSize = 4;
constexpr std::size_t kInsertTupleHeaderSize = 8;
constexpr std::size_t kMetadataPayloadSize = 4 + access::kTupleHeaderSize;
constexpr std::size_t kHotUpdateHeaderSize = 12;
constexpr std::size_t kPruneHeaderSize = 8;
constexpr std::uint16_t kKnownPageFlags =
    access::heap_page_flags::kPrunable | access::heap_page_flags::kFull;

auto payload_error(const bool decoding, const std::string_view operation, std::string message)
    -> base::Error {
    return base::Error{
        decoding ? base::ErrorCode::kCorruption : base::ErrorCode::kInvalidArgument,
        std::move(message),
    }
        .with_context("recovery.heap_wal", std::string{operation});
}

auto fail_payload(const bool decoding, const std::string_view operation, std::string message)
    -> base::Status {
    return base::fail(payload_error(decoding, operation, std::move(message)));
}

auto known_page_flags(const std::uint16_t flags) noexcept -> bool {
    return (flags & static_cast<std::uint16_t>(~kKnownPageFlags)) == 0U;
}

auto strictly_sorted_slots(const std::span<const base::SlotId> slots) -> bool {
    return std::adjacent_find(slots.begin(), slots.end(), std::greater_equal{}) == slots.end();
}

auto strictly_sorted_redirects(const std::span<const access::HeapPruneRedirect> redirects) -> bool {
    return std::adjacent_find(
               redirects.begin(),
               redirects.end(),
               [](const auto& left, const auto& right) {
                   return left.root >= right.root;
               }
           )
           == redirects.end();
}

auto decode_tuple_header(const HeapWalTupleData& tuple, const bool exact, const bool decoding)
    -> base::Result<access::TupleHeader> {
    if (tuple.slot == base::kInvalidSlotId || tuple.bytes.size() < access::kTupleHeaderSize
        || (exact && tuple.bytes.size() != access::kTupleHeaderSize)
        || tuple.bytes.size()
               > storage::kPageSize - storage::kPageHeaderSize - access::kLinePointerSize) {
        return base::fail(payload_error(
            decoding,
            "validate payload",
            "heap WAL tuple has an invalid slot or length"
        ));
    }
    auto header = access::TupleHeader::decode(tuple.bytes);
    if (!header) {
        return base::fail(
            payload_error(decoding, "validate payload", "heap WAL tuple header is not canonical")
        );
    }
    return header.value();
}

auto validate_insert_payload(
    const HeapWalPayload& payload,
    const bool multiple,
    const bool decoding
) -> base::Status {
    if (payload.page_flags != 0U || !payload.redirects.empty() || !payload.dead.empty()
        || !payload.unused.empty() || payload.tuples.empty()
        || (!multiple && payload.tuples.size() != 1U)
        || payload.tuples.size() > std::numeric_limits<std::uint16_t>::max()) {
        return fail_payload(decoding, "validate insert", "heap insert WAL shape is invalid");
    }
    base::SlotId previous = base::kInvalidSlotId;
    for (std::size_t index = 0; index < payload.tuples.size(); ++index) {
        const auto& tuple = payload.tuples[index];
        auto header = decode_tuple_header(tuple, false, decoding);
        if (!header || header->flags != 0U || header->xmax != base::kInvalidXid
            || header->ctid.slot_id() != tuple.slot || (index != 0U && tuple.slot <= previous)) {
            return fail_payload(
                decoding,
                "validate insert",
                "heap insert WAL tuples are not canonical or slot ordered"
            );
        }
        previous = tuple.slot;
    }
    return {};
}

auto valid_metadata_flags(
    const access::HeapWalOperation operation,
    const access::TupleHeader& header
) noexcept -> bool {
    switch (operation) {
        case access::HeapWalOperation::kLock:
            return header.has_flag(access::tuple_flags::kXmaxLockOnly)
                   && !header.has_flag(access::tuple_flags::kUpdated)
                   && !header.has_flag(access::tuple_flags::kDeleted)
                   && !header.has_flag(access::tuple_flags::kHotUpdated);
        case access::HeapWalOperation::kUpdate:
            return header.has_flag(access::tuple_flags::kUpdated)
                   && !header.has_flag(access::tuple_flags::kHotUpdated)
                   && !header.has_flag(access::tuple_flags::kDeleted)
                   && !header.has_flag(access::tuple_flags::kXmaxLockOnly);
        case access::HeapWalOperation::kDelete:
            return header.has_flag(access::tuple_flags::kDeleted)
                   && !header.has_flag(access::tuple_flags::kUpdated)
                   && !header.has_flag(access::tuple_flags::kHotUpdated)
                   && !header.has_flag(access::tuple_flags::kXmaxLockOnly);
        default:
            return false;
    }
}

auto validate_metadata_payload(const HeapWalPayload& payload, const bool decoding) -> base::Status {
    if (!known_page_flags(payload.page_flags) || payload.tuples.size() != 1U
        || !payload.redirects.empty() || !payload.dead.empty() || !payload.unused.empty()) {
        return fail_payload(decoding, "validate metadata", "heap metadata WAL shape is invalid");
    }
    auto header = decode_tuple_header(payload.tuples.front(), true, decoding);
    if (!header || !valid_metadata_flags(payload.operation, header.value())) {
        return fail_payload(
            decoding,
            "validate metadata",
            "heap metadata WAL tuple flags do not match the operation"
        );
    }
    return {};
}

auto validate_hot_update_payload(const HeapWalPayload& payload, const bool decoding)
    -> base::Status {
    if (!known_page_flags(payload.page_flags) || payload.tuples.size() != 2U
        || payload.tuples[0].slot == payload.tuples[1].slot || !payload.redirects.empty()
        || !payload.dead.empty() || !payload.unused.empty()) {
        return fail_payload(decoding, "validate HOT update", "heap HOT WAL shape is invalid");
    }
    auto old_header = decode_tuple_header(payload.tuples[0], true, decoding);
    auto new_header = decode_tuple_header(payload.tuples[1], false, decoding);
    if (!old_header || !new_header || !old_header->has_flag(access::tuple_flags::kUpdated)
        || !old_header->has_flag(access::tuple_flags::kHotUpdated)
        || old_header->has_flag(access::tuple_flags::kDeleted)
        || old_header->has_flag(access::tuple_flags::kXmaxLockOnly)
        || !new_header->has_flag(access::tuple_flags::kHeapOnly)
        || new_header->ctid.slot_id() != payload.tuples[1].slot) {
        return fail_payload(
            decoding,
            "validate HOT update",
            "heap HOT WAL tuple metadata is inconsistent"
        );
    }
    return {};
}

auto validate_prune_payload(const HeapWalPayload& payload, const bool decoding) -> base::Status {
    if (!known_page_flags(payload.page_flags) || !payload.tuples.empty()
        || (payload.redirects.empty() && payload.dead.empty() && payload.unused.empty())
        || payload.redirects.size() > std::numeric_limits<std::uint16_t>::max()
        || payload.dead.size() > std::numeric_limits<std::uint16_t>::max()
        || payload.unused.size() > std::numeric_limits<std::uint16_t>::max()
        || !strictly_sorted_redirects(payload.redirects) || !strictly_sorted_slots(payload.dead)
        || !strictly_sorted_slots(payload.unused)) {
        return fail_payload(decoding, "validate prune", "heap prune WAL shape is invalid");
    }

    std::set<base::SlotId> changed;
    std::set<base::SlotId> targets;
    for (const auto redirect : payload.redirects) {
        if (redirect.root == base::kInvalidSlotId || redirect.target == base::kInvalidSlotId
            || redirect.root == redirect.target || !changed.insert(redirect.root).second) {
            return fail_payload(
                decoding,
                "validate prune",
                "heap prune WAL redirect is invalid or duplicated"
            );
        }
        targets.insert(redirect.target);
    }
    for (const auto slot : payload.dead) {
        if (slot == base::kInvalidSlotId || !changed.insert(slot).second) {
            return fail_payload(decoding, "validate prune", "heap prune dead slot is invalid");
        }
    }
    for (const auto slot : payload.unused) {
        if (slot == base::kInvalidSlotId || !changed.insert(slot).second) {
            return fail_payload(decoding, "validate prune", "heap prune unused slot is invalid");
        }
    }
    for (const auto target : targets) {
        if (changed.contains(target)) {
            return fail_payload(
                decoding,
                "validate prune",
                "heap prune redirect target is also removed"
            );
        }
    }
    return {};
}

auto encoded_size(const HeapWalPayload& payload) -> base::Result<std::size_t> {
    std::size_t size = kHeapWalPayloadHeaderSize;
    switch (payload.operation) {
        case access::HeapWalOperation::kInitialize:
            break;
        case access::HeapWalOperation::kInsert:
        case access::HeapWalOperation::kMultiInsert:
            size += kInsertListHeaderSize;
            for (const auto& tuple : payload.tuples) {
                if (tuple.bytes.size() > std::numeric_limits<std::uint32_t>::max()
                    || size > std::numeric_limits<std::uint32_t>::max() - kInsertTupleHeaderSize
                                  - tuple.bytes.size()) {
                    return base::fail(
                        payload_error(false, "size payload", "heap WAL is too large")
                    );
                }
                size += kInsertTupleHeaderSize + tuple.bytes.size();
            }
            break;
        case access::HeapWalOperation::kLock:
        case access::HeapWalOperation::kUpdate:
        case access::HeapWalOperation::kDelete:
            size += kMetadataPayloadSize;
            break;
        case access::HeapWalOperation::kHotUpdate:
            size +=
                kHotUpdateHeaderSize + access::kTupleHeaderSize + payload.tuples[1].bytes.size();
            break;
        case access::HeapWalOperation::kPrune:
            size += kPruneHeaderSize + payload.redirects.size() * 4U
                    + (payload.dead.size() + payload.unused.size()) * 2U;
            break;
    }
    return size;
}

auto write_common_header(base::LittleEndianWriter& writer) -> base::Status {
    auto status = writer.write_u32(kHeapWalPayloadMagic);
    if (status) {
        status = writer.write_u16(kHeapWalPayloadVersion);
    }
    if (status) {
        status = writer.write_u16(static_cast<std::uint16_t>(kHeapWalPayloadHeaderSize));
    }
    return status;
}

auto write_insert_payload(base::LittleEndianWriter& writer, const HeapWalPayload& payload)
    -> base::Status {
    auto status = writer.write_u16(static_cast<std::uint16_t>(payload.tuples.size()));
    if (status) {
        status = writer.write_u16(0);
    }
    for (const auto& tuple : payload.tuples) {
        if (status) {
            status = writer.write_u16(tuple.slot.value());
        }
        if (status) {
            status = writer.write_u16(0);
        }
        if (status) {
            status = writer.write_u32(static_cast<std::uint32_t>(tuple.bytes.size()));
        }
        if (status) {
            status = writer.write_bytes(tuple.bytes);
        }
    }
    return status;
}

auto write_metadata_payload(base::LittleEndianWriter& writer, const HeapWalPayload& payload)
    -> base::Status {
    auto status = writer.write_u16(payload.tuples.front().slot.value());
    if (status) {
        status = writer.write_u16(payload.page_flags);
    }
    if (status) {
        status = writer.write_bytes(payload.tuples.front().bytes);
    }
    return status;
}

auto write_hot_update_payload(base::LittleEndianWriter& writer, const HeapWalPayload& payload)
    -> base::Status {
    auto status = writer.write_u16(payload.tuples[0].slot.value());
    if (status) {
        status = writer.write_u16(payload.tuples[1].slot.value());
    }
    if (status) {
        status = writer.write_u16(payload.page_flags);
    }
    if (status) {
        status = writer.write_u16(0);
    }
    if (status) {
        status = writer.write_u32(static_cast<std::uint32_t>(payload.tuples[1].bytes.size()));
    }
    if (status) {
        status = writer.write_bytes(payload.tuples[0].bytes);
    }
    if (status) {
        status = writer.write_bytes(payload.tuples[1].bytes);
    }
    return status;
}

auto write_prune_payload(base::LittleEndianWriter& writer, const HeapWalPayload& payload)
    -> base::Status {
    auto status = writer.write_u16(payload.page_flags);
    if (status) {
        status = writer.write_u16(static_cast<std::uint16_t>(payload.redirects.size()));
    }
    if (status) {
        status = writer.write_u16(static_cast<std::uint16_t>(payload.dead.size()));
    }
    if (status) {
        status = writer.write_u16(static_cast<std::uint16_t>(payload.unused.size()));
    }
    for (const auto redirect : payload.redirects) {
        if (status) {
            status = writer.write_u16(redirect.root.value());
        }
        if (status) {
            status = writer.write_u16(redirect.target.value());
        }
    }
    for (const auto slot : payload.dead) {
        if (status) {
            status = writer.write_u16(slot.value());
        }
    }
    for (const auto slot : payload.unused) {
        if (status) {
            status = writer.write_u16(slot.value());
        }
    }
    return status;
}

auto write_operation_payload(base::LittleEndianWriter& writer, const HeapWalPayload& payload)
    -> base::Status {
    switch (payload.operation) {
        case access::HeapWalOperation::kInitialize:
            return {};
        case access::HeapWalOperation::kInsert:
        case access::HeapWalOperation::kMultiInsert:
            return write_insert_payload(writer, payload);
        case access::HeapWalOperation::kLock:
        case access::HeapWalOperation::kUpdate:
        case access::HeapWalOperation::kDelete:
            return write_metadata_payload(writer, payload);
        case access::HeapWalOperation::kHotUpdate:
            return write_hot_update_payload(writer, payload);
        case access::HeapWalOperation::kPrune:
            return write_prune_payload(writer, payload);
    }
    return base::fail(payload_error(false, "encode payload", "unknown heap WAL operation"));
}

auto read_tuple(base::LittleEndianReader& reader) -> base::Result<HeapWalTupleData> {
    auto slot = reader.read_u16();
    auto reserved = reader.read_u16();
    auto length = reader.read_u32();
    if (!slot || !reserved || !length || reserved.value() != 0U
        || length.value() < access::kTupleHeaderSize) {
        return base::fail(payload_error(true, "decode insert", "heap WAL tuple header is invalid"));
    }
    auto bytes = reader.read_bytes(length.value());
    if (!bytes) {
        return base::fail(payload_error(true, "decode insert", "heap WAL tuple is truncated"));
    }
    return HeapWalTupleData{
        .slot = base::SlotId{slot.value()},
        .bytes = std::vector<std::byte>{bytes->begin(), bytes->end()},
    };
}

auto read_insert_payload(const access::HeapWalOperation operation, base::LittleEndianReader& reader)
    -> base::Result<HeapWalPayload> {
    auto count = reader.read_u16();
    auto reserved = reader.read_u16();
    if (!count || !reserved || count.value() == 0U || reserved.value() != 0U) {
        return base::fail(payload_error(true, "decode insert", "heap insert WAL count is invalid"));
    }
    HeapWalPayload payload{
        .operation = operation,
        .page_flags = 0,
        .tuples = {},
        .redirects = {},
        .dead = {},
        .unused = {},
    };
    payload.tuples.reserve(count.value());
    for (std::size_t index = 0; index < count.value(); ++index) {
        auto tuple = read_tuple(reader);
        if (!tuple) {
            return base::fail(std::move(tuple).error());
        }
        payload.tuples.push_back(std::move(tuple).value());
    }
    return payload;
}

auto read_metadata_payload(
    const access::HeapWalOperation operation,
    base::LittleEndianReader& reader
) -> base::Result<HeapWalPayload> {
    auto slot = reader.read_u16();
    auto page_flags = reader.read_u16();
    auto header = reader.read_bytes(access::kTupleHeaderSize);
    if (!slot || !page_flags || !header) {
        return base::fail(payload_error(true, "decode metadata", "heap metadata WAL is truncated"));
    }
    return HeapWalPayload{
        .operation = operation,
        .page_flags = page_flags.value(),
        .tuples = {HeapWalTupleData{
            .slot = base::SlotId{slot.value()},
            .bytes = std::vector<std::byte>{header->begin(), header->end()},
        }},
        .redirects = {},
        .dead = {},
        .unused = {},
    };
}

auto read_hot_update_payload(base::LittleEndianReader& reader) -> base::Result<HeapWalPayload> {
    auto old_slot = reader.read_u16();
    auto new_slot = reader.read_u16();
    auto page_flags = reader.read_u16();
    auto reserved = reader.read_u16();
    auto new_length = reader.read_u32();
    auto old_header = reader.read_bytes(access::kTupleHeaderSize);
    if (!old_slot || !new_slot || !page_flags || !reserved || !new_length || !old_header
        || reserved.value() != 0U || new_length.value() < access::kTupleHeaderSize) {
        return base::fail(
            payload_error(true, "decode HOT update", "heap HOT WAL header is invalid")
        );
    }
    auto new_tuple = reader.read_bytes(new_length.value());
    if (!new_tuple) {
        return base::fail(
            payload_error(true, "decode HOT update", "heap HOT WAL tuple is truncated")
        );
    }
    return HeapWalPayload{
        .operation = access::HeapWalOperation::kHotUpdate,
        .page_flags = page_flags.value(),
        .tuples =
            {
                HeapWalTupleData{
                    .slot = base::SlotId{old_slot.value()},
                    .bytes = std::vector<std::byte>{old_header->begin(), old_header->end()},
                },
                HeapWalTupleData{
                    .slot = base::SlotId{new_slot.value()},
                    .bytes = std::vector<std::byte>{new_tuple->begin(), new_tuple->end()},
                },
            },
        .redirects = {},
        .dead = {},
        .unused = {},
    };
}

auto read_prune_payload(base::LittleEndianReader& reader) -> base::Result<HeapWalPayload> {
    auto page_flags = reader.read_u16();
    auto redirect_count = reader.read_u16();
    auto dead_count = reader.read_u16();
    auto unused_count = reader.read_u16();
    if (!page_flags || !redirect_count || !dead_count || !unused_count) {
        return base::fail(
            payload_error(true, "decode prune", "heap prune WAL header is truncated")
        );
    }
    HeapWalPayload payload{
        .operation = access::HeapWalOperation::kPrune,
        .page_flags = page_flags.value(),
        .tuples = {},
        .redirects = {},
        .dead = {},
        .unused = {},
    };
    payload.redirects.reserve(redirect_count.value());
    payload.dead.reserve(dead_count.value());
    payload.unused.reserve(unused_count.value());
    for (std::size_t index = 0; index < redirect_count.value(); ++index) {
        auto root = reader.read_u16();
        auto target = reader.read_u16();
        if (!root || !target) {
            return base::fail(
                payload_error(true, "decode prune", "heap prune redirect is truncated")
            );
        }
        payload.redirects.push_back(
            access::HeapPruneRedirect{
                .root = base::SlotId{root.value()},
                .target = base::SlotId{target.value()},
            }
        );
    }
    for (std::size_t index = 0; index < dead_count.value(); ++index) {
        auto slot = reader.read_u16();
        if (!slot) {
            return base::fail(
                payload_error(true, "decode prune", "heap prune dead slot is truncated")
            );
        }
        payload.dead.emplace_back(slot.value());
    }
    for (std::size_t index = 0; index < unused_count.value(); ++index) {
        auto slot = reader.read_u16();
        if (!slot) {
            return base::fail(
                payload_error(true, "decode prune", "heap prune unused slot is truncated")
            );
        }
        payload.unused.emplace_back(slot.value());
    }
    return payload;
}

auto read_operation_payload(
    const access::HeapWalOperation operation,
    base::LittleEndianReader& reader
) -> base::Result<HeapWalPayload> {
    switch (operation) {
        case access::HeapWalOperation::kInitialize:
            return HeapWalPayload{
                .operation = operation,
                .page_flags = 0,
                .tuples = {},
                .redirects = {},
                .dead = {},
                .unused = {},
            };
        case access::HeapWalOperation::kInsert:
        case access::HeapWalOperation::kMultiInsert:
            return read_insert_payload(operation, reader);
        case access::HeapWalOperation::kLock:
        case access::HeapWalOperation::kUpdate:
        case access::HeapWalOperation::kDelete:
            return read_metadata_payload(operation, reader);
        case access::HeapWalOperation::kHotUpdate:
            return read_hot_update_payload(reader);
        case access::HeapWalOperation::kPrune:
            return read_prune_payload(reader);
    }
    return base::fail(payload_error(true, "decode payload", "unknown heap WAL operation"));
}

}  // namespace

namespace detail {

auto heap_wal_info(const access::HeapWalOperation operation) -> base::Result<std::uint8_t> {
    switch (operation) {
        case access::HeapWalOperation::kInitialize:
            return wal::opcode::heap::kInitialize;
        case access::HeapWalOperation::kInsert:
            return wal::opcode::heap::kInsert;
        case access::HeapWalOperation::kMultiInsert:
            return wal::opcode::heap::kMultiInsert;
        case access::HeapWalOperation::kLock:
            return wal::opcode::heap::kLock;
        case access::HeapWalOperation::kUpdate:
            return wal::opcode::heap::kUpdate;
        case access::HeapWalOperation::kHotUpdate:
            return wal::opcode::heap::kHotUpdate;
        case access::HeapWalOperation::kDelete:
            return wal::opcode::heap::kDelete;
        case access::HeapWalOperation::kPrune:
            return wal::opcode::heap::kPrune;
    }
    return base::fail(payload_error(false, "map opcode", "unknown heap WAL operation"));
}

auto heap_wal_operation(const std::uint8_t info) -> base::Result<access::HeapWalOperation> {
    switch (info) {
        case wal::opcode::heap::kInitialize:
            return access::HeapWalOperation::kInitialize;
        case wal::opcode::heap::kInsert:
            return access::HeapWalOperation::kInsert;
        case wal::opcode::heap::kMultiInsert:
            return access::HeapWalOperation::kMultiInsert;
        case wal::opcode::heap::kLock:
            return access::HeapWalOperation::kLock;
        case wal::opcode::heap::kUpdate:
            return access::HeapWalOperation::kUpdate;
        case wal::opcode::heap::kHotUpdate:
            return access::HeapWalOperation::kHotUpdate;
        case wal::opcode::heap::kDelete:
            return access::HeapWalOperation::kDelete;
        case wal::opcode::heap::kPrune:
            return access::HeapWalOperation::kPrune;
        default:
            return base::fail(payload_error(true, "map opcode", "unknown heap WAL opcode"));
    }
}

auto validate_heap_wal_payload(const HeapWalPayload& payload, const bool decoding) -> base::Status {
    switch (payload.operation) {
        case access::HeapWalOperation::kInitialize:
            if (payload.page_flags != 0U || !payload.tuples.empty() || !payload.redirects.empty()
                || !payload.dead.empty() || !payload.unused.empty()) {
                return fail_payload(decoding, "validate initialize", "heap init WAL is not empty");
            }
            return {};
        case access::HeapWalOperation::kInsert:
            return validate_insert_payload(payload, false, decoding);
        case access::HeapWalOperation::kMultiInsert:
            return validate_insert_payload(payload, true, decoding);
        case access::HeapWalOperation::kLock:
        case access::HeapWalOperation::kUpdate:
        case access::HeapWalOperation::kDelete:
            return validate_metadata_payload(payload, decoding);
        case access::HeapWalOperation::kHotUpdate:
            return validate_hot_update_payload(payload, decoding);
        case access::HeapWalOperation::kPrune:
            return validate_prune_payload(payload, decoding);
    }
    return fail_payload(decoding, "validate payload", "unknown heap WAL operation");
}

}  // namespace detail

auto encode_heap_wal_payload(const HeapWalPayload& payload)
    -> base::Result<std::vector<std::byte>> {
    auto valid = detail::validate_heap_wal_payload(payload, false);
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    auto size = encoded_size(payload);
    if (!size) {
        return base::fail(std::move(size).error());
    }
    std::vector<std::byte> encoded(size.value());
    base::LittleEndianWriter writer{encoded};
    auto status = write_common_header(writer);
    if (status) {
        status = write_operation_payload(writer, payload);
    }
    if (!status || writer.position() != encoded.size()) {
        return base::fail(payload_error(
            false,
            "encode payload",
            "heap WAL encoder did not fill the exact payload"
        ));
    }
    return encoded;
}

auto decode_heap_wal_payload(const std::uint8_t info, const base::Bytes bytes)
    -> base::Result<HeapWalPayload> {
    if (bytes.size() < kHeapWalPayloadHeaderSize) {
        return base::fail(payload_error(true, "decode header", "heap WAL payload is truncated"));
    }
    base::LittleEndianReader reader{bytes};
    auto magic = reader.read_u32();
    auto version = reader.read_u16();
    auto header_size = reader.read_u16();
    if (!magic || !version || !header_size || magic.value() != kHeapWalPayloadMagic
        || version.value() != kHeapWalPayloadVersion
        || header_size.value() != kHeapWalPayloadHeaderSize) {
        return base::fail(payload_error(
            true,
            "decode header",
            "heap WAL magic, version, or header size is unsupported"
        ));
    }
    auto operation = detail::heap_wal_operation(info);
    if (!operation) {
        return base::fail(std::move(operation).error());
    }
    auto payload = read_operation_payload(operation.value(), reader);
    if (!payload) {
        return base::fail(std::move(payload).error());
    }
    if (auto finished = reader.finish(); !finished) {
        return base::fail(payload_error(true, "decode payload", "heap WAL has trailing bytes"));
    }
    auto valid = detail::validate_heap_wal_payload(payload.value(), true);
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    return payload.value();
}

}  // namespace db::recovery
