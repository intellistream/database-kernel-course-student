#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_key.hpp"
#include "db/base/error.hpp"

namespace db::access {
namespace {

auto bytes(std::initializer_list<unsigned int> values) -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive_layout(BtreeKeyLayout& layout) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(layout.valid());
    EXPECT_TRUE(layout.column_types().empty());
    EXPECT_EQ(layout.column_count(), 0U);
    EXPECT_EQ(layout.maximum_encoded_size(), 0U);
    EXPECT_EQ(layout.type_signature(), 0U);
    EXPECT_FALSE(layout.bytewise_comparable());

    const auto no_values = std::span<const types::Value>{};
    const auto no_positions = std::span<const std::size_t>{};
    expect_unavailable(layout.encode(no_values));
    expect_unavailable(layout.encode_prefix(no_values));
    expect_unavailable(layout.encode_projected(no_values, no_positions));
    expect_unavailable(layout.validate({}));
    expect_unavailable(layout.contains_null({}));
    expect_unavailable(layout.compare({}, {}));
    expect_unavailable(layout.compare_prefix({}, {}, 0U));

    EncodedBtreeKey output{std::byte{0x7f}};
    expect_unavailable(layout.encode_into(no_values, output));
    EXPECT_EQ(output, EncodedBtreeKey{std::byte{0x7f}});
    expect_unavailable(layout.encode_projected_into(no_values, no_positions, output));
    EXPECT_EQ(output, EncodedBtreeKey{std::byte{0x7f}});
    expect_unavailable(layout.encode_projected_canonical_into(no_values, no_positions, output));
    EXPECT_EQ(output, EncodedBtreeKey{std::byte{0x7f}});
}

TEST(BtreeKeyLayoutTest, MoveTransfersTheLayoutAndLeavesTheSourceInert) {
    auto source = BtreeKeyLayout::create({types::kInt64Type, types::kBoolType}).value();
    const auto signature = source.type_signature();

    BtreeKeyLayout moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.column_count(), 2U);
    EXPECT_EQ(moved.type_signature(), signature);
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_layout(source);

    auto assigned = BtreeKeyLayout::create({types::kBoolType}).value();
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(assigned.type_signature(), signature);
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_layout(moved);
}

TEST(BtreeKeyLayoutTest, ProjectsCompositeKeyAndOverwritesAReusedOutputObject) {
    const auto layout = BtreeKeyLayout::create(
        {types::kInt64Type, types::DataType{types::kCharTypeId, types::TypeModifier{16}}}
    );
    ASSERT_TRUE(layout);

    const std::vector<types::Value> first_row{
        types::Value::character("long-value", types::TypeModifier{16}),
        types::Value::boolean(false),
        types::Value::integer(7),
    };
    const std::vector<std::size_t> positions{2U, 0U};
    EncodedBtreeKey output{std::byte{0x7f}};
    ASSERT_TRUE(layout->encode_projected_into(first_row, positions, output));
    EXPECT_EQ(output, bytes({0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x01, 0x00,
                             0x0a, 'l',  'o',  'n',  'g',  '-',  'v',  'a',  'l',  'u',  'e'}));

    const std::vector<types::Value> second_row{
        types::Value::character("x", types::TypeModifier{16}),
        types::Value::boolean(true),
        types::Value::integer(-2),
    };
    ASSERT_TRUE(layout->encode_projected_into(second_row, positions, output));
    EXPECT_EQ(
        output,
        bytes({0x01, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x01, 0x00, 0x01, 'x'})
    );

    EncodedBtreeKey canonical;
    ASSERT_TRUE(layout->encode_projected_canonical_into(second_row, positions, canonical));
    EXPECT_EQ(canonical, output);
}

