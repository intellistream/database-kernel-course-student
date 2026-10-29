#include "db/access/heap_table.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "db/access/heap_page.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/storage/page.hpp"

namespace db::access {

namespace {

[[maybe_unused]] auto heap_table_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view message
) -> base::Error {
    return base::Error{code, std::string{message}}.with_context("access", std::string{operation});
}

[[maybe_unused]] auto inactive_heap_table(const std::string_view operation) -> base::Error {
    return heap_table_error(
        base::ErrorCode::kUnavailable,
        operation,
        "heap relation handle is inactive"
    );
}

[[maybe_unused]] auto
validate_descriptor(const base::RelationId relation_id, const std::uint8_t fillfactor)
    -> base::Status {
    if (relation_id == base::kInvalidRelationId) {
        return base::fail(heap_table_error(
            base::ErrorCode::kInvalidArgument,
            "open heap relation",
            "heap relation identifier must be valid"
        ));
    }
    if (fillfactor < kMinimumHeapFillfactor || fillfactor > 100U) {
        return base::fail(heap_table_error(
            base::ErrorCode::kInvalidArgument,
            "open heap relation",
            "heap fillfactor must be between 10 and 100"
        ));
    }
    return {};
}

[[maybe_unused]] auto clamp_free_space(const std::size_t free_space) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(
        std::min<std::size_t>(free_space, storage::kMaximumPageFreeSpace)
    );
}

[[maybe_unused]] auto page_count_or_zero(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const std::string_view operation
) -> base::Result<std::uint64_t> {
    auto count = pool.relation_page_count(relation_id);
    if (count) {
        return count.value();
    }
    if (count.error().has_system_error()
        && count.error().system_error() == std::errc::no_such_file_or_directory) {
        return 0U;
    }
    return base::fail(std::move(count).error().with_context("access", std::string{operation}));
}

}  // namespace

class HeapRelationSpace final {
public:
    explicit HeapRelationSpace(storage::FreeSpaceMap free_space_map_value)
        : free_space_map{std::move(free_space_map_value)} {}

    mutable std::mutex mutex;
    storage::FreeSpaceMap free_space_map;
    std::uint64_t page_count{0};
    bool initialized{false};
};

struct HeapSpaceRegistry::Impl final {
    mutable std::mutex mutex;
    std::unordered_map<base::RelationId, std::shared_ptr<HeapRelationSpace>> spaces;
};

HeapSpaceRegistry::HeapSpaceRegistry()
    : implementation_{std::make_unique<Impl>()} {}

HeapSpaceRegistry::~HeapSpaceRegistry() = default;

auto HeapSpaceRegistry::install(
    const base::RelationId relation_id,
    storage::FreeSpaceMap free_space_map
) -> base::Status {
    if (relation_id == base::kInvalidRelationId) {
        return base::fail(heap_table_error(
            base::ErrorCode::kInvalidArgument,
            "install heap relation space",
            "heap relation identifier must be valid"
        ));
    }
    if (!free_space_map.valid()) {
        return base::fail(heap_table_error(
            base::ErrorCode::kInvalidArgument,
            "install heap relation space",
            "free-space map must be active"
        ));
    }
    const std::lock_guard lock{implementation_->mutex};
    const auto [unused, inserted] = implementation_->spaces.emplace(
        relation_id,
        std::make_shared<HeapRelationSpace>(std::move(free_space_map))
    );
    static_cast<void>(unused);
    if (!inserted) {
        return base::fail(heap_table_error(
            base::ErrorCode::kAlreadyExists,
            "install heap relation space",
            "heap relation space is already registered"
        ));
    }
    return {};
}

auto HeapSpaceRegistry::remove(const base::RelationId relation_id) -> base::Status {
    const std::lock_guard lock{implementation_->mutex};
    if (implementation_->spaces.erase(relation_id) == 0U) {
        return base::fail(heap_table_error(
            base::ErrorCode::kNotFound,
            "remove heap relation space",
            "heap relation space is not registered"
        ));
    }
    return {};
}

