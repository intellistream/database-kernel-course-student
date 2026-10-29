#include "db/transaction/xid.hpp"

namespace {

[[maybe_unused]] void compile_xid_header() {
    db::transaction::XidGenerator generator;
    (void)generator.next_xid();
}

}  // namespace
