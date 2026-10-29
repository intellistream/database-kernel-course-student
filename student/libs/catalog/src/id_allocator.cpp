#include "id_allocator.hpp"

#include <limits>
#include <string>
#include <string_view>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::catalog {
namespace {

constexpr std::uint64_t kFirstId = 1;
constexpr std::uint64_t kExhaustedFrontier =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;

[[maybe_unused]] auto valid_frontier(const std::uint64_t frontier) noexcept -> bool {
    return frontier >= kFirstId && frontier <= kExhaustedFrontier;
}

template <typename Identifier>
[[maybe_unused]] auto
allocate([[maybe_unused]] std::uint64_t& frontier, [[maybe_unused]] const std::string_view domain)
    -> base::Result<Identifier> {
    // LAB-S02.Q10:R01-BEGIN
    base::lab::missing_algorithm("S02.Q10:R01");
    // LAB-S02.Q10:R01-END
}
}  // namespace

[[maybe_unused]] CatalogIdAllocator::CatalogIdAllocator(
    [[maybe_unused]] const CatalogIdState state
) noexcept
    : state_{state} {
    // LAB-S02.Q10:R02-BEGIN
    base::lab::missing_algorithm("S02.Q10:R02");
    // LAB-S02.Q10:R02-END
}

[[maybe_unused]] CatalogIdAllocator::CatalogIdAllocator(
    [[maybe_unused]] CatalogIdAllocator&& other
) noexcept
    : state_{other.state_} {
    // LAB-S02.Q10:R03-BEGIN
    base::lab::missing_algorithm("S02.Q10:R03");
    // LAB-S02.Q10:R03-END
}

auto CatalogIdAllocator::operator=(CatalogIdAllocator&& other) noexcept -> CatalogIdAllocator& {
    if (this != &other) {
        state_ = other.state_;
        other.state_ = CatalogIdState{
            .next_oid = kExhaustedFrontier,
            .next_relation_id = kExhaustedFrontier,
            .next_index_id = kExhaustedFrontier,
        };
    }
    return *this;
}

[[maybe_unused]] auto CatalogIdAllocator::restore([[maybe_unused]] const CatalogIdState state)
    -> base::Result<CatalogIdAllocator> {
    // LAB-S02.Q10:R04-BEGIN
    base::lab::missing_algorithm("S02.Q10:R04");
    // LAB-S02.Q10:R04-END
}

[[maybe_unused]] auto CatalogIdAllocator::allocate_oid() -> base::Result<base::Oid> {
    // LAB-S02.Q10:R05-BEGIN
    base::lab::missing_algorithm("S02.Q10:R05");
    // LAB-S02.Q10:R05-END
}

[[maybe_unused]] auto CatalogIdAllocator::allocate_relation_id() -> base::Result<base::RelationId> {
    // LAB-S02.Q10:R06-BEGIN
    base::lab::missing_algorithm("S02.Q10:R06");
    // LAB-S02.Q10:R06-END
}

[[maybe_unused]] auto CatalogIdAllocator::allocate_index_id() -> base::Result<base::IndexId> {
    // LAB-S02.Q10:R07-BEGIN
    base::lab::missing_algorithm("S02.Q10:R07");
    // LAB-S02.Q10:R07-END
}

auto CatalogIdAllocator::state() const noexcept -> CatalogIdState {
    return state_;
}

}  // namespace db::catalog
