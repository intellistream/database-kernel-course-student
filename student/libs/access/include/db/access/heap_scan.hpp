#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "db/access/heap_context.hpp"
#include "db/access/heap_page.hpp"
#include "db/access/heap_table.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::access {

// Physical scans track the concrete access path. SQL sequential scans instead
// retain their qualification in transaction through a separate logical
// predicate SIREAD, so filter-rejected physical tuples do not become reads.
enum class HeapScanSireadMode : std::uint8_t {
    kPhysical,
    kLogicalPredicate,
};

// Access owns this raw-row port so a logical sequential scan can decide
// whether tuple-version provenance belongs to its SQL qualification without
// depending on execution. The borrower must outlive the scan and return a
// structured error. Escaped exceptions are converted at the scan boundary. It
// is queried only in logical-predicate mode, for visible old versions and
// physically invisible concurrent versions.
class HeapScanQualificationPort {
public:
    HeapScanQualificationPort(const HeapScanQualificationPort&) = delete;
    auto operator=(const HeapScanQualificationPort&) -> HeapScanQualificationPort& = delete;
    HeapScanQualificationPort(HeapScanQualificationPort&&) = delete;
    auto operator=(HeapScanQualificationPort&&) -> HeapScanQualificationPort& = delete;
    virtual ~HeapScanQualificationPort() = default;

    virtual auto matches(std::span<const std::byte> payload) const -> base::Result<bool> = 0;

protected:
    HeapScanQualificationPort() = default;
};

// Every referenced service must outlive the scan. Physical mode registers one
// relation SIREAD plus tuple reads. Logical-predicate mode requires the
// qualification port whenever SSI is active: engine registers the SQL-level
// predicate while access gates every version-provenance edge through that same
// qualification.
struct HeapScanContext final {
    HeapMvccContext mvcc{};
    HeapScanSireadMode siread_mode{HeapScanSireadMode::kPhysical};
    const HeapScanQualificationPort* logical_qualification{nullptr};
};

struct HeapScanTuple final {
    base::Rid rid{base::kInvalidRid};
    TupleHeader header;
    std::vector<std::byte> payload;

    auto operator==(const HeapScanTuple&) const -> bool = default;
};

// Move-only sequential heap scan. It fixes the relation page horizon at begin,
// captures one stable image under one content latch per page, then builds a
// bounded candidate batch after releasing the pin and latch.
class HeapScan final {
public:
    static auto begin(const HeapTable& table, HeapScanContext context) -> base::Result<HeapScan>;

    HeapScan(const HeapScan&) = delete;
    auto operator=(const HeapScan&) -> HeapScan& = delete;
    HeapScan(HeapScan&& other) noexcept;
    auto operator=(HeapScan&& other) noexcept -> HeapScan&;
    ~HeapScan();

    // Moving transfers the cursor and leaves the source as an inert value.
    auto valid() const noexcept -> bool;
    auto next() -> base::Result<std::optional<HeapScanTuple>>;
    // Reuses caller-owned payload capacity across rows. On end-of-scan the
    // output remains unchanged; on success it owns the same stable bytes that
    // next() would return. No frame pin or borrowed page view escapes this
    // call.
    auto next_into(HeapScanTuple& output) -> base::Result<bool>;
    auto relation_id() const noexcept -> base::RelationId;
    auto relation_page_horizon() const noexcept -> std::uint64_t;
    auto pages_loaded() const noexcept -> std::size_t;

private:
    struct Impl;

    explicit HeapScan(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::access
