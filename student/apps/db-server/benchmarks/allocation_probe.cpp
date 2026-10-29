#include "allocation_probe.hpp"

#include <cstdlib>
#include <new>

namespace db::server::benchmarks {
namespace {

// Benchmark-local allocation instrumentation requires mutable per-thread state.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
thread_local bool probing = false;
thread_local AllocationCounts counts;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

void begin_allocation_probe() noexcept {
    counts = {};
    probing = true;
}

auto end_allocation_probe() noexcept -> AllocationCounts {
    probing = false;
    return counts;
}

void record_allocation(const std::size_t bytes) noexcept {
    if (probing) {
        ++counts.calls;
        counts.bytes += bytes;
    }
}

}  // namespace db::server::benchmarks

// Only this standalone executable replaces ordinary new/delete. The measured
// vectors and strings use ordinary alignment; these counts do not claim to cover
// malloc, over-aligned allocations, or allocations made by other threads.
// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
auto operator new(const std::size_t bytes) -> void* {
    auto* allocation = std::malloc(bytes == 0U ? 1U : bytes);
    if (allocation == nullptr) {
        throw std::bad_alloc{};
    }
    db::server::benchmarks::record_allocation(bytes);
    return allocation;
}

auto operator new[](const std::size_t bytes) -> void* {
    return ::operator new(bytes);
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation) noexcept {
    ::operator delete(allocation);
}

void operator delete(void* allocation, std::size_t /*bytes*/) noexcept {
    ::operator delete(allocation);
}

void operator delete[](void* allocation, std::size_t /*bytes*/) noexcept {
    ::operator delete(allocation);
}

// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