auto HeapSpaceRegistry::contains(const base::RelationId relation_id) const -> bool {
    const std::lock_guard lock{implementation_->mutex};
    return implementation_->spaces.contains(relation_id);
}

auto HeapSpaceRegistry::size() const -> std::size_t {
    const std::lock_guard lock{implementation_->mutex};
    return implementation_->spaces.size();
}

// LAB-S01.Q10:R01-BEGIN
[[maybe_unused]] auto HeapSpaceRegistry::resolve(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const std::uint64_t page_count
) -> base::Result<std::shared_ptr<HeapRelationSpace>> {
    static_cast<void>(relation_id);
    static_cast<void>(page_count);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q10:R01: algorithm not implemented"
    ));
}

// LAB-S01.Q10:R01-END

namespace {

struct HeapTableState {
    HeapTableState(
        const base::RelationId relation_id_value,
        storage::BufferPool& pool_value,
        std::shared_ptr<HeapRelationSpace> space_value,
        HeapMutationLogPort& mutation_log_value,
        const std::uint8_t fillfactor_value
    )
        : relation_id{relation_id_value},
          pool{&pool_value},
          space{std::move(space_value)},
          mutation_log{&mutation_log_value},
          fillfactor{fillfactor_value} {}

    base::RelationId relation_id;
    storage::BufferPool* pool;
    std::shared_ptr<HeapRelationSpace> space;
    HeapMutationLogPort* mutation_log;
    std::uint8_t fillfactor;
};

}  // namespace

struct HeapTable::Impl final : HeapTableState {
    using HeapTableState::HeapTableState;
};

namespace {

struct HeapTupleSpace final {
    std::uint16_t tuple_bytes;
    std::uint16_t physical_bytes;
};

[[maybe_unused]] auto relation_reserved_space(const HeapTableState& table) noexcept
    -> std::uint16_t {
    const auto percent = static_cast<std::size_t>(100U - table.fillfactor);
    return static_cast<std::uint16_t>((storage::kPageSize * percent) / 100U);
}

[[maybe_unused]] auto validate_insert_xid(const base::Xid xid) -> base::Status {
    if (xid == base::kInvalidXid || xid.value() == std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(heap_table_error(
            base::ErrorCode::kInvalidArgument,
            "insert heap tuple",
            "heap insert requires a normal transaction identifier"
        ));
    }
    return {};
}

[[maybe_unused]] auto tuple_space(const base::Bytes payload) -> base::Result<HeapTupleSpace> {
    constexpr auto k_maximum_payload =
        storage::kMaximumPageFreeSpace - kTupleHeaderSize - kLinePointerSize;
    if (payload.size() > k_maximum_payload) {
        return base::fail(heap_table_error(
            base::ErrorCode::kResourceExhausted,
            "insert heap tuple",
            "heap tuple cannot fit on one page"
        ));
    }
    const auto tuple_bytes = kTupleHeaderSize + payload.size();
    return HeapTupleSpace{
        .tuple_bytes = static_cast<std::uint16_t>(tuple_bytes),
        .physical_bytes = static_cast<std::uint16_t>(tuple_bytes + kLinePointerSize),
    };
}

[[maybe_unused]] auto log_page_mutation(
    [[maybe_unused]] HeapTableState& table,
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::Page* const before_page,
    [[maybe_unused]] HeapWalMutation mutation
) -> base::Result<base::Lsn> {
    // LAB-S10.Q06:R1001-BEGIN
    base::lab::missing_algorithm("S10.Q06:R1001");
    // LAB-S10.Q06:R1001-END
}

// LAB-S01.Q10:R02-BEGIN
[[maybe_unused]] auto
extend_page_locked([[maybe_unused]] HeapTableState& table, [[maybe_unused]] const base::Xid xid)
    -> base::Result<base::PageId> {
    static_cast<void>(table);
    static_cast<void>(xid);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q10:R02: algorithm not implemented"
    ));
}

