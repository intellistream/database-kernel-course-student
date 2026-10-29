#include <type_traits>

#include "db/access/heap_table.hpp"

namespace {

template <typename Type>
concept ExposesImplementation = requires { typename Type::Impl; };

template <typename Type>
concept ExposesVacuumOperation = requires { typename Type::VacuumOperation; };

template <typename Type>
concept ExposesIndexCleanupOperation = requires { typename Type::IndexCleanupOperation; };

static_assert(std::is_nothrow_move_constructible_v<db::access::HeapTable>);
static_assert(std::is_nothrow_move_assignable_v<db::access::HeapTable>);
static_assert(!std::is_copy_constructible_v<db::access::HeapTable>);
static_assert(!std::is_move_constructible_v<db::access::HeapSpaceRegistry>);
static_assert(!ExposesImplementation<db::access::HeapTable>);
static_assert(!ExposesImplementation<db::access::HeapSpaceRegistry>);
static_assert(!ExposesVacuumOperation<db::access::HeapTable>);
static_assert(!ExposesIndexCleanupOperation<db::access::HeapTable>);

}  // namespace
