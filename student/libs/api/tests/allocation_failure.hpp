#pragma once

#include <cstddef>

namespace db::api::tests {

void fail_allocation_after(std::size_t successful_allocations) noexcept;
void stop_failing_allocations() noexcept;

}  // namespace db::api::tests
