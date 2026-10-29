#include "db/access/dead_tid_store.hpp"

namespace db::access::header_compile {

void consume_dead_tid_store(const DeadTidStore& store) {
    static_cast<void>(store.empty());
}

}  // namespace db::access::header_compile
