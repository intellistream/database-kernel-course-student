#include "db/access/heap_delete.hpp"

namespace {

[[maybe_unused]] void heap_delete_header_compile() {
    const db::access::HeapDeleteOutcome outcome{db::access::HeapDeleteSuccess{}};
    static_cast<void>(outcome);
}

}  // namespace
