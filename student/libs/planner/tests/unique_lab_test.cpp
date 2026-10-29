#include <utility>
#include <variant>

#include <gtest/gtest.h>

#include "db/planner/plan.hpp"

#include "lowering_internal.hpp"

namespace db::planner {
namespace {

TEST(LabS06Q03LoweringTest, WrapsOwnedPlanAndCopiesTheWholeOutputSchema) {
    const PlanSchema input_schema{
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "text", .data_type = types::kCharType},
        }
    };
    Plan input{SeqScanPlan{
        .metadata = {.output = input_schema},
        .relation = {.oid = base::Oid{17U}, .name = "dto_only", .schema = input_schema},
        .filter = nullptr,
    }};
    PlanSchema output_schema = input_schema;
    auto result = detail::append_unique(std::move(input), output_schema);
    output_schema.columns[0U].name = "changed_after_call";
    const auto* unique = std::get_if<UniquePlan>(&result.node);
    ASSERT_NE(unique, nullptr);
    ASSERT_NE(unique->input, nullptr);
    EXPECT_EQ(unique->metadata.output, input_schema);
    const auto* child = std::get_if<SeqScanPlan>(&unique->input->node);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->metadata.output, input_schema);
    EXPECT_EQ(child->relation.oid, base::Oid{17U});
    EXPECT_EQ(child->relation.name, "dto_only");
    EXPECT_EQ(child->filter, nullptr);
}

TEST(LabS06Q03LoweringTest, PreservesAnExplicitZeroColumnOutput) {
    Plan input{SeqScanPlan{
        .metadata = {.output = {}},
        .relation = {.name = "empty_dto", .schema = {}},
        .filter = nullptr,
    }};
    auto result = detail::append_unique(std::move(input), {});
    const auto* unique = std::get_if<UniquePlan>(&result.node);
    ASSERT_NE(unique, nullptr);
    ASSERT_NE(unique->input, nullptr);
    EXPECT_TRUE(unique->metadata.output.columns.empty());
    const auto* child = std::get_if<SeqScanPlan>(&unique->input->node);
    ASSERT_NE(child, nullptr);
    EXPECT_TRUE(child->metadata.output.columns.empty());
}

}  // namespace
}  // namespace db::planner
