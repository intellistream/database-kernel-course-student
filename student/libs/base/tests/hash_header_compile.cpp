#include <cstddef>

#include "db/base/hash.hpp"

constexpr std::size_t kFirstSeed = 7U;
constexpr std::size_t kSecondSeed = 11U;

static_assert(noexcept(db::base::combine_hash(kFirstSeed, kSecondSeed)));
static_assert(
    db::base::combine_hash(kFirstSeed, kSecondSeed)
    != db::base::combine_hash(kSecondSeed, kFirstSeed)
);
