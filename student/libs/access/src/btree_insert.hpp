#pragma once
#include "db/base/lab_missing.hpp"

#include <optional>

#include "db/access/btree_key.hpp"
#include "db/access/btree_page.hpp"
#include "db/access/btree_wal.hpp"
#include "db/access/index_tid.hpp"
#include "db/base/result.hpp"
#include "db/storage/buffer_pool.hpp"

namespace db::access::detail {

struct BtreeUniqueCheck final {
    bool enforce{false};
    const IndexTidClassifier* classifier{nullptr};
    const UniqueConflictWaiter* waiter{nullptr};

    auto heap_aware() const noexcept -> bool 
{
        return classifier != nullptr;
    }

};

struct BtreeInsertRequest final {
    base::RelationId relation_id{base::kInvalidRelationId};
    const BtreeKeyLayout* key_layout{nullptr};
    storage::BufferPool* pool{nullptr};
    std::optional<BtreeRoot> root_hint;
    const EncodedBtreeKey* key{nullptr};
    base::Rid heap_rid{base::kInvalidRid};
    BtreeUniqueCheck unique_check;
    BtreeMutationLogPort* mutation_log{nullptr};
    base::Xid writer_xid{base::kInvalidXid};
    transaction::SerializableConflictManager* serializable_conflicts{nullptr};
    bool serializable_writer{false};
};

auto insert_btree_entry(BtreeInsertRequest request) -> base::Result<BtreeRoot>;

}  // namespace db::access::detail
