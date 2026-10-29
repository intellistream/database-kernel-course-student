#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/io/socket.hpp"
#include "db/protocol/frame_codec.hpp"
#include "db/test_support/test_directory.hpp"

#include "server_application.hpp"
#include "server_test_support.hpp"

namespace db::server {
namespace {

static_assert(std::is_move_constructible_v<ServerApplication>);
static_assert(!std::is_move_assignable_v<ServerApplication>);
static_assert(!std::is_copy_constructible_v<ServerApplication>);

struct ReceivedFrame final {
    protocol::FrameTag tag{protocol::FrameTag::kError};
    std::vector<std::byte> payload;
};

auto read_server_frame(io::TcpStream& stream) -> base::Result<ReceivedFrame> {
    std::array<std::byte, protocol::kRmdbFrameHeaderSize> header{};
    auto read_header = stream.read_exact(header);
    if (!read_header) {
        return base::fail(std::move(read_header).error());
    }
    base::BigEndianReader reader{header};
    auto payload_length = reader.read_u32();
    if (!payload_length) {
        return base::fail(std::move(payload_length).error());
    }
    auto tag = reader.read_u8();
    if (!tag) {
        return base::fail(std::move(tag).error());
    }
    std::vector<std::byte> payload(payload_length.value());
    auto read_payload = stream.read_exact(payload);
    if (!read_payload) {
        return base::fail(std::move(read_payload).error());
    }
    return ReceivedFrame{
        .tag = static_cast<protocol::FrameTag>(tag.value()),
        .payload = std::move(payload),
    };
}

auto send_exec_stream(
    io::TcpStream& stream,
    protocol::FrameEncoder& encoder,
    const std::string_view sql
) -> base::Status {
    std::vector<std::byte> payload;
    payload.reserve(sql.size());
    for (const char character : sql) {
        payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    auto frame = encoder.encode(
        protocol::FrameDirection::kClientToServer,
        protocol::FrameTag::kExecStream,
        protocol::kRmdbNoFrameFlags,
        payload
    );
    if (!frame) {
        return base::fail(std::move(frame).error());
    }
    return stream.write_all(frame.value());
}

TEST(ServerApplicationTest, MoveConstructionTransfersTheOnlyRetryableLifecycleOwner) {
    auto root = test_support::TestDirectory::create("server-application-move");
    ASSERT_TRUE(root) << root.error().to_string();
    ServerOptions options;
    options.data_directory = root->path();
    options.endpoint = io::TcpEndpoint::loopback(0U);
    options.buffer_pool_pages = 8U;

    auto opened = ServerApplication::open(options);
    ASSERT_TRUE(opened) << opened.error().to_string();
    auto endpoint = opened->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();

    ServerApplication source = std::move(opened).value();
    ServerApplication moved{std::move(source)};
    auto moved_endpoint = moved.local_endpoint();
    ASSERT_TRUE(moved_endpoint) << moved_endpoint.error().to_string();
    EXPECT_EQ(moved_endpoint.value(), endpoint.value());

    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    auto source_endpoint = source.local_endpoint();
    ASSERT_FALSE(source_endpoint);
    EXPECT_EQ(source_endpoint.error().code(), base::ErrorCode::kUnavailable);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(source.shutdown());
    ASSERT_TRUE(moved.shutdown());

    auto reopened = ServerApplication::open(options);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    ASSERT_TRUE(reopened->shutdown());
}

TEST(ServerApplicationTest, RunsTheRealApiAdapterThroughTheTcpServer) {
    auto root = test_support::TestDirectory::create("server-application");
    ASSERT_TRUE(root) << root.error().to_string();
    ServerOptions options;
    options.data_directory = root->path();
    options.endpoint = io::TcpEndpoint::loopback(0U);
    options.buffer_pool_pages = 8U;
    options.tcp.maximum_concurrent_connections = 2U;
    options.tcp.listen_backlog = 8U;
    auto application = ServerApplication::open(options);
    ASSERT_TRUE(application) << application.error().to_string();
    auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto client = io::TcpStream::connect(endpoint.value());
    ASSERT_TRUE(client) << client.error().to_string();
    ASSERT_TRUE(client->write_all(protocol::kRmdbV3Preface));
    std::array<std::byte, protocol::kRmdbV3PrefaceSize> echoed{};
    ASSERT_TRUE(client->read_exact(echoed));
    EXPECT_EQ(echoed, protocol::kRmdbV3Preface);

    protocol::FrameEncoder encoder;
    ASSERT_TRUE(
        send_exec_stream(*client, encoder, "CREATE TABLE wire_items (id INT, label CHAR(8))")
    );
    auto create_response = read_server_frame(*client);
    ASSERT_TRUE(create_response) << create_response.error().to_string();
    EXPECT_EQ(create_response->tag, protocol::FrameTag::kCommandOk);
    EXPECT_TRUE(create_response->payload.empty());

    ASSERT_TRUE(send_exec_stream(*client, encoder, "INSERT INTO wire_items VALUES (7, 'seven')"));
    auto insert_response = read_server_frame(*client);
    ASSERT_TRUE(insert_response) << insert_response.error().to_string();
    EXPECT_EQ(insert_response->tag, protocol::FrameTag::kCommandOk);

    ASSERT_TRUE(send_exec_stream(*client, encoder, "SELECT id, label FROM wire_items"));
    auto metadata = read_server_frame(*client);
    ASSERT_TRUE(metadata) << metadata.error().to_string();
    EXPECT_EQ(metadata->tag, protocol::FrameTag::kMeta);
    auto row = read_server_frame(*client);
    ASSERT_TRUE(row) << row.error().to_string();
    EXPECT_EQ(row->tag, protocol::FrameTag::kRow);
    auto end = read_server_frame(*client);
    ASSERT_TRUE(end) << end.error().to_string();
    EXPECT_EQ(end->tag, protocol::FrameTag::kResultEnd);

    ASSERT_TRUE(client->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

}  // namespace
}  // namespace db::server
