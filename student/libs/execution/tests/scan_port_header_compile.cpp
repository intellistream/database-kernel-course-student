#include <type_traits>

#include "db/execution/scan_port.hpp"

namespace {

static_assert(std::is_copy_constructible_v<db::execution::SeqScanRequest>);
static_assert(std::is_copy_constructible_v<db::execution::IndexScanRequest>);
static_assert(!std::is_copy_constructible_v<db::execution::ScanPort>);

}  // namespace
