#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/execution/exec_context.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

auto make_parameter_namespace() -> base::Result<planner::ParameterNamespace> {
    const std::vector external_slots{
        planner::ExternalParameterSlot{
            .slot = planner::ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
        planner::ExternalParameterSlot{
            .slot = planner::ExternalParameterNumber{2U},
            .data_type = types::kBoolType,
        },
    };
    return planner::ParameterNamespace::from_external_slots(external_slots);
}

void expect_empty_context(
    ExecContext& context,
    const planner::PlanParameterReference& prior_reference
) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(context.parameter_count(), 0U);
    EXPECT_EQ(context.external_parameter_count(), 0U);
    context.clear_executor_parameters();
    const auto unavailable = context.parameter(prior_reference);
    ASSERT_FALSE(unavailable);
    EXPECT_EQ(unavailable.error().code(), base::ErrorCode::kExecution);
    const auto snapshot = context.snapshot();
    EXPECT_EQ(snapshot.parameter_count(), 0U);
    EXPECT_EQ(snapshot.external_parameter_count(), 0U);
}

TEST(ExecContextTest, OwnsExternalValuesAndBindsExecutorParametersByTypedReference) {
    auto parameter_namespace = make_parameter_namespace();
    ASSERT_TRUE(parameter_namespace) << parameter_namespace.error().to_string();
    const auto executor_reference =
        parameter_namespace->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor_reference) << executor_reference.error().to_string();

    const std::vector external_values{
        types::Value::integer(41),
        types::Value::null(types::kBoolType),
    };
    auto context = ExecContext::create(parameter_namespace.value(), external_values);
    ASSERT_TRUE(context) << context.error().to_string();
    EXPECT_EQ(context->parameter_count(), 3U);
    EXPECT_EQ(context->external_parameter_count(), 2U);
    ASSERT_NE(context->parameter_descriptor(), nullptr);
    EXPECT_EQ(context->parameter_descriptor()->slots().size(), 3U);

    const auto first_reference =
        parameter_namespace->external_reference(planner::ExternalParameterNumber{1U});
    ASSERT_TRUE(first_reference) << first_reference.error().to_string();
    const auto first_value = context->parameter(first_reference.value());
    ASSERT_TRUE(first_value) << first_value.error().to_string();
    EXPECT_EQ(first_value->get(), types::Value::integer(41));

    const auto initially_bound = context->executor_parameter_is_bound(executor_reference.value());
    ASSERT_TRUE(initially_bound) << initially_bound.error().to_string();
    EXPECT_FALSE(initially_bound.value());
    const auto unbound = context->parameter(executor_reference.value());
    ASSERT_FALSE(unbound);
    EXPECT_EQ(unbound.error().code(), base::ErrorCode::kExecution);

    const auto set =
        context->set_executor_parameter(executor_reference.value(), types::Value::integer(77));
    ASSERT_TRUE(set) << set.error().to_string();
    const auto bound = context->executor_parameter_is_bound(executor_reference.value());
    ASSERT_TRUE(bound) << bound.error().to_string();
    EXPECT_TRUE(bound.value());
    const auto executor_value = context->parameter(executor_reference.value());
    ASSERT_TRUE(executor_value) << executor_value.error().to_string();
    EXPECT_EQ(executor_value->get(), types::Value::integer(77));

    context->clear_executor_parameters();
    const auto cleared = context->executor_parameter_is_bound(executor_reference.value());
    ASSERT_TRUE(cleared) << cleared.error().to_string();
    EXPECT_FALSE(cleared.value());
}

TEST(ExecContextTest, RejectsWrongCountsAndForgedReferencesWithoutMutatingSlots) {
    auto parameter_namespace = make_parameter_namespace();
    ASSERT_TRUE(parameter_namespace) << parameter_namespace.error().to_string();
    const auto executor_reference =
        parameter_namespace->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor_reference) << executor_reference.error().to_string();

    const std::vector too_few{types::Value::integer(1)};
    const auto missing_external = ExecContext::create(parameter_namespace.value(), too_few);
    ASSERT_FALSE(missing_external);
    EXPECT_EQ(missing_external.error().code(), base::ErrorCode::kInvalidArgument);

    const std::vector valid_external{
        types::Value::integer(5),
        types::Value::boolean(true),
    };
    auto context = ExecContext::create(parameter_namespace.value(), valid_external);
    ASSERT_TRUE(context) << context.error().to_string();

    const auto wrong_value =
        context->set_executor_parameter(executor_reference.value(), types::Value::boolean(false));
    ASSERT_FALSE(wrong_value);
    EXPECT_EQ(wrong_value.error().code(), base::ErrorCode::kInvalidArgument);

    const planner::PlanParameterReference forged{
        .id = executor_reference->id,
        .kind = planner::PlanParameterKind::kExternal,
        .data_type = types::kInt64Type,
    };
    const auto forged_read = context->parameter(forged);
    ASSERT_FALSE(forged_read);
    EXPECT_EQ(forged_read.error().code(), base::ErrorCode::kExecution);

    const auto external_reference =
        parameter_namespace->external_reference(planner::ExternalParameterNumber{1U});
    ASSERT_TRUE(external_reference) << external_reference.error().to_string();
    const auto external_clear = context->clear_executor_parameter(external_reference.value());
    ASSERT_FALSE(external_clear);
    EXPECT_EQ(external_clear.error().code(), base::ErrorCode::kExecution);

    const auto still_unbound = context->executor_parameter_is_bound(executor_reference.value());
    ASSERT_TRUE(still_unbound) << still_unbound.error().to_string();
    EXPECT_FALSE(still_unbound.value());
}