// LAB-S01.Q10:R02-END

[[maybe_unused]] auto extend_and_insert_rows_locked(
    [[maybe_unused]] HeapTableState& table,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const std::span<const base::Bytes> rows,
    [[maybe_unused]] const HeapWalOperation operation
) -> base::Result<std::vector<base::Rid>> {
    // LAB-S01.Q08:R2004-BEGIN
    base::lab::missing_algorithm("S01.Q08:R2004");
    // LAB-S01.Q08:R2004-END
}

// LAB-S01.Q08:R01-BEGIN
[[maybe_unused]] auto fsm_candidate(
    [[maybe_unused]] HeapTableState& table,
    [[maybe_unused]] const std::uint16_t tuple_bytes,
    [[maybe_unused]] const bool space_lock_held
) -> base::Result<std::optional<base::PageId>> {
    static_cast<void>(table);
    static_cast<void>(tuple_bytes);
    static_cast<void>(space_lock_held);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R01: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R01-END

// LAB-S01.Q08:R02-BEGIN
[[maybe_unused]] auto try_insert_rows_on_page(
    [[maybe_unused]] HeapTableState& table,
    [[maybe_unused]] const base::PageId page_id,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const std::span<const base::Bytes> rows,
    [[maybe_unused]] const HeapWalOperation operation,
    [[maybe_unused]] const bool newly_initialized
) -> base::Result<std::vector<base::Rid>> {
    static_cast<void>(table);
    static_cast<void>(page_id);
    static_cast<void>(xid);
    static_cast<void>(command_id);
    static_cast<void>(rows);
    static_cast<void>(operation);
    static_cast<void>(newly_initialized);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R02: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R02-END

[[maybe_unused]] void
append_rids(std::vector<base::Rid>& destination, std::vector<base::Rid> source) {
    destination.insert(
        destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end())
    );
}

// LAB-S01.Q08:R03-BEGIN
[[maybe_unused]] auto try_candidate(
    [[maybe_unused]] HeapTableState& table,
    [[maybe_unused]] const std::optional<base::PageId> candidate,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const std::span<const base::Bytes> rows,
    [[maybe_unused]] const HeapWalOperation operation
) -> base::Result<std::vector<base::Rid>> {
    static_cast<void>(table);
    static_cast<void>(candidate);
    static_cast<void>(xid);
    static_cast<void>(command_id);
    static_cast<void>(rows);
    static_cast<void>(operation);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R03: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R03-END

// LAB-S01.Q08:R04-BEGIN
[[maybe_unused]] auto insert_under_extension_lock(
    [[maybe_unused]] HeapTableState& table,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const std::span<const base::Bytes> rows,
    [[maybe_unused]] const HeapWalOperation operation,
    [[maybe_unused]] const std::uint16_t first_tuple_bytes
) -> base::Result<std::vector<base::Rid>> {
    static_cast<void>(table);
    static_cast<void>(xid);
    static_cast<void>(command_id);
    static_cast<void>(rows);
    static_cast<void>(operation);
    static_cast<void>(first_tuple_bytes);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R04: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R04-END

[[maybe_unused]] auto validate_insert_rows(
    const base::Xid xid,
    const std::span<const base::Bytes> rows,
    const bool bounded_batch
) -> base::Status {
    auto valid_xid = validate_insert_xid(xid);
    if (!valid_xid) {
        return valid_xid;
    }
    if (bounded_batch && rows.size() > kMaximumHeapMultiInsertRows) {
        return base::fail(heap_table_error(
            base::ErrorCode::kResourceExhausted,
            "multi-insert heap tuples",
            "heap multi-insert row bound is exceeded"
        ));
    }
    std::size_t payload_bytes = 0;
    for (const auto payload : rows) {
        auto space = tuple_space(payload);
        if (!space) {
            return base::fail(std::move(space).error());
        }
        if (bounded_batch && payload.size() > kMaximumHeapMultiInsertPayloadBytes - payload_bytes) {
            return base::fail(heap_table_error(
                base::ErrorCode::kResourceExhausted,
                "multi-insert heap tuples",
                "heap multi-insert payload bound is exceeded"
            ));
        }
        payload_bytes += payload.size();
    }
    return {};
}

// LAB-S01.Q08:R05-BEGIN
[[maybe_unused]] auto insert_rows(
    [[maybe_unused]] HeapTableState& table,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const std::span<const base::Bytes> rows,
    [[maybe_unused]] const HeapWalOperation operation
) -> base::Result<std::vector<base::Rid>> {
    static_cast<void>(table);
    static_cast<void>(xid);
    static_cast<void>(command_id);
    static_cast<void>(rows);
    static_cast<void>(operation);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R05: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R05-END

}  // namespace

HeapTable::HeapTable(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

HeapTable::HeapTable(HeapTable&& other) noexcept = default;
auto HeapTable::operator=(HeapTable&& other) noexcept -> HeapTable& = default;
HeapTable::~HeapTable() = default;

// LAB-S01.Q10:R03-BEGIN
[[maybe_unused]] auto HeapTable::create(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] HeapSpaceRegistry& space_registry,
    [[maybe_unused]] HeapMutationLogPort& mutation_log,
    [[maybe_unused]] const std::uint8_t fillfactor
) -> base::Result<HeapTable> {
    static_cast<void>(relation_id);
    static_cast<void>(pool);
    static_cast<void>(space_registry);
    static_cast<void>(mutation_log);
    static_cast<void>(fillfactor);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q10:R03: algorithm not implemented"
    ));
}

// LAB-S01.Q10:R03-END

// LAB-S01.Q10:R04-BEGIN
[[maybe_unused]] auto HeapTable::open(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] storage::BufferPool& pool,
    [[maybe_unused]] HeapSpaceRegistry& space_registry,
    [[maybe_unused]] HeapMutationLogPort& mutation_log,
    [[maybe_unused]] const std::uint8_t fillfactor
) -> base::Result<HeapTable> {
    static_cast<void>(relation_id);
    static_cast<void>(pool);
    static_cast<void>(space_registry);
    static_cast<void>(mutation_log);
    static_cast<void>(fillfactor);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q10:R04: algorithm not implemented"
    ));
}

