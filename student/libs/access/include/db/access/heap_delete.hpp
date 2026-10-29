#pragma once

#include <variant>

#include "db/access/heap_update.hpp"
#include "db/base/identifier.hpp"

namespace db::access {

struct HeapDeleteRequest final {
    base::Rid target{base::kInvalidRid};
};

struct HeapDeleteSuccess final {
    // Redirects may make this differ from the candidate RID.
    base::Rid deleted_rid{base::kInvalidRid};

    auto operator==(const HeapDeleteSuccess&) const -> bool = default;
};

using HeapDeleteOutcome = std::variant<HeapDeleteSuccess, HeapMutationRejected>;

}  // namespace db::access
