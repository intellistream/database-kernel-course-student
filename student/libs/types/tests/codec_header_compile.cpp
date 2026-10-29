#include <concepts>
#include <utility>
#include <vector>

#include "db/types/codec.hpp"

static_assert(std::same_as<
              decltype(db::types::heap_encode(
                  std::declval<const db::types::Value&>(),
                  std::declval<db::types::DataType>()
              )),
              db::base::Result<std::vector<std::byte>>>);
static_assert(std::same_as<
              decltype(db::types::PreparedHeapDecoder::create(std::declval<db::types::DataType>())),
              db::base::Result<db::types::PreparedHeapDecoder>>);
static_assert(std::same_as<
              decltype(std::declval<const db::types::PreparedHeapDecoder&>()
                           .decode_exact(std::declval<db::base::Bytes>())),
              db::base::Result<db::types::Value>>);
static_assert(std::same_as<
              decltype(db::types::key_compare(
                  std::declval<db::base::Bytes>(),
                  std::declval<db::base::Bytes>(),
                  std::declval<db::types::DataType>()
              )),
              db::base::Result<std::strong_ordering>>);
