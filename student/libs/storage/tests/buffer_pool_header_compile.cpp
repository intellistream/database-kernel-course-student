#include <type_traits>

#include "db/storage/buffer_pool.hpp"

namespace {

template <typename Type>
concept ExposesBufferPoolImplementation = requires { typename Type::Impl; };

static_assert(!std::is_copy_constructible_v<db::storage::BufferPool>);
static_assert(!std::is_move_constructible_v<db::storage::BufferPool>);
static_assert(!ExposesBufferPoolImplementation<db::storage::BufferPool>);

}  // namespace
