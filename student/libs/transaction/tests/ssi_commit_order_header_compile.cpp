#include "ssi_commit_order.hpp"

namespace {

[[maybe_unused]] void compile_ssi_commit_order_header() {
    auto sequence = std::uint64_t{1};
    const auto advanced = db::transaction::detail::advance_ssi_commit_sequence(sequence);
    (void)advanced;
}

}  // namespace
