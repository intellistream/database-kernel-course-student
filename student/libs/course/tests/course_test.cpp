#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/course/course.hpp"

namespace db::course {
namespace {

TEST(CourseTest, FoundationsAndStorageUseOwnedPublicContracts) {
    using namespace foundations;
    auto schema = ResultSchema::create({
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(schema) << schema.error().to_string();
    auto result = ResultSet::create(std::move(schema).value(), {{Value::integer(11)}});
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_EQ(result->at(0U)->get().at(0U)->get(), Value::integer(11));

    const storage::PageAddress address{base::RelationId{1}, base::PageId{0}};
    auto page = storage::Page::create(storage::PageKind::kHeap, address);
    ASSERT_TRUE(page) << page.error().to_string();
}

TEST(CourseTest, QueryAndTransactionTopicsExposeReusableBoundaries) {
    auto parsed = query::parse_sql("SELECT 1");
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    EXPECT_EQ(parsed->statements.size(), 1U);

    transaction::TransactionStateMachine state;
    auto started = state.begin_explicit();
    ASSERT_TRUE(started) << started.error().to_string();
    EXPECT_EQ(state.state(), transaction::TransactionBlockState::kInTransaction);
    auto command = state.start_command();
    ASSERT_TRUE(command) << command.error().to_string();
    auto finished = state.finish_command(transaction::CommandOutcome::kSucceeded);
    ASSERT_TRUE(finished) << finished.error().to_string();
    auto committed = state.commit_explicit();
    ASSERT_TRUE(committed) << committed.error().to_string();
    EXPECT_EQ(state.state(), transaction::TransactionBlockState::kIdle);
}

TEST(CourseTest, WalTopicBuildsAndRoundTripsAnOwnedRecord) {
    wal::WalRecord record{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{1},
        .resource_manager = wal::WalResourceManager::kHeap,
        .info = wal::opcode::heap::kInsert,
        .blocks = {},
        .main_payload = {std::byte{0x01}, std::byte{0x02}},
    };
    auto encoded = wal::encode_record(record);
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    const base::Bytes bytes{encoded->data(), encoded->size()};
    auto header = wal::decode_record_header(bytes.first(wal::kWalRecordHeaderSize));
    ASSERT_TRUE(header) << header.error().to_string();
    auto decoded = wal::decode_record(bytes, header.value());
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(decoded.value(), record);
}

}  // namespace
}  // namespace db::course
