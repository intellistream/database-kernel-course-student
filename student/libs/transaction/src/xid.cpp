#include "db/transaction/xid.hpp"

#include <cstddef>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/proc_slot.hpp"

#include "xid_assignment.hpp"

namespace db::transaction {
namespace {

constexpr std::uint32_t kXidHalfRange = std::uint32_t{1} << 31U;
constexpr std::size_t kXidGenerationSpinLimit = 64U;

class XidGenerationGuard final {
public:
    explicit XidGenerationGuard(std::mutex& mutex)
        : mutex_{mutex} {
        for (std::size_t spin = 0U; spin < kXidGenerationSpinLimit; ++spin) {
            if (mutex_.try_lock()) {
                return;
            }
            __builtin_ia32_pause();
        }
        mutex_.lock();
    }

    ~XidGenerationGuard() {
        mutex_.unlock();
    }

    XidGenerationGuard(const XidGenerationGuard&) = delete;
    auto operator=(const XidGenerationGuard&) -> XidGenerationGuard& = delete;
    XidGenerationGuard(XidGenerationGuard&&) = delete;
    auto operator=(XidGenerationGuard&&) -> XidGenerationGuard& = delete;

private:
    std::mutex& mutex_;
};

[[maybe_unused]] auto xid_error(base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "transaction/xid",
        std::move(operation)
    );
}

}  // namespace

[[maybe_unused]] auto
compare_xids([[maybe_unused]] base::Xid left, [[maybe_unused]] base::Xid right)
    -> base::Result<XidOrder> {
    // LAB-S08.Q02:R01-BEGIN
    base::lab::missing_algorithm("S08.Q02:R01");
    // LAB-S08.Q02:R01-END
}

[[maybe_unused]] auto XidGenerator::from_next_xid([[maybe_unused]] base::Xid next_xid)
    -> base::Result<std::unique_ptr<XidGenerator>> {
    // LAB-S08.Q02:R02-BEGIN
    base::lab::missing_algorithm("S08.Q02:R02");
    // LAB-S08.Q02:R02-END
}

[[maybe_unused]] auto XidGenerator::allocate() -> base::Result<base::Xid> {
    // LAB-S08.Q02:R03-BEGIN
    base::lab::missing_algorithm("S08.Q02:R03");
    // LAB-S08.Q02:R03-END
}

[[maybe_unused]] auto XidGenerator::allocate_prepared(
    [[maybe_unused]] detail::XidAssignmentPreparation& preparation,
    [[maybe_unused]] ProcSlot& slot
) -> base::Result<base::Xid> {
    // LAB-S08.Q02:R04-BEGIN
    base::lab::missing_algorithm("S08.Q02:R04");
    // LAB-S08.Q02:R04-END
}

auto XidGenerator::next_xid() const -> base::Xid {
    const XidGenerationGuard lock{generation_mutex_};
    return next_xid_;
}

auto XidGenerator::remaining_before_wrap() const -> std::uint64_t {
    const XidGenerationGuard lock{generation_mutex_};
    return static_cast<std::uint64_t>(kXidWrapBoundary.value() - next_xid_.value());
}

XidGenerator::XidGenerator(base::Xid next_xid) noexcept
    : next_xid_{next_xid} {}

}  // namespace db::transaction
