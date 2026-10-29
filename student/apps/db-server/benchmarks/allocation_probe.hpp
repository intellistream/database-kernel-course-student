#pragma once

#include <cstddef>

namespace db::server::benchmarks {

struct AllocationCounts final {
    std::size_t calls{0U};
    std::size_t bytes{0U};
};

// Single-threaded benchmark probe. Enable only around an untimed operation;
// setup, validation, teardown and benchmark framework allocations are excluded.
void begin_allocation_probe() noexcept;
auto end_allocation_probe() noexcept -> AllocationCounts;

}  // namespace db::server::benchmarks
