#include "allocation_failure.hpp"

#include <cstdlib>
#include <new>

namespace db::api::tests {
namespace {

// This test executable injects one failure on the calling thread only.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
thread_local bool enabled = false;
thread_local std::size_t remaining = 0U;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

void fail_allocation_after(const std::size_t successful_allocations) noexcept {
    remaining = successful_allocations;
    enabled = true;
}

void stop_failing_allocations() noexcept {
    enabled = false;
}

void check_allocation() {
    if (enabled) {
        if (remaining == 0U) {
            // Error construction must be allowed after the injected failure.
            enabled = false;
            throw std::bad_alloc{};
        }
        --remaining;
    }
}

}  // namespace db::api::tests

// Only ordinary allocations used by the tested schema, vectors and strings.
// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
auto operator new(const std::size_t bytes) -> void* {
    db::api::tests::check_allocation();
    auto* allocation = std::malloc(bytes == 0U ? 1U : bytes);
    if (allocation == nullptr) {
        throw std::bad_alloc{};
    }
    return allocation;
}

auto operator new[](const std::size_t bytes) -> void* {
    return ::operator new(bytes);
}

auto operator new(const std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept -> void* {
    try {
        return ::operator new(bytes);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

auto operator new[](const std::size_t bytes, const std::nothrow_t& tag) noexcept -> void* {
    return ::operator new(bytes, tag);
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

void operator delete(void* allocation, const std::nothrow_t& /*tag*/) noexcept {
    ::operator delete(allocation);
}

void operator delete[](void* allocation, const std::nothrow_t& /*tag*/) noexcept {
    ::operator delete(allocation);
}

// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