// LAB-S01.Q10:R04-END

auto HeapTable::valid() const noexcept -> bool {
    return implementation_ != nullptr;
}

auto HeapTable::relation_id() const noexcept -> base::RelationId {
    if (!valid()) {
        return base::kInvalidRelationId;
    }
    return implementation_->relation_id;
}

auto HeapTable::fillfactor() const noexcept -> std::uint8_t {
    return valid() ? implementation_->fillfactor : 0U;
}

auto HeapTable::reserved_update_space() const noexcept -> std::uint16_t {
    return valid() ? relation_reserved_space(*implementation_) : 0U;
}

auto HeapTable::page_count() const -> std::uint64_t {
    if (!valid()) {
        return 0U;
    }
    const std::lock_guard lock{implementation_->space->mutex};
    return implementation_->space->page_count;
}

auto HeapTable::page_ids() const -> std::vector<base::PageId> {
    if (!valid()) {
        return {};
    }
    const std::lock_guard lock{implementation_->space->mutex};
    std::vector<base::PageId> result;
    result.reserve(implementation_->space->page_count);
    for (std::uint64_t page = 0; page < implementation_->space->page_count; ++page) {
        result.emplace_back(static_cast<std::uint32_t>(page));
    }
    return result;
}

auto HeapTable::is_physically_empty() const -> base::Result<bool> {
    if (!valid()) {
        return base::fail(inactive_heap_table("check physical heap emptiness"));
    }
    const auto count = page_count();
    for (std::uint64_t page = 0; page < count; ++page) {
        const auto address = storage::PageAddress{
            .relation_id = implementation_->relation_id,
            .page_id = base::PageId{static_cast<std::uint32_t>(page)},
        };
        auto read = implementation_->pool->read_page(address);
        if (!read) {
            return base::fail(
                std::move(read).error().with_context("access", "check physical heap emptiness")
            );
        }
        auto header = read->page()->header();
        if (!header) {
            return base::fail(
                std::move(header).error().with_context("access", "check physical heap emptiness")
            );
        }
        if (header->kind != storage::PageKind::kHeap || header->special != storage::kPageSize
            || header->lower < storage::kPageHeaderSize
            || (header->lower - storage::kPageHeaderSize) % kLinePointerSize != 0U) {
            return base::fail(heap_table_error(
                base::ErrorCode::kCorruption,
                "check physical heap emptiness",
                "heap page header is invalid"
            ));
        }
        if (header->lower != storage::kPageHeaderSize) {
            return false;
        }
    }
    return true;
}

