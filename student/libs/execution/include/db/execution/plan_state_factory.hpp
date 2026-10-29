#pragma once

#include <memory>

#include "db/base/result.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// Composition-owned port used when an execution state contains a physical
// child plan. The factory is consulted only while the parent state is being
// constructed; the resulting tree owns every child state and never retains
// this interface. Engine/recovery can therefore assemble scans and access
// methods without making the execution library depend on either component.
class PlanStateFactory {
public:
    PlanStateFactory(const PlanStateFactory&) = delete;
    auto operator=(const PlanStateFactory&) -> PlanStateFactory& = delete;
    PlanStateFactory(PlanStateFactory&&) = delete;
    auto operator=(PlanStateFactory&&) -> PlanStateFactory& = delete;
    virtual ~PlanStateFactory() = default;

    virtual auto create(const planner::Plan& plan) -> base::Result<std::unique_ptr<PlanState>> = 0;

protected:
    PlanStateFactory() = default;
};

}  // namespace db::execution
