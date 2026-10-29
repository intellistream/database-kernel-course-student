#pragma once

#include <cstdint>

namespace db::wal {

// Stable dispatch identifiers carried by every durable WAL record. Resource
// manager payloads remain owned and interpreted by their recovery adapters.
enum class WalResourceManager : std::uint8_t {
    kSystem = 0,
    kTransaction = 1,
    kHeap = 2,
    kBtree = 3,
    kCatalog = 4,
};

}  // namespace db::wal