// LAB-S01.Q10:R05-BEGIN
[[maybe_unused]] auto HeapTable::ensure_initial_page() -> base::Result<base::PageId> {
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q10:R05: algorithm not implemented"
    ));
}

// LAB-S01.Q10:R05-END

// LAB-S01.Q10:R06-BEGIN
[[maybe_unused]] auto HeapTable::extend_page() -> base::Result<base::PageId> {
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q10:R06: algorithm not implemented"
    ));
}

// LAB-S01.Q10:R06-END

// LAB-S01.Q08:R06-BEGIN
[[maybe_unused]] auto HeapTable::insert(
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const base::Bytes payload
) -> base::Result<base::Rid> {
    static_cast<void>(xid);
    static_cast<void>(command_id);
    static_cast<void>(payload);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R06: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R06-END

// LAB-S01.Q08:R07-BEGIN
[[maybe_unused]] auto HeapTable::multi_insert(
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const std::span<const base::Bytes> rows
) -> base::Result<std::vector<base::Rid>> {
    static_cast<void>(xid);
    static_cast<void>(command_id);
    static_cast<void>(rows);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R07: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R07-END

// LAB-S01.Q08:R08-BEGIN
[[maybe_unused]] auto HeapTable::find_page_with_space(
    [[maybe_unused]] const std::uint16_t tuple_bytes,
    [[maybe_unused]] const HeapSpacePolicy policy
) -> base::Result<std::optional<base::PageId>> {
    static_cast<void>(tuple_bytes);
    static_cast<void>(policy);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R08: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R08-END

// LAB-S01.Q08:R09-BEGIN
[[maybe_unused]] auto HeapTable::refresh_free_space([[maybe_unused]] const base::PageId page_id)
    -> base::Status {
    static_cast<void>(page_id);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R09: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R09-END

// LAB-S01.Q08:R10-BEGIN
[[maybe_unused]] auto HeapTable::record_free_space(
    [[maybe_unused]] const base::PageId page_id,
    [[maybe_unused]] const std::uint16_t free_bytes
) const -> base::Status {
    static_cast<void>(page_id);
    static_cast<void>(free_bytes);
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q08:R10: algorithm not implemented"
    ));
}

// LAB-S01.Q08:R10-END

// LAB-S01.Q10:R07-BEGIN
[[maybe_unused]] auto HeapTable::persist_free_space_map() -> base::Status {
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q10:R07: algorithm not implemented"
    ));
}

// LAB-S01.Q10:R07-END

// LAB-S01.Q10:R08-BEGIN
[[maybe_unused]] auto HeapTable::sync_relation() -> base::Status {
    return base::fail(heap_table_error(
        base::ErrorCode::kUnsupported,
        "student lab",
        "S01.Q10:R08: algorithm not implemented"
    ));
}

// LAB-S01.Q10:R08-END

auto HeapTable::buffer_pool() const noexcept -> storage::BufferPool& {
    return *implementation_->pool;
}

auto HeapTable::mutation_log() const noexcept -> HeapMutationLogPort& {
    return *implementation_->mutation_log;
}

}  // namespace db::access
