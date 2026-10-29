#include <type_traits>

#include "db/recovery/startup_runner.hpp"

static_assert(!std::is_default_constructible_v<db::recovery::StartupRecoveryRunContext>);
static_assert(std::is_copy_constructible_v<db::recovery::StartupRecoveryRunContext>);
static_assert(std::is_default_constructible_v<db::recovery::StartupRecoveryReport>);
static_assert(std::is_copy_constructible_v<db::recovery::StartupRecoveryReport>);

namespace {

[[maybe_unused]] constexpr auto kRunStartupRecovery = &db::recovery::run_startup_recovery;

}  // namespace
