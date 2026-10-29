#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include "db/access/heap_context.hpp"
#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/visibility.hpp"

namespace db::access {

enum class HeapUpdateKind : std::uint8_t {
    kHot,
    kCold,
};

// The executor owns indexed-column comparison. The heap may use HOT only when
// this proof is true and the new version fits on the old page.
struct HeapUpdateRequest final {
    base::Rid target{base::kInvalidRid};
    base::Bytes payload{};
    bool indexed_columns_unchanged{false};
};

// Borrowed transaction services. They must outlive HeapTable::update().
struct HeapMutationContext final {
    HeapMvccContext mvcc{};
    std::uint32_t command_id{0};
};

using HeapUpdateContext = HeapMutationContext;

struct HeapUpdateSuccess final {
    base::Rid rid{base::kInvalidRid};
    HeapUpdateKind kind{HeapUpdateKind::kCold};

    auto index_maintenance_required() const noexcept -> bool {
        return kind == HeapUpdateKind::kCold;
    }

    auto operator==(const HeapUpdateSuccess&) const -> bool = default;
};

struct HeapMutationRejected final {
    transaction::TupleModificationDecision decision{};
    // Carries the current ctid for updated/self-modified versions so an upper
    // layer can perform EvalPlanQual-style wait/recheck processing.
    std::optional<base::Rid> newer_rid{};

    auto operator==(const HeapMutationRejected&) const -> bool = default;
};

using HeapUpdateRejected = HeapMutationRejected;

using HeapUpdateOutcome = std::variant<HeapUpdateSuccess, HeapUpdateRejected>;

}  // namespace db::access
