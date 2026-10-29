#include <type_traits>

#include "detail/durable_metadata.hpp"

namespace {

static_assert(std::is_aggregate_v<db::storage::detail::DurableMetadata>);
static_assert(std::is_aggregate_v<db::storage::detail::MetadataReadOptions>);
static_assert(db::storage::detail::kDurableMetadataHeaderSize == 24U);

}  // namespace
