#include <concepts>

#include "db/io/posix.hpp"

static_assert(!std::copy_constructible<db::io::PosixFile>);
static_assert(std::movable<db::io::PosixFile>);
static_assert(!std::copy_constructible<db::io::PosixDirectory>);
static_assert(std::movable<db::io::PosixDirectory>);
