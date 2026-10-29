#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

auto make_descriptor() -> TupleDescriptorHandle {
    auto descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kInt64Type},
        TupleColumn{
            .name = "name",
            .data_type = types::DataType{types::kCharTypeId, types::TypeModifier{8}}
        },
    });
    EXPECT_TRUE(descriptor) << descriptor.error().to_string();
    return std::move(descriptor).value();
}

TEST(TupleDescriptorTest, AcceptsDuplicateProjectionNamesAndSharesImmutableShape) {
    auto descriptor = TupleDescriptor::create({
        TupleColumn{.name = "value", .data_type = types::kInt64Type},
        TupleColumn{.name = "value", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(descriptor) << descriptor.error().to_string();
    EXPECT_EQ(descriptor->get()->size(), 2U);
    EXPECT_EQ(descriptor->get()->columns()[1U].name, "value");

    auto tuple =
        Tuple::create(descriptor.value(), {types::Value::integer(7), types::Value::integer(9)});
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    // This assertion intentionally exercises Tuple's value-copy contract.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const Tuple copied = tuple.value();
    EXPECT_EQ(copied.descriptor().get(), tuple->descriptor().get());
    EXPECT_EQ(copied.values()[1U], types::Value::integer(9));
}

TEST(TupleDescriptorTest, RejectsInvalidTypeAndAdaptsPlannerOutputSchema) {
    const auto invalid = TupleDescriptor::create({
        TupleColumn{.name = "bad", .data_type = types::DataType{base::TypeId{999U}}},
    });
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);

    const planner::PlanSchema schema{
        .columns = {
            {.name = "id", .data_type = types::kInt64Type, .origin = std::nullopt},
            {.name = "flag", .data_type = types::kBoolType, .origin = std::nullopt},
        },
    };
    const auto descriptor = TupleDescriptor::from_plan_schema(schema);
    ASSERT_TRUE(descriptor) << descriptor.error().to_string();
    ASSERT_EQ(descriptor->get()->columns().size(), 2U);
    EXPECT_EQ(descriptor->get()->columns()[0U].data_type, types::kInt64Type);
    EXPECT_EQ(descriptor->get()->columns()[1U].data_type, types::kBoolType);

    const planner::PlanSchema invalid_schema{
        .columns = {
            {
                .name = "bad",
                .data_type = types::DataType{base::TypeId{999U}},
                .origin = std::nullopt,
            },
        },
    };
    const auto invalid_adaptation = TupleDescriptor::from_plan_schema(invalid_schema);
    ASSERT_FALSE(invalid_adaptation);
    EXPECT_EQ(invalid_adaptation.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(TupleTest, RequiresExactArityAndTypesButPreservesTypedNullAndRid) {
    const TupleDescriptorHandle descriptor = make_descriptor();

    const auto wrong_arity = Tuple::create(descriptor, {types::Value::integer(7)});
    ASSERT_FALSE(wrong_arity);
    EXPECT_EQ(wrong_arity.error().code(), base::ErrorCode::kInvalidArgument);

    const auto wrong_type =
        Tuple::create(descriptor, {types::Value::integer(7), types::Value::boolean(true)});
    ASSERT_FALSE(wrong_type);
    EXPECT_EQ(wrong_type.error().code(), base::ErrorCode::kInvalidArgument);

    const base::Rid rid{base::PageId{3U}, base::SlotId{4U}};
    const auto tuple = Tuple::create(
        descriptor,
        {
            types::Value::null(types::kInt64Type),
            types::Value::null(types::DataType{types::kCharTypeId, types::TypeModifier{8}}),
        },
        rid
    );
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    ASSERT_TRUE(tuple->rid().has_value());
    EXPECT_EQ(tuple->rid(), rid);
    ASSERT_TRUE(tuple->values()[0U].is_null());
    ASSERT_TRUE(tuple->values()[1U].is_null());

    const auto value = tuple->value_at(1U);
    ASSERT_TRUE(value) << value.error().to_string();
    EXPECT_EQ(
        value->get(),
        types::Value::null(types::DataType{types::kCharTypeId, types::TypeModifier{8}})
    );
    const auto missing = tuple->value_at(2U);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(TupleTest, SupportsAnEmptyLogicalShapeForCommandAndConstantOperators) {
    const auto descriptor = TupleDescriptor::create({});
    ASSERT_TRUE(descriptor) << descriptor.error().to_string();
    EXPECT_TRUE(descriptor->get()->empty());

    const auto tuple = Tuple::create(descriptor.value(), {});
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    EXPECT_TRUE(tuple->values().empty());

    auto slot = TupleSlot::create(descriptor.value());
    ASSERT_TRUE(slot) << slot.error().to_string();
    ASSERT_TRUE(slot->store_values({}));
    ASSERT_NE(slot->tuple(), nullptr);
    EXPECT_TRUE(slot->tuple()->values().empty());
}

TEST(TupleTest, TransfersOwnedValueStorageForCandidateRecycling) {
    const TupleDescriptorHandle descriptor = make_descriptor();
    std::vector<types::Value> values{
        types::Value::integer(7),
        types::Value::character("alice", types::TypeModifier{8}),
    };
    const types::Value* const allocation = values.data();
    auto tuple = Tuple::create(descriptor, std::move(values));
    ASSERT_TRUE(tuple) << tuple.error().to_string();

    std::vector<types::Value> consumed = std::move(tuple).value().into_values();
    EXPECT_EQ(consumed.data(), allocation);
    EXPECT_EQ(consumed[0U], types::Value::integer(7));
}

TEST(TupleSlotTest, RetainsItsDescriptorAndClearsOnlyItsMaterialization) {
    const TupleDescriptorHandle descriptor = make_descriptor();
    auto slot = TupleSlot::create(descriptor);
    ASSERT_TRUE(slot) << slot.error().to_string();
    EXPECT_TRUE(slot->empty());
    EXPECT_EQ(slot->tuple(), nullptr);

    const base::Rid rid{base::PageId{8U}, base::SlotId{2U}};
    const auto stored = slot->store_values(
        {
            types::Value::integer(1),
            types::Value::character("alice", types::TypeModifier{8}),
        },
        rid
    );
    ASSERT_TRUE(stored) << stored.error().to_string();
    ASSERT_FALSE(slot->empty());
    ASSERT_NE(slot->tuple(), nullptr);
    const Tuple* const resident_tuple = slot->tuple();
    EXPECT_EQ(slot->tuple()->rid(), rid);
    EXPECT_EQ(slot->tuple()->descriptor().get(), descriptor.get());

    const auto other_descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kInt64Type},
        TupleColumn{
            .name = "name",
            .data_type = types::DataType{types::kCharTypeId, types::TypeModifier{8}}
        },
    });
    ASSERT_TRUE(other_descriptor) << other_descriptor.error().to_string();
    auto other_tuple = Tuple::create(
        other_descriptor.value(),
        {
            types::Value::integer(2),
            types::Value::character("bob", types::TypeModifier{8}),
        }
    );
    ASSERT_TRUE(other_tuple) << other_tuple.error().to_string();
    const auto mismatched = slot->store(std::move(other_tuple).value());
    ASSERT_FALSE(mismatched);
    EXPECT_EQ(mismatched.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_NE(slot->tuple(), nullptr);
    EXPECT_EQ(slot->tuple()->values()[0U], types::Value::integer(1));

    slot->clear();
    EXPECT_TRUE(slot->empty());
    EXPECT_EQ(slot->tuple(), nullptr);
    EXPECT_EQ(slot->descriptor().get(), descriptor.get());

    ASSERT_TRUE(slot->store_values({
        types::Value::integer(3),
        types::Value::character("carol", types::TypeModifier{8}),
    }));
    EXPECT_EQ(slot->tuple(), resident_tuple);
}

TEST(TupleSlotTest, MoveTransfersTheTupleAndLeavesAReusableEmptySource) {
    const TupleDescriptorHandle descriptor = make_descriptor();
    auto created = TupleSlot::create(descriptor);
    ASSERT_TRUE(created) << created.error().to_string();
    ASSERT_TRUE(created->store_values({
        types::Value::integer(1),
        types::Value::character("alice", types::TypeModifier{8}),
    }));

    TupleSlot source = std::move(created).value();
    TupleSlot moved{std::move(source)};
    ASSERT_FALSE(moved.empty());
    ASSERT_NE(moved.tuple(), nullptr);
    EXPECT_EQ(moved.tuple()->values()[0U], types::Value::integer(1));

    // Inspecting and reusing the documented empty source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(source.descriptor().get(), descriptor.get());
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.tuple(), nullptr);
    ASSERT_TRUE(source.store_values({
        types::Value::integer(2),
        types::Value::character("bob", types::TypeModifier{8}),
    }));

    auto replacement = TupleSlot::create(descriptor);
    ASSERT_TRUE(replacement) << replacement.error().to_string();
    replacement.value() = std::move(moved);
    ASSERT_FALSE(replacement->empty());
    ASSERT_NE(replacement->tuple(), nullptr);
    EXPECT_EQ(replacement->tuple()->values()[0U], types::Value::integer(1));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(moved.descriptor().get(), descriptor.get());
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(moved.tuple(), nullptr);
}

}  // namespace
}  // namespace db::execution
