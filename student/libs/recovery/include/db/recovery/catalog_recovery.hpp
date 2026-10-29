#pragma once

#include <cstdint>

#include "db/base/result.hpp"
#include "db/catalog/store.hpp"
#include "db/recovery/durable_state_redo.hpp"

namespace db::recovery {

// Production bridge for the narrow durable-state port. CatalogStore keeps its
// FCAT generation atomic; recovery asks only that its next OID never move
// backwards from a checkpoint-observed durable frontier.
class CatalogStoreRecoveryAdapter final : public CatalogRecoveryPort {
public:
    explicit CatalogStoreRecoveryAdapter(catalog::CatalogStore& store) noexcept;

    auto ensure_next_object_id(std::uint32_t next_object_id) -> base::Status override;

private:
    catalog::CatalogStore& store_;
};

}  // namespace db::recovery