TEST(BtreeKeyLayoutTest, OrdersNullsAndEveryBuiltInKeyType) {
    const auto integer = BtreeKeyLayout::create({types::kInt64Type});
    const auto floating = BtreeKeyLayout::create({types::kFloat32Type});
    const auto boolean = BtreeKeyLayout::create({types::kBoolType});
    const auto timestamp = BtreeKeyLayout::create({types::kTimestampType});
    const auto character =
        BtreeKeyLayout::create({types::DataType{types::kCharTypeId, types::TypeModifier{8}}});
    ASSERT_TRUE(integer);
    ASSERT_TRUE(floating);
    ASSERT_TRUE(boolean);
    ASSERT_TRUE(timestamp);
    ASSERT_TRUE(character);

    const auto null_integer = integer->encode({types::Value::null(types::kInt64Type)});
    const auto zero = integer->encode({types::Value::integer(0)});
    const auto negative = integer->encode({types::Value::integer(-10)});
    ASSERT_TRUE(null_integer);
    ASSERT_TRUE(zero);
    ASSERT_TRUE(negative);
    EXPECT_TRUE(integer->contains_null(null_integer.value()).value());
    EXPECT_EQ(
        integer->compare(null_integer.value(), zero.value()).value(),
        std::strong_ordering::less
    );
    EXPECT_EQ(integer->compare(negative.value(), zero.value()).value(), std::strong_ordering::less);

    const auto negative_infinity =
        floating->encode({types::Value::floating(-std::numeric_limits<float>::infinity())});
    const auto positive_infinity =
        floating->encode({types::Value::floating(std::numeric_limits<float>::infinity())});
    const auto nan = floating->encode(
        {types::Value::floating(std::bit_cast<float>(std::uint32_t{0x7fc0'1234U}))}
    );
    ASSERT_TRUE(negative_infinity);
    ASSERT_TRUE(positive_infinity);
    ASSERT_TRUE(nan);
    EXPECT_EQ(
        floating->compare(negative_infinity.value(), positive_infinity.value()).value(),
        std::strong_ordering::less
    );
    EXPECT_EQ(
        floating->compare(positive_infinity.value(), nan.value()).value(),
        std::strong_ordering::less
    );

    const auto false_key = boolean->encode({types::Value::boolean(false)});
    const auto true_key = boolean->encode({types::Value::boolean(true)});
    ASSERT_TRUE(false_key);
    ASSERT_TRUE(true_key);
    EXPECT_EQ(
        boolean->compare(false_key.value(), true_key.value()).value(),
        std::strong_ordering::less
    );

    const auto before_epoch =
        timestamp->encode({types::Value::timestamp(types::TimestampValue{-1})});
    const auto epoch = timestamp->encode({types::Value::timestamp(types::TimestampValue{0})});
    ASSERT_TRUE(before_epoch);
    ASSERT_TRUE(epoch);
    EXPECT_EQ(
        timestamp->compare(before_epoch.value(), epoch.value()).value(),
        std::strong_ordering::less
    );

    const auto b = character->encode({types::Value::character("b", types::TypeModifier{8})});
    const auto az = character->encode({types::Value::character("az", types::TypeModifier{8})});
    ASSERT_TRUE(b);
    ASSERT_TRUE(az);
    EXPECT_EQ(character->compare(b.value(), az.value()).value(), std::strong_ordering::greater);
}

TEST(BtreeKeyLayoutTest, ValidatesExactValueTypesAndProjectionBoundsAtomically) {
    const auto layout = BtreeKeyLayout::create({types::kInt64Type, types::kBoolType});
    ASSERT_TRUE(layout);
    EncodedBtreeKey output = bytes({0xa5});

    const std::vector<types::Value> row{types::Value::integer(7)};
    const auto too_short = layout->encode_into(row, output);
    ASSERT_FALSE(too_short);
    EXPECT_EQ(too_short.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(output, bytes({0xa5}));

    const std::vector<types::Value> mismatched{
        types::Value::integer(7),
        types::Value::integer(1),
    };
    const auto wrong_type = layout->encode_into(mismatched, output);
    ASSERT_FALSE(wrong_type);
    EXPECT_EQ(wrong_type.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(output, bytes({0xa5}));

    const std::vector<std::size_t> out_of_range{0U, 2U};
    const auto invalid_projection = layout->encode_projected_into(mismatched, out_of_range, output);
    ASSERT_FALSE(invalid_projection);
    EXPECT_EQ(invalid_projection.error().code(), base::ErrorCode::kOutOfRange);
    EXPECT_EQ(output, bytes({0xa5}));

    const std::vector<std::size_t> no_positions;
    const auto missing_projection = layout->encode_projected_into(mismatched, no_positions, output);
    ASSERT_FALSE(missing_projection);
    EXPECT_EQ(missing_projection.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(output, bytes({0xa5}));
}

TEST(BtreeKeyLayoutTest, RejectsUnstorableAndOversizedLayouts) {
    const auto empty = BtreeKeyLayout::create({});
    const auto unknown = BtreeKeyLayout::create({types::kUnknownType});
    const auto unbounded_character = BtreeKeyLayout::create({types::kCharType});
    const types::DataType widest_character{types::kCharTypeId, types::TypeModifier{4048}};
    const auto oversized =
        BtreeKeyLayout::create(std::vector<types::DataType>(17U, widest_character));

    EXPECT_FALSE(empty);
    EXPECT_FALSE(unknown);
    EXPECT_FALSE(unbounded_character);
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(BtreeKeyLayoutTest, RejectsMalformedAndNoncanonicalEncodedKeys) {
    const auto integer = BtreeKeyLayout::create({types::kInt64Type});
    const auto floating = BtreeKeyLayout::create({types::kFloat32Type});
    const auto boolean = BtreeKeyLayout::create({types::kBoolType});
    const auto fixed_composite =
        BtreeKeyLayout::create({types::kInt64Type, types::kBoolType, types::kTimestampType});
    const auto character =
        BtreeKeyLayout::create({types::DataType{types::kCharTypeId, types::TypeModifier{2}}});
    ASSERT_TRUE(integer);
    ASSERT_TRUE(floating);
    ASSERT_TRUE(boolean);
    ASSERT_TRUE(fixed_composite);
    ASSERT_TRUE(character);

    auto valid = integer->encode({types::Value::integer(1)}).value();
    valid.push_back(std::byte{0});
    const auto trailing = integer->validate(valid);
    ASSERT_FALSE(trailing);
    EXPECT_EQ(trailing.error().code(), base::ErrorCode::kCorruption);

    const auto unknown_tag = integer->validate(bytes({0x02}));
    ASSERT_FALSE(unknown_tag);
    EXPECT_EQ(unknown_tag.error().code(), base::ErrorCode::kCorruption);

    const auto truncated_integer = integer->validate(bytes({0x01, 0x80}));
    ASSERT_FALSE(truncated_integer);
    EXPECT_EQ(truncated_integer.error().code(), base::ErrorCode::kCorruption);

    const auto invalid_boolean = boolean->validate(bytes({0x01, 0x02}));
    ASSERT_FALSE(invalid_boolean);
    EXPECT_EQ(invalid_boolean.error().code(), base::ErrorCode::kCorruption);

    const auto noncanonical_nan = floating->validate(bytes({0x01, 0xff, 0xc0, 0x12, 0x34}));
    ASSERT_FALSE(noncanonical_nan);
    EXPECT_EQ(noncanonical_nan.error().code(), base::ErrorCode::kCorruption);

    const auto valid_fixed_composite = fixed_composite->encode(
        {types::Value::null(types::kInt64Type),
         types::Value::boolean(true),
         types::Value::timestamp(types::TimestampValue{42})}
    );
    ASSERT_TRUE(valid_fixed_composite);
    EXPECT_TRUE(fixed_composite->validate(valid_fixed_composite.value()));

    const auto overlong_character = character->validate(bytes({0x01, 0x00, 0x03, 'a', 'b', 'c'}));
    ASSERT_FALSE(overlong_character);
    EXPECT_EQ(overlong_character.error().code(), base::ErrorCode::kCorruption);
}

TEST(BtreeKeyLayoutTest, ComparesPrefixColumnsButRequiresCompleteFullKeys) {
    const auto layout = BtreeKeyLayout::create({types::kInt64Type, types::kBoolType});
    ASSERT_TRUE(layout);

    const auto full = layout->encode({types::Value::integer(1), types::Value::boolean(true)});
    const auto greater = layout->encode({types::Value::integer(2), types::Value::boolean(false)});
    const std::array prefix_values{types::Value::integer(1)};
    const auto prefix = layout->encode_prefix(prefix_values);
    ASSERT_TRUE(full);
    ASSERT_TRUE(greater);
    ASSERT_TRUE(prefix);

    EXPECT_EQ(
        layout->compare_prefix(full.value(), prefix.value(), 1U).value(),
        std::strong_ordering::equal
    );
    EXPECT_EQ(
        layout->compare_prefix(greater.value(), prefix.value(), 1U).value(),
        std::strong_ordering::greater
    );
    const auto incomplete = layout->compare(full.value(), prefix.value());
    ASSERT_FALSE(incomplete);
    EXPECT_EQ(incomplete.error().code(), base::ErrorCode::kCorruption);
    auto corrupt_greater = greater.value();
    corrupt_greater.pop_back();
    const auto different_prefix_with_corrupt_suffix =
        layout->compare(full.value(), corrupt_greater);
    ASSERT_FALSE(different_prefix_with_corrupt_suffix);
    EXPECT_EQ(different_prefix_with_corrupt_suffix.error().code(), base::ErrorCode::kCorruption);
}

TEST(BtreeKeyLayoutTest, HasAStableOrderSensitiveTypeSignatureAndMaximumSize) {
    const auto first = BtreeKeyLayout::create({types::kInt64Type, types::kBoolType});
    const auto same = BtreeKeyLayout::create({types::kInt64Type, types::kBoolType});
    const auto reordered = BtreeKeyLayout::create({types::kBoolType, types::kInt64Type});
    const auto char_four =
        BtreeKeyLayout::create({types::DataType{types::kCharTypeId, types::TypeModifier{4}}});
    const auto char_five =
        BtreeKeyLayout::create({types::DataType{types::kCharTypeId, types::TypeModifier{5}}});
    ASSERT_TRUE(first);
    ASSERT_TRUE(same);
    ASSERT_TRUE(reordered);
    ASSERT_TRUE(char_four);
    ASSERT_TRUE(char_five);

    EXPECT_EQ(first->type_signature(), 0x78b3'c9aa'838c'5768ULL);
    EXPECT_EQ(first->type_signature(), same->type_signature());
    EXPECT_NE(first->type_signature(), reordered->type_signature());
    EXPECT_NE(char_four->type_signature(), char_five->type_signature());
    EXPECT_EQ(first->maximum_encoded_size(), 11U);
    EXPECT_EQ(char_four->maximum_encoded_size(), 7U);
    EXPECT_TRUE(first->bytewise_comparable());
    EXPECT_FALSE(char_four->bytewise_comparable());
}

}  // namespace
}  // namespace db::access
