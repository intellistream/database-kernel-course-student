#pragma once

#include <cstddef>

namespace db::access {
class BtreeMutationLogPort;
class HeapMutationLogPort;
class HeapSpaceRegistry;
}  // namespace db::access

namespace db::catalog {
class CatalogStore;
}  // namespace db::catalog

namespace db::storage {
class BufferPool;
}  // namespace db::storage

namespace db::transaction {
class TransactionManager;
}  // namespace db::transaction

namespace db::engine::detail {

// Borrowed physical services assembled only by Database. Query, COPY, and
// utility execution extend this value with workflow-specific state instead of
// maintaining independently drifting service lists. Every reference is
// required and remains valid for the enclosing Database::Impl lifetime.
struct PhysicalServices final {
    std::size_t maximum_encoded_row_size{0U};
    catalog::CatalogStore& catalog;
    transaction::TransactionManager& transaction_manager;
    storage::BufferPool& buffer_pool;
    access::HeapSpaceRegistry& heap_space_registry;
    access::HeapMutationLogPort& heap_mutation_log;
    access::BtreeMutationLogPort& btree_mutation_log;
};

}  // namespace db::engine::detail