TEST(ExecContextTest, RejectsAnInactivePlannerParameterNamespace) {
    auto created = make_parameter_namespace();
    ASSERT_TRUE(created) << created.error().to_string();
    planner::ParameterNamespace source = std::move(created).value();
    planner::ParameterNamespace moved{std::move(source)};
    EXPECT_TRUE(moved.valid());

    const std::vector external_values{
        types::Value::integer(1),
        types::Value::boolean(false),
    };
    // Passing the documented inert source verifies the execution consumer boundary.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    const auto context = ExecContext::create(source, external_values);
    ASSERT_FALSE(context);
    EXPECT_EQ(context.error().code(), base::ErrorCode::kUnavailable);
}

TEST(ExecContextTest, MovesBindingsAndResetsTheSourceToAValidEmptyNamespace) {
    auto parameter_namespace = make_parameter_namespace();
    ASSERT_TRUE(parameter_namespace) << parameter_namespace.error().to_string();
    const auto executor_reference =
        parameter_namespace->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor_reference) << executor_reference.error().to_string();
    const std::vector external_values{
        types::Value::integer(41),
        types::Value::boolean(true),
    };
    auto created = ExecContext::create(parameter_namespace.value(), external_values);
    ASSERT_TRUE(created) << created.error().to_string();
    ASSERT_TRUE(
        created->set_executor_parameter(executor_reference.value(), types::Value::integer(77))
    );

    ExecContext source = std::move(created).value();
    ExecContext moved{std::move(source)};
    EXPECT_EQ(moved.parameter_count(), 3U);
    EXPECT_EQ(moved.external_parameter_count(), 2U);
    // Inspecting the documented empty source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_empty_context(source, executor_reference.value());

    ExecContext destination = moved.snapshot();
    destination.clear_executor_parameters();
    destination = std::move(moved);
    const auto retained = destination.parameter(executor_reference.value());
    ASSERT_TRUE(retained) << retained.error().to_string();
    EXPECT_EQ(retained->get(), types::Value::integer(77));
    // Inspecting the documented empty source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_empty_context(moved, executor_reference.value());
}

TEST(ExecContextTest, DoesNotBorrowThePlannerNamespaceOrCallerParameterStorage) {
    auto build_context = []() -> base::Result<ExecContext> {
        auto parameter_namespace = make_parameter_namespace();
        if (!parameter_namespace) {
            return base::fail(std::move(parameter_namespace).error());
        }
        const std::vector external_values{
            types::Value::integer(19),
            types::Value::boolean(false),
        };
        return ExecContext::create(parameter_namespace.value(), external_values);
    };

    auto context = build_context();
    ASSERT_TRUE(context) << context.error().to_string();
    const planner::PlanParameterReference first{
        .id = planner::PlanParameterId{0U},
        .kind = planner::PlanParameterKind::kExternal,
        .data_type = types::kInt64Type,
    };
    const auto value = context->parameter(first);
    ASSERT_TRUE(value) << value.error().to_string();
    EXPECT_EQ(value->get(), types::Value::integer(19));
}

TEST(ExecContextTest, SnapshotRetainsTheCurrentParameterBindingsIndependently) {
    auto parameter_namespace = make_parameter_namespace();
    ASSERT_TRUE(parameter_namespace) << parameter_namespace.error().to_string();
    const auto executor_reference =
        parameter_namespace->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(executor_reference) << executor_reference.error().to_string();
    const std::vector external_values{
        types::Value::integer(41),
        types::Value::boolean(true),
    };
    auto context = ExecContext::create(parameter_namespace.value(), external_values);
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(
        context->set_executor_parameter(executor_reference.value(), types::Value::integer(77))
    );

    auto frozen = context->snapshot();
    EXPECT_EQ(frozen.parameter_descriptor(), context->parameter_descriptor());
    ASSERT_TRUE(
        context->set_executor_parameter(executor_reference.value(), types::Value::integer(88))
    );
    const auto retained = frozen.parameter(executor_reference.value());
    ASSERT_TRUE(retained) << retained.error().to_string();
    EXPECT_EQ(retained->get(), types::Value::integer(77));
    const auto current = context->parameter(executor_reference.value());
    ASSERT_TRUE(current) << current.error().to_string();
    EXPECT_EQ(current->get(), types::Value::integer(88));
}

}  // namespace
}  // namespace db::execution
